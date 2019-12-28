// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/latency/latency_info.h"

namespace ui {

void PrintTo(const LatencyInfo& latency, ::std::ostream* os) {
  *os << "LatencyInfo: trace_id=" << latency.trace_id();
}

}  // namespace ui
