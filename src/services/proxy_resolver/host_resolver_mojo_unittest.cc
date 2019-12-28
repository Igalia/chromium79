// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/proxy_resolver/host_resolver_mojo.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/test/task_environment.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/ip_address.h"
#include "net/base/net_errors.h"
#include "net/base/request_priority.h"
#include "net/base/test_completion_callback.h"
#include "net/log/net_log_with_source.h"
#include "net/test/event_waiter.h"
#include "net/test/gtest_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using net::test::IsError;
using net::test::IsOk;

namespace proxy_resolver {
namespace {

void Fail(int result) {
  FAIL() << "Unexpected callback called with error " << result;
}

class MockMojoHostResolverRequest {
 public:
  MockMojoHostResolverRequest(
      mojo::PendingRemote<mojom::HostResolverRequestClient> client,
      base::OnceClosure error_callback)
      : client_(std::move(client)), error_callback_(std::move(error_callback)) {
    client_.set_disconnect_handler(base::BindOnce(
        &MockMojoHostResolverRequest::OnDisconnect, base::Unretained(this)));
  }

  void OnDisconnect() { std::move(error_callback_).Run(); }

 private:
  mojo::Remote<mojom::HostResolverRequestClient> client_;
  base::OnceClosure error_callback_;
};

struct HostResolverAction {
  enum Action {
    COMPLETE,
    DROP,
    RETAIN,
  };

  static HostResolverAction ReturnError(net::Error error) {
    HostResolverAction result;
    result.error = error;
    return result;
  }

  static HostResolverAction ReturnResult(
      std::vector<net::IPAddress> addresses) {
    HostResolverAction result;
    result.addresses = std::move(addresses);
    return result;
  }

  static HostResolverAction DropRequest() {
    HostResolverAction result;
    result.action = DROP;
    return result;
  }

  static HostResolverAction RetainRequest() {
    HostResolverAction result;
    result.action = RETAIN;
    return result;
  }

  Action action = COMPLETE;
  std::vector<net::IPAddress> addresses;
  net::Error error = net::OK;
};

class MockMojoHostResolver : public HostResolverMojo::Impl {
 public:
  explicit MockMojoHostResolver(
      base::RepeatingClosure request_connection_error_callback)
      : request_connection_error_callback_(
            std::move(request_connection_error_callback)) {}

  ~MockMojoHostResolver() override {
    EXPECT_EQ(results_returned_, actions_.size());
  }

  void AddAction(HostResolverAction action) {
    actions_.push_back(std::move(action));
  }

  const std::vector<std::string>& requests() { return requests_received_; }

  void ResolveDns(
      const std::string& hostname,
      net::ProxyResolveDnsOperation operation,
      mojo::PendingRemote<mojom::HostResolverRequestClient> client) override {
    requests_received_.push_back(hostname);
    ASSERT_LE(results_returned_, actions_.size());
    switch (actions_[results_returned_].action) {
      case HostResolverAction::COMPLETE:
        mojo::Remote<mojom::HostResolverRequestClient>(std::move(client))
            ->ReportResult(actions_[results_returned_].error,
                           actions_[results_returned_].addresses);
        break;
      case HostResolverAction::RETAIN:
        requests_.push_back(std::make_unique<MockMojoHostResolverRequest>(
            std::move(client),
            base::BindOnce(request_connection_error_callback_)));
        break;
      case HostResolverAction::DROP:
        break;
    }
    results_returned_++;
  }

 private:
  std::vector<HostResolverAction> actions_;
  size_t results_returned_ = 0;
  std::vector<std::string> requests_received_;
  base::RepeatingClosure request_connection_error_callback_;
  std::vector<std::unique_ptr<MockMojoHostResolverRequest>> requests_;
};

}  // namespace

class HostResolverMojoTest : public testing::Test {
 protected:
  enum class ConnectionErrorSource {
    REQUEST,
  };
  using Waiter = net::EventWaiter<ConnectionErrorSource>;

  HostResolverMojoTest()
      : mock_resolver_(base::BindRepeating(&Waiter::NotifyEvent,
                                           base::Unretained(&waiter_),
                                           ConnectionErrorSource::REQUEST)),
        resolver_(&mock_resolver_) {}

  int Resolve(const std::string& hostname,
              std::vector<net::IPAddress>* out_addresses) {
    std::unique_ptr<net::ProxyHostResolver::Request> request =
        resolver_.CreateRequest(hostname,
                                net::ProxyResolveDnsOperation::DNS_RESOLVE_EX);

    net::TestCompletionCallback callback;
    int result = callback.GetResult(request->Start(callback.callback()));

    *out_addresses = request->GetResults();
    return result;
  }

