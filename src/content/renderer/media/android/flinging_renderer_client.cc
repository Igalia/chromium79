// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/media/android/flinging_renderer_client.h"

#include <utility>

#include "base/bind.h"
#include "base/callback_helpers.h"

namespace content {

FlingingRendererClient::FlingingRendererClient(
    ClientExtentionRequest client_extension_request,
    scoped_refptr<base::SingleThreadTaskRunner> media_task_runner,
    std::unique_ptr<media::MojoRenderer> mojo_renderer,
    media::RemotePlayStateChangeCB remote_play_state_change_cb)
    : MojoRendererWrapper(std::move(mojo_renderer)),
      media_task_runner_(std::move(media_task_runner)),
      remote_play_state_change_cb_(remote_play_state_change_cb),
      delayed_bind_client_extension_request_(
          std::move(client_extension_request)),
      client_extension_binding_(this) {}

FlingingRendererClient::~FlingingRendererClient() = default;

void FlingingRendererClient::Initialize(
    media::MediaResource* media_resource,
    media::RendererClient* client,
    const media::PipelineStatusCB& init_cb) {
  DCHECK(media_task_runner_->BelongsToCurrentThread());

  client_ = client;

  client_extension_binding_.Bind(
      std::move(delayed_bind_client_extension_request_), media_task_runner_);

  MojoRendererWrapper::Initialize(media_resource, client, init_cb);
}

void FlingingRendererClient::OnRemotePlayStateChange(
    media::MediaStatus::State state) {
  DCHECK(media_task_runner_->BelongsToCurrentThread());
  remote_play_state_change_cb_.Run(state);
}

}  // namespace content
