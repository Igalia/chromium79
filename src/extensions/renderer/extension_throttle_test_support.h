// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_RENDERER_EXTENSION_THROTTLE_TEST_SUPPORT_H_
#define EXTENSIONS_RENDERER_EXTENSION_THROTTLE_TEST_SUPPORT_H_

#include <string>

#include "base/macros.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "net/base/backoff_entry.h"

namespace extensions {

class TestTickClock : public base::TickClock {
 public:
  TestTickClock();
  explicit TestTickClock(base::TimeTicks now);
  ~TestTickClock() override;

  base::TimeTicks NowTicks() const override;
  void set_now(base::TimeTicks now) { now_ticks_ = now; }

 private:
  base::TimeTicks now_ticks_;
  DISALLOW_COPY_AND_ASSIGN(TestTickClock);
};

}  // namespace extensions

#endif  // EXTENSIONS_RENDERER_EXTENSION_THROTTLE_TEST_SUPPORT_H_
