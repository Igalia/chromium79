// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_SIGNALING_FTL_MESSAGE_RECEPTION_CHANNEL_H_
#define REMOTING_SIGNALING_FTL_MESSAGE_RECEPTION_CHANNEL_H_

#include <list>
#include <memory>

#include "base/callback_forward.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "net/base/backoff_entry.h"
#include "remoting/signaling/message_reception_channel.h"

namespace remoting {

// Handles the lifetime and validity of the messaging stream used for FTL.
class FtlMessageReceptionChannel final : public MessageReceptionChannel {
 public:
  static constexpr base::TimeDelta kPongTimeout =
      base::TimeDelta::FromSeconds(15);

  FtlMessageReceptionChannel();
  ~FtlMessageReceptionChannel() override;

  // MessageReceptionChannel implementations.
  void Initialize(const StreamOpener& stream_opener,
                  const MessageCallback& on_incoming_msg) override;
  void StartReceivingMessages(base::OnceClosure on_ready,
                              DoneCallback on_closed) override;
  void StopReceivingMessages() override;
  bool IsReceivingMessages() const override;

  const net::BackoffEntry& GetReconnectRetryBackoffEntryForTesting() const;

 private:
  enum class State {
    // TODO(yuweih): Evaluate if this class needs to be reusable.
    STOPPED,

    // StartReceivingMessages() is called but the channel hasn't received a
    // signal from the server yet.
    STARTING,

    // Stream is started, or is dropped but being retried.
    STARTED,
  };

  void OnReceiveMessagesStreamReady();
  void OnReceiveMessagesStreamClosed(const grpc::Status& status);
  void OnMessageReceived(const ftl::ReceiveMessagesResponse& response);

  void RunStreamReadyCallbacks();
  void RunStreamClosedCallbacks(const grpc::Status& status);
  void RetryStartReceivingMessagesWithBackoff();
  void RetryStartReceivingMessages();
  void StartReceivingMessagesInternal();
  void StopReceivingMessagesInternal();

  void BeginStreamTimers();
  void OnPongTimeout();

  StreamOpener stream_opener_;
  MessageCallback on_incoming_msg_;
  std::unique_ptr<ScopedGrpcServerStream> receive_messages_stream_;
  std::list<base::OnceClosure> stream_ready_callbacks_;
  std::list<DoneCallback> stream_closed_callbacks_;
  State state_ = State::STOPPED;
  net::BackoffEntry reconnect_retry_backoff_;
  base::OneShotTimer reconnect_retry_timer_;
  std::unique_ptr<base::DelayTimer> stream_pong_timer_;

  base::WeakPtrFactory<FtlMessageReceptionChannel> weak_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(FtlMessageReceptionChannel);
};

}  // namespace remoting

#endif  // REMOTING_SIGNALING_FTL_MESSAGE_RECEPTION_CHANNEL_H_