  base::test::TaskEnvironment task_environment_;
  Waiter waiter_;
  MockMojoHostResolver mock_resolver_;
  HostResolverMojo resolver_;
};

TEST_F(HostResolverMojoTest, Basic) {
  std::vector<net::IPAddress> addresses;
  net::IPAddress address(1, 2, 3, 4);
  addresses.push_back(address);
  addresses.push_back(ConvertIPv4ToIPv4MappedIPv6(address));
  mock_resolver_.AddAction(HostResolverAction::ReturnResult(addresses));

  std::vector<net::IPAddress> result;
  EXPECT_THAT(Resolve("example.com", &result), IsOk());
  EXPECT_EQ(addresses, result);

  ASSERT_EQ(1u, mock_resolver_.requests().size());
  EXPECT_EQ("example.com", mock_resolver_.requests()[0]);
}

TEST_F(HostResolverMojoTest, ResolveCachedResult) {
  std::vector<net::IPAddress> addresses;
  net::IPAddress address(1, 2, 3, 4);
  addresses.push_back(address);
  addresses.push_back(ConvertIPv4ToIPv4MappedIPv6(address));
  mock_resolver_.AddAction(HostResolverAction::ReturnResult(addresses));

  // Load results into cache.
  std::vector<net::IPAddress> result;
  ASSERT_THAT(Resolve("example.com", &result), IsOk());
  ASSERT_EQ(1u, mock_resolver_.requests().size());

  // Expect results from cache.
  result.clear();
  EXPECT_THAT(Resolve("example.com", &result), IsOk());
  EXPECT_EQ(addresses, result);
  EXPECT_EQ(1u, mock_resolver_.requests().size());
}

TEST_F(HostResolverMojoTest, Multiple) {
  std::vector<net::IPAddress> addresses;
  addresses.emplace_back(1, 2, 3, 4);
  mock_resolver_.AddAction(HostResolverAction::ReturnResult(addresses));
  mock_resolver_.AddAction(
      HostResolverAction::ReturnError(net::ERR_NAME_NOT_RESOLVED));

  std::unique_ptr<net::ProxyHostResolver::Request> request1 =
      resolver_.CreateRequest("example.com",
                              net::ProxyResolveDnsOperation::DNS_RESOLVE_EX);
  std::unique_ptr<net::ProxyHostResolver::Request> request2 =
      resolver_.CreateRequest("example.org",
                              net::ProxyResolveDnsOperation::DNS_RESOLVE_EX);
  net::TestCompletionCallback callback1;
  net::TestCompletionCallback callback2;
  ASSERT_EQ(net::ERR_IO_PENDING, request1->Start(callback1.callback()));
  ASSERT_EQ(net::ERR_IO_PENDING, request2->Start(callback2.callback()));

  EXPECT_THAT(callback1.GetResult(net::ERR_IO_PENDING), IsOk());
  EXPECT_THAT(callback2.GetResult(net::ERR_IO_PENDING),
              IsError(net::ERR_NAME_NOT_RESOLVED));
  EXPECT_EQ(addresses, request1->GetResults());
  ASSERT_EQ(0u, request2->GetResults().size());

  EXPECT_THAT(mock_resolver_.requests(),
              testing::ElementsAre("example.com", "example.org"));
}

TEST_F(HostResolverMojoTest, Error) {
  mock_resolver_.AddAction(
      HostResolverAction::ReturnError(net::ERR_NAME_NOT_RESOLVED));

  std::vector<net::IPAddress> result;
  EXPECT_THAT(Resolve("example.com", &result),
              IsError(net::ERR_NAME_NOT_RESOLVED));
  EXPECT_TRUE(result.empty());

  ASSERT_EQ(1u, mock_resolver_.requests().size());
  EXPECT_EQ("example.com", mock_resolver_.requests()[0]);
}

TEST_F(HostResolverMojoTest, EmptyResult) {
  mock_resolver_.AddAction(HostResolverAction::ReturnError(net::OK));

  std::vector<net::IPAddress> result;
  EXPECT_THAT(Resolve("example.com", &result), IsOk());
  EXPECT_TRUE(result.empty());

  ASSERT_EQ(1u, mock_resolver_.requests().size());
}

TEST_F(HostResolverMojoTest, Cancel) {
  mock_resolver_.AddAction(HostResolverAction::RetainRequest());

  std::unique_ptr<net::ProxyHostResolver::Request> request =
      resolver_.CreateRequest("example.com",
                              net::ProxyResolveDnsOperation::DNS_RESOLVE_EX);
  request->Start(base::BindOnce(&Fail));

  request.reset();
  waiter_.WaitForEvent(ConnectionErrorSource::REQUEST);

  ASSERT_EQ(1u, mock_resolver_.requests().size());
  EXPECT_EQ("example.com", mock_resolver_.requests()[0]);
}

TEST_F(HostResolverMojoTest, ImplDropsClientConnection) {
  mock_resolver_.AddAction(HostResolverAction::DropRequest());

  std::vector<net::IPAddress> result;
  EXPECT_THAT(Resolve("example.com", &result), IsError(net::ERR_FAILED));
  EXPECT_TRUE(result.empty());

  ASSERT_EQ(1u, mock_resolver_.requests().size());
  EXPECT_EQ("example.com", mock_resolver_.requests()[0]);
}

}  // namespace proxy_resolver
