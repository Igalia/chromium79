// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_VIDEO_CAPTURE_VIDEO_SOURCE_PROVIDER_IMPL_H_
#define SERVICES_VIDEO_CAPTURE_VIDEO_SOURCE_PROVIDER_IMPL_H_

#include <map>

#include "mojo/public/cpp/bindings/binding_set.h"
#include "services/video_capture/device_factory.h"
#include "services/video_capture/public/mojom/video_source_provider.mojom.h"

namespace video_capture {

class VideoSourceImpl;

class VideoSourceProviderImpl : public mojom::VideoSourceProvider {
 public:
  VideoSourceProviderImpl(
      DeviceFactory* device_factory,
      base::RepeatingClosure on_last_client_disconnected_cb);
  ~VideoSourceProviderImpl() override;

  void AddClient(mojom::VideoSourceProviderRequest request);

  // mojom::VideoSourceProvider implementation.
  void GetSourceInfos(GetSourceInfosCallback callback) override;
  void GetVideoSource(const std::string& device_id,
                      mojom::VideoSourceRequest source_request) override;
  void AddSharedMemoryVirtualDevice(
      const media::VideoCaptureDeviceInfo& device_info,
      mojom::ProducerPtr producer,
      bool send_buffer_handles_to_producer_as_raw_file_descriptors,
      mojom::SharedMemoryVirtualDeviceRequest virtual_device) override;
  void AddTextureVirtualDevice(
      const media::VideoCaptureDeviceInfo& device_info,
      mojom::TextureVirtualDeviceRequest virtual_device) override;
  void RegisterVirtualDevicesChangedObserver(
      mojom::DevicesChangedObserverPtr observer,
      bool raise_event_if_virtual_devices_already_present) override;
  void Close(CloseCallback callback) override;

 private:
  void OnClientDisconnected();
  void OnClientDisconnectedOrClosed();
  void OnVideoSourceLastClientDisconnected(const std::string& device_id);

  DeviceFactory* const device_factory_;
  base::RepeatingClosure on_last_client_disconnected_cb_;
  int client_count_ = 0;
  int closed_but_not_yet_disconnected_client_count_ = 0;
  mojo::BindingSet<mojom::VideoSourceProvider> bindings_;
  std::map<std::string, std::unique_ptr<VideoSourceImpl>> sources_;
  DISALLOW_COPY_AND_ASSIGN(VideoSourceProviderImpl);
};

}  // namespace video_capture

#endif  // SERVICES_VIDEO_CAPTURE_VIDEO_SOURCE_PROVIDER_IMPL_H_
