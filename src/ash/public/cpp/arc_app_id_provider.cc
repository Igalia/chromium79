// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/public/cpp/arc_app_id_provider.h"

#include "base/logging.h"

namespace ash {

namespace {
ArcAppIdProvider* g_instance = nullptr;
}

// static
ArcAppIdProvider* ArcAppIdProvider::Get() {
  return g_instance;
}

ArcAppIdProvider::ArcAppIdProvider() {
  DCHECK_EQ(nullptr, g_instance);
  g_instance = this;
}

ArcAppIdProvider::~ArcAppIdProvider() {
  DCHECK_EQ(this, g_instance);
  g_instance = nullptr;
}

}  // namespace ash
