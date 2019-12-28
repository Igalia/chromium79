// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_APP_CHROME_WATCHER_CLIENT_WIN_H_
#define CHROME_APP_CHROME_WATCHER_CLIENT_WIN_H_

#include <windows.h>

#include "base/callback.h"
#include "base/command_line.h"
#include "base/macros.h"
#include "base/process/process.h"
#include "base/time/time.h"
#include "base/win/scoped_handle.h"

// Launches a Chrome watcher process and permits the client to wait until the
// process is fully initialized.
class ChromeWatcherClient {
 public:
  // A CommandLineGenerator generates command lines that will launch a separate
  // process and pass the supplied values to WatcherMain in that process.
  // |parent_process| is the process that the watcher process should watch;
  // |main_thread_id| is the parent process' main thread ID.
  // |on_initialized_event| should be signaled when the watcher process is fully
  // initialized. The process will be launched such that the HANDLEs are
  // inherited by the new process.
  typedef base::Callback<base::CommandLine(HANDLE parent_process,
                                           DWORD main_thread_id,
                                           HANDLE on_initialized_event)>
      CommandLineGenerator;

  // Constructs an instance that launches its watcher process using the command
  // line generated by |command_line_generator|.
  explicit ChromeWatcherClient(
      const CommandLineGenerator& command_line_generator);

  ~ChromeWatcherClient();

  // Launches the watcher process such that the child process is able to inherit
  // a handle to the current process. Returns true if the process is
  // successfully launched.
  bool LaunchWatcher();

  // Blocks until the process, previously launched by LaunchWatcher, is either
  // fully initialized or has terminated. Returns true if the process
  // successfully initializes. May be called multiple times.
  bool EnsureInitialized();

  // Waits for the process to exit. Returns true on success. It is up to the
  // client to somehow signal the process to exit.
  bool WaitForExit(int* exit_code);

  // Same as WaitForExit() but only waits for up to |timeout|.
  bool WaitForExitWithTimeout(base::TimeDelta timeout, int* exit_code);

 private:
  CommandLineGenerator command_line_generator_;
  base::win::ScopedHandle on_initialized_event_;
  base::Process process_;

  DISALLOW_COPY_AND_ASSIGN(ChromeWatcherClient);
};

#endif  // CHROME_APP_CHROME_WATCHER_CLIENT_WIN_H_
