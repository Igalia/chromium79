// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/tracing/public/cpp/perfetto/java_heap_profiler/java_heap_profiler_android.h"

#include "base/android/java_heap_dump_generator.h"
#include "base/files/scoped_temp_dir.h"
#include "services/tracing/public/cpp/perfetto/perfetto_traced_process.h"

namespace tracing {

JavaHeapProfiler::JavaHeapProfiler()
    : DataSourceBase(mojom::kJavaHeapProfilerSourceName) {}

// static
JavaHeapProfiler* JavaHeapProfiler::GetInstance() {
  static base::NoDestructor<JavaHeapProfiler> instance;
  return instance.get();
}

void JavaHeapProfiler::StartTracing(
    PerfettoProducer* producer,
    const perfetto::DataSourceConfig& data_source_config) {
  base::ScopedTempDir temp_dir;
  if (!temp_dir.CreateUniqueTempDir()) {
    VLOG(0) << "Failed to create unique temporary directory.";
    return;
  }
  std::string file_path = temp_dir.GetPath().Append("temp_hprof.hprof").value();

  base::android::WriteJavaHeapDumpToPath(file_path);

  // TODO(zhanggeorge): Convert heap dump and write to trace.
}

void JavaHeapProfiler::StopTracing(base::OnceClosure stop_complete_callback) {
  producer_ = nullptr;
  std::move(stop_complete_callback).Run();
}

void JavaHeapProfiler::Flush(base::RepeatingClosure flush_complete_callback) {
  flush_complete_callback.Run();
}
}  // namespace tracing
