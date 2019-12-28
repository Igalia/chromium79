// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_REMOTE_COCOA_APP_SHIM_NS_VIEW_IDS_H_
#define COMPONENTS_REMOTE_COCOA_APP_SHIM_NS_VIEW_IDS_H_

#include <stdint.h>

#include "base/macros.h"
#include "components/remote_cocoa/app_shim/remote_cocoa_app_shim_export.h"

@class NSView;

namespace remote_cocoa {

// Return an NSView given its id. This is to be called in an app shim process.
NSView* REMOTE_COCOA_APP_SHIM_EXPORT GetNSViewFromId(uint64_t ns_view_id);

// A scoped mapping from |ns_view_id| to |view|. While this object exists,
// GetNSViewFromId will return |view| when queried with |ns_view_id|. This
// is to be instantiated in the app shim process.
class REMOTE_COCOA_APP_SHIM_EXPORT ScopedNSViewIdMapping {
 public:
  ScopedNSViewIdMapping(uint64_t ns_view_id, NSView* view);
  ~ScopedNSViewIdMapping();

 private:
  const uint64_t ns_view_id_;
  DISALLOW_COPY_AND_ASSIGN(ScopedNSViewIdMapping);
};

}  // namespace remote_cocoa

#endif  // COMPONENTS_REMOTE_COCOA_APP_SHIM_NS_VIEW_IDS_H_
