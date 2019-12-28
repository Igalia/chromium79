// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_ARC_METRICS_STABILITY_METRICS_MANAGER_H_
#define COMPONENTS_ARC_METRICS_STABILITY_METRICS_MANAGER_H_

#include "base/optional.h"
#include "base/sequence_checker.h"
#include "components/arc/metrics/arc_metrics_constants.h"

class PrefService;

namespace arc {

// Singleton instance that keeps track of ARC-related stability metrics. In case
// of a crash, stability metrics from previous session are included in initial
// stability log, which is generated early on browser startup when ARC services
// aren't available. This class supports recording stability metrics by
// persisting them into local state.
class StabilityMetricsManager {
 public:
  static void Initialize(PrefService* local_state);
  static void Shutdown();

  // May return null if not initialized, which happens only in unit tests.
  static StabilityMetricsManager* Get();

  // Reads metrics from |local_state_| and record to UMA. Called from
  // ChromeOSMetricsProvider to include stability metrics in all uploaded UMA
  // logs.
  void RecordMetricsToUMA();

  // Resets metrics persisted in |local_state_|. Called from ArcSessionManager
  // which determines whether stability metrics should be recorded for current
  // session,
  void ResetMetrics();

  // Returns current persisted value (if exists) for Arc.State UMA histogram.
  base::Optional<bool> GetArcEnabledState();

  // Sets value for Arc.State UMA histogram.
  void SetArcEnabledState(bool enabled);

  // Returns current persisted value (if exists) for Arc.State UMA histogram.
  base::Optional<NativeBridgeType> GetArcNativeBridgeType();

  // Sets value for Arc.NativeBridgeType UMA histogram.
  void SetArcNativeBridgeType(NativeBridgeType native_bridge_type);

 private:
  explicit StabilityMetricsManager(PrefService* local_state);
  ~StabilityMetricsManager();

  SEQUENCE_CHECKER(sequence_checker_);
  PrefService* const local_state_;

  DISALLOW_COPY_AND_ASSIGN(StabilityMetricsManager);
};

}  // namespace arc

#endif  // COMPONENTS_ARC_METRICS_STABILITY_METRICS_MANAGER_H_
