// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WM_MRU_WINDOW_TRACKER_H_
#define ASH_WM_MRU_WINDOW_TRACKER_H_

#include <vector>

#include "ash/ash_export.h"
#include "base/macros.h"
#include "base/observer_list.h"
#include "ui/aura/window_observer.h"
#include "ui/wm/public/activation_change_observer.h"

namespace ash {

enum DesksMruType {
  // The MRU window list will include windows from all active and inactive
  // desks.
  kAllDesks,

  // The MRU window list will exclude windows from the inactive desks.
  kActiveDesk,
};

// A predicate that determines whether |window| can be included in the MRU
// window list.
bool CanIncludeWindowInMruList(aura::Window* window);

// Maintains a most recently used list of windows. This is used for window
// cycling using Alt+Tab and overview mode.
class ASH_EXPORT MruWindowTracker : public ::wm::ActivationChangeObserver,
                                    public aura::WindowObserver {
 public:
  using WindowList = std::vector<aura::Window*>;

  class Observer : public base::CheckedObserver {
   public:
    // Invoked when a tracked window is destroyed,
    virtual void OnWindowUntracked(aura::Window* untracked_window) {}
  };

  MruWindowTracker();
  ~MruWindowTracker() override;

  // Returns the set of windows which can be cycled through using the tracked
  // list of most recently used windows.
  // |desks_mru_type| determines whether to include or exclude windows from the
  // inactive desks.
  WindowList BuildMruWindowList(DesksMruType desks_mru_type) const;

  // This does the same thing as the above, but ignores the system modal dialog
  // state and hence the returned list could contain more windows if a system
  // modal dialog window is present.
  // |desks_mru_type| determines whether to include or exclude windows from the
  // inactive desks.
  WindowList BuildWindowListIgnoreModal(DesksMruType desks_mru_type) const;

  // This does the same thing as |BuildMruWindowList()| but with some
  // exclusions. This list is used for cycling through by the keyboard via
  // alt-tab.
  // |desks_mru_type| determines whether to include or exclude windows from the
  // inactive desks.
  WindowList BuildWindowForCycleList(DesksMruType desks_mru_type) const;

  // This does the same thing as |BuildWindowForCycleList()| but includes
  // ARC PIP windows if they exist. Entering PIP for Android can consume the
  // window (in contrast to Chrome PIP, which creates a new window). To support
  // the same interaction as Chrome PIP auto-pip, include the Android PIP window
  // in alt-tab. This will let alt tabbing back to the 'original window' restore
  // that window from PIP, which matches behaviour for Chrome PIP, where
  // alt-tabbing back to the original Chrome tab or app ends auto-PIP.
  WindowList BuildWindowForCycleWithPipList(DesksMruType desks_mru_type) const;

  // Starts or stops ignoring window activations. If no longer ignoring
  // activations the currently active window is moved to the front of the
  // MRU window list. Used by WindowCycleList to avoid adding all cycled
  // windows to the front of the MRU window list.
  void SetIgnoreActivations(bool ignore);

  // Add/Remove observers.
  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

 private:
  // Updates the mru_windows_ list to insert/move |active_window| at/to the
  // front.
  void SetActiveWindow(aura::Window* active_window);

  // Overridden from wm::ActivationChangeObserver:
  void OnWindowActivated(ActivationReason reason,
                         aura::Window* gained_active,
                         aura::Window* lost_active) override;

  // Overridden from aura::WindowObserver:
  void OnWindowDestroyed(aura::Window* window) override;

  // List of windows that have been activated in containers that we cycle
  // through, sorted such that the most recently used window comes last.
  std::vector<aura::Window*> mru_windows_;

  base::ObserverList<Observer, true> observers_;

  bool ignore_window_activations_ = false;

  DISALLOW_COPY_AND_ASSIGN(MruWindowTracker);
};

}  // namespace ash

#endif  // ASH_WM_MRU_WINDOW_TRACKER_H_
