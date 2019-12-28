// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_MAC_PERMISSION_UTILS_H_
#define REMOTING_HOST_MAC_PERMISSION_UTILS_H_

#include "base/memory/scoped_refptr.h"

namespace base {
class SingleThreadTaskRunner;
}

namespace remoting {
namespace mac {

// Prompts the user to add the current application to the set of trusted
// Accessibility and Screen Recording applications.  The Accessibility
// permission is required for input injection (10.14 and later) and Screen
// Recording is required for screen capture (10.15 and later).  |task_runner|
// is used to run the dialog message loop.
void PromptUserToChangeTrustStateIfNeeded(
    scoped_refptr<base::SingleThreadTaskRunner> task_runner);

}  // namespace mac
}  // namespace remoting

#endif  // REMOTING_HOST_MAC_PERMISSION_UTILS_H_
