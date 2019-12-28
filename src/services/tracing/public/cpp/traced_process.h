// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_TRACING_PUBLIC_CPP_TRACED_PROCESS_H_
#define SERVICES_TRACING_PUBLIC_CPP_TRACED_PROCESS_H_

#include "services/tracing/public/mojom/traced_process.mojom.h"

namespace tracing {

class TracedProcess {
 public:
  static void OnTracedProcessRequest(
      mojo::PendingReceiver<mojom::TracedProcess> receiver);
};

}  // namespace tracing

#endif  // SERVICES_TRACING_PUBLIC_CPP_TRACED_PROCESS_H_
