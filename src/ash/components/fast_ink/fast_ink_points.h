// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_COMPONENTS_FAST_INK_FAST_INK_POINTS_H_
#define ASH_COMPONENTS_FAST_INK_FAST_INK_POINTS_H_

#include <memory>

#include "base/containers/circular_deque.h"
#include "base/macros.h"
#include "base/time/time.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"

namespace fast_ink {

// FastInkPoints is a helper class used for displaying low-latency palette
// tools. It contains a collection of points representing one or more
// contiguous trajectory segments.
class FastInkPoints {
 public:
  // Struct to describe each point.
  struct FastInkPoint {
    gfx::PointF location;
    base::TimeTicks time;
    bool gap_after = false;  // True when there is a gap after this point.
  };

  // Constructor with a parameter to choose the fade out time of the points in
  // the collection. Zero means no fadeout.
  explicit FastInkPoints(base::TimeDelta life_duration);
  ~FastInkPoints();

  // Adds a point.
  void AddPoint(const gfx::PointF& point, const base::TimeTicks& time);
  // Adds a gap after the most recent point. This is useful for multi-stroke
  // gesture handling (e.g. strokes going over the bezel).
  void AddGap();
  // Updates the collection latest time. Automatically clears points that are
  // too old.
  void MoveForwardToTime(const base::TimeTicks& latest_time);
  // Removes all points.
  void Clear();
  // Gets the bounding box of the points, int coordinates.
  gfx::Rect GetBoundingBox() const;
  // Gets the bounding box of the points, float coordinates.
  gfx::RectF GetBoundingBoxF() const;
  // Returns the oldest point in the collection.
  FastInkPoint GetOldest() const;
  // Returns the newest point in the collection.
  FastInkPoint GetNewest() const;
  // Returns the number of points in the collection.
  int GetNumberOfPoints() const;
  // Whether there are any points or not.
  bool IsEmpty() const;
  // Expose the collection so callers can work with the points.
  const base::circular_deque<FastInkPoint>& points() const;
  // Returns the fadeout factor for a point. This is a value between 0.0 and
  // 1.0, where 0.0 corresponds to a recently  added point, and 1.0 to a point
  // that is about to expire. Do not call this method if |life_duration_| is 0.
  float GetFadeoutFactor(int index) const;
  // Fills the container with predicted points based on |real_points|.
  void Predict(const FastInkPoints& real_points,
               const base::TimeTicks& current_time,
               base::TimeDelta prediction_duration,
               const gfx::Size& screen_size);

 private:
  const base::TimeDelta life_duration_;
  base::circular_deque<FastInkPoint> points_;
  // The latest time of the collection of points. This gets updated when new
  // points are added or when MoveForwardToTime is called.
  base::TimeTicks collection_latest_time_;

  DISALLOW_COPY_AND_ASSIGN(FastInkPoints);
};

}  // namespace fast_ink

#endif  // ASH_COMPONENTS_FAST_INK_FAST_INK_POINTS_H_
