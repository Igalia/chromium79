// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_NETWORK_DHCP_PAC_FILE_FETCHER_MOJO_H_
#define SERVICES_NETWORK_DHCP_PAC_FILE_FETCHER_MOJO_H_

#include <memory>

#include "base/component_export.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/completion_once_callback.h"
#include "net/proxy_resolution/dhcp_pac_file_fetcher.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/mojom/dhcp_wpad_url_client.mojom.h"
#include "url/gurl.h"

namespace net {
class URLRequestContext;
class NetLogWithSource;
class PacFileFetcher;
}  // namespace net

namespace network {

// Implementation of DhcpPacFileFetcher that gets the URL of the PAC file from
// the default network over a mojo pipe. The default network points to a single
// PAC file URL, provided by Shill, as reported over DHCP.
// Currently only used on ChromeOS.
class COMPONENT_EXPORT(NETWORK_SERVICE) DhcpPacFileFetcherMojo
    : public net::DhcpPacFileFetcher {
 public:
  DhcpPacFileFetcherMojo(
      net::URLRequestContext* url_request_context,
      network::mojom::DhcpWpadUrlClientPtr dhcp_wpad_url_client);

  ~DhcpPacFileFetcherMojo() override;

  // DhcpPacFileFetcher implementation
  int Fetch(base::string16* utf16_text,
            net::CompletionOnceCallback callback,
            const net::NetLogWithSource& net_log,
            const net::NetworkTrafficAnnotationTag traffic_annotation) override;
  void Cancel() override;
  void OnShutdown() override;
  const GURL& GetPacURL() const override;
  std::string GetFetcherName() const override;

  void SetPacFileFetcherForTesting(
      std::unique_ptr<net::PacFileFetcher> pac_file_fetcher);

 private:
  void ContinueFetch(base::string16* utf16_text,
                     const net::NetworkTrafficAnnotationTag traffic_annotation,
                     std::string pac_url);
  void OnFetchCompleted(int result);
  void OnPacUrlReceived(const std::string& url);

  net::CompletionOnceCallback callback_;
  base::string16* utf16_text_;
  GURL pac_url_;
  net::MutableNetworkTrafficAnnotationTag traffic_annotation_;
  std::unique_ptr<net::PacFileFetcher> pac_file_fetcher_;
  network::mojom::DhcpWpadUrlClientPtr dhcp_wpad_url_client_;

  base::WeakPtrFactory<DhcpPacFileFetcherMojo> weak_ptr_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(DhcpPacFileFetcherMojo);
};

}  // namespace network

#endif  // SERVICES_NETWORK_DHCP_PAC_FILE_FETCHER_MOJO_H_