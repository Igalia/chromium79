// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/paint_preview/renderer/paint_preview_recorder_impl.h"

#include <utility>

#include "base/auto_reset.h"
#include "base/memory/shared_memory.h"
#include "base/task_runner.h"
#include "cc/paint/paint_record.h"
#include "cc/paint/paint_recorder.h"
#include "components/paint_preview/renderer/paint_preview_recorder_utils.h"
#include "content/public/renderer/render_frame.h"
#include "mojo/public/cpp/base/shared_memory_utils.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_registry.h"
#include "third_party/blink/public/web/web_local_frame.h"

namespace paint_preview {

namespace {

mojom::PaintPreviewStatus FinishRecording(
    sk_sp<const cc::PaintRecord> recording,
    const gfx::Rect& bounds,
    PaintPreviewTracker* tracker,
    base::File skp_file,
    base::ReadOnlySharedMemoryRegion* region) {
  ParseGlyphs(recording.get(), tracker);
  if (!SerializeAsSkPicture(recording, tracker, bounds, std::move(skp_file)))
    return mojom::PaintPreviewStatus::kCaptureFailed;

  if (!BuildAndSerializeProto(tracker, region))
    return mojom::PaintPreviewStatus::kProtoSerializationFailed;
  return mojom::PaintPreviewStatus::kOk;
}

}  // namespace

PaintPreviewRecorderImpl::PaintPreviewRecorderImpl(
    content::RenderFrame* render_frame)
    : content::RenderFrameObserver(render_frame),
      is_painting_preview_(false),
      is_main_frame_(render_frame->IsMainFrame()),
      routing_id_(render_frame->GetRoutingID()) {
  render_frame->GetAssociatedInterfaceRegistry()->AddInterface(
      base::BindRepeating(&PaintPreviewRecorderImpl::BindPaintPreviewRecorder,
                          weak_ptr_factory_.GetWeakPtr()));
}

PaintPreviewRecorderImpl::~PaintPreviewRecorderImpl() = default;

void PaintPreviewRecorderImpl::CapturePaintPreview(
    mojom::PaintPreviewCaptureParamsPtr params,
    CapturePaintPreviewCallback callback) {
  mojom::PaintPreviewStatus status = mojom::PaintPreviewStatus::kOk;
  base::ReadOnlySharedMemoryRegion region;
  // This should not be called recursively or multiple times while unfinished
  // (Blink can only run one capture per RenderFrame at a time).
  DCHECK(!is_painting_preview_);
  // DCHECK, but fallback safely as it is difficult to reason about whether this
  // might happen due to it being tied to a RenderFrame rather than
  // RenderWidget and we don't want to crash the renderer as this is
  // recoverable.
  if (is_painting_preview_) {
    status = mojom::PaintPreviewStatus::kAlreadyCapturing;
    std::move(callback).Run(status, std::move(region));
    return;
  }
  base::AutoReset<bool>(&is_painting_preview_, true);

  CapturePaintPreviewInternal(params, &region, &status);
  std::move(callback).Run(status, std::move(region));
}

void PaintPreviewRecorderImpl::OnDestruct() {
  paint_preview_recorder_receiver_.reset();
  base::ThreadTaskRunnerHandle::Get()->DeleteSoon(FROM_HERE, this);
}

void PaintPreviewRecorderImpl::BindPaintPreviewRecorder(
    mojo::PendingAssociatedReceiver<mojom::PaintPreviewRecorder> receiver) {
  paint_preview_recorder_receiver_.Bind(std::move(receiver));
}

void PaintPreviewRecorderImpl::CapturePaintPreviewInternal(
    const mojom::PaintPreviewCaptureParamsPtr& params,
    base::ReadOnlySharedMemoryRegion* region,
    mojom::PaintPreviewStatus* status) {
  blink::WebLocalFrame* frame = render_frame()->GetWebFrame();
  // Warm up paint for an out-of-lifecycle paint phase.
  frame->DispatchBeforePrintEvent();

  DCHECK_EQ(is_main_frame_, params->is_main_frame);
  gfx::Rect bounds;
  if (is_main_frame_ || params->clip_rect == gfx::Rect(0, 0, 0, 0)) {
    auto size = frame->DocumentSize();
    bounds = gfx::Rect(0, 0, size.width, size.height);
  } else {
    bounds = gfx::Rect(params->clip_rect.size());
  }

  cc::PaintRecorder recorder;
  recorder.beginRecording(bounds.width(), bounds.height());
  PaintPreviewTracker tracker(params->guid, routing_id_, is_main_frame_);
  // TODO(crbug/1008885): Create a method on |canvas| to inject |tracker_| to
  // propagate to graphics contexts and inner canvases
  // TODO(crbug/1001109): Create a method on |frame| to execute the capture
  // within Blink.

  // Restore to before out-of-lifecycle paint phase.
  frame->DispatchAfterPrintEvent();

  // TODO(crbug/1011896): Determine if making this async would be beneficial.
  *status = FinishRecording(recorder.finishRecordingAsPicture(), bounds,
                            &tracker, std::move(params->file), region);
}

}  // namespace paint_preview
