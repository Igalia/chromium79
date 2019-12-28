// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/protocol/fake_connection_to_client.h"

#include <utility>

#include "remoting/codec/video_encoder.h"
#include "remoting/protocol/audio_source.h"
#include "remoting/protocol/audio_stream.h"
#include "remoting/protocol/session.h"
#include "remoting/protocol/video_frame_pump.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_capturer.h"

namespace remoting {
namespace protocol {

FakeVideoStream::FakeVideoStream() {}
FakeVideoStream::~FakeVideoStream() = default;

void FakeVideoStream::SetEventTimestampsSource(
    scoped_refptr<InputEventTimestampsSource> event_timestamps_source) {}

void FakeVideoStream::Pause(bool pause) {}

void FakeVideoStream::SetLosslessEncode(bool want_lossless) {}

void FakeVideoStream::SetLosslessColor(bool want_lossless) {}

void FakeVideoStream::SetObserver(Observer* observer) {
  observer_ = observer;
}

void FakeVideoStream::SelectSource(int id) {}

base::WeakPtr<FakeVideoStream> FakeVideoStream::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

FakeConnectionToClient::FakeConnectionToClient(std::unique_ptr<Session> session)
    : session_(std::move(session)) {}

FakeConnectionToClient::~FakeConnectionToClient() = default;

void FakeConnectionToClient::SetEventHandler(EventHandler* event_handler) {
  event_handler_ = event_handler;
}

std::unique_ptr<VideoStream> FakeConnectionToClient::StartVideoStream(
    std::unique_ptr<webrtc::DesktopCapturer> desktop_capturer) {
  if (video_stub_ && video_encode_task_runner_) {
    std::unique_ptr<VideoEncoder> video_encoder =
        VideoEncoder::Create(session_->config());

    std::unique_ptr<protocol::VideoFramePump> pump(
        new protocol::VideoFramePump(video_encode_task_runner_,
                                     std::move(desktop_capturer),
                                     std::move(video_encoder),
                                     video_stub_));
    video_feedback_stub_ = pump->video_feedback_stub();
    return std::move(pump);
  }

  std::unique_ptr<FakeVideoStream> result(new FakeVideoStream());
  last_video_stream_ = result->GetWeakPtr();
  return std::move(result);
}

std::unique_ptr<AudioStream> FakeConnectionToClient::StartAudioStream(
    std::unique_ptr<AudioSource> audio_source) {
  NOTIMPLEMENTED();
  return nullptr;
}

ClientStub* FakeConnectionToClient::client_stub() {
  return client_stub_;
}

void FakeConnectionToClient::Disconnect(ErrorCode disconnect_error) {
  CHECK(is_connected_);

  is_connected_ = false;
  disconnect_error_ = disconnect_error;
  if (event_handler_)
    event_handler_->OnConnectionClosed(disconnect_error_);
}

Session* FakeConnectionToClient::session() {
  return session_.get();
}

void FakeConnectionToClient::set_clipboard_stub(ClipboardStub* clipboard_stub) {
  clipboard_stub_ = clipboard_stub;
}

void FakeConnectionToClient::set_host_stub(HostStub* host_stub) {
  host_stub_ = host_stub;
}

void FakeConnectionToClient::set_input_stub(InputStub* input_stub) {
  input_stub_ = input_stub;
}

}  // namespace protocol
}  // namespace remoting
