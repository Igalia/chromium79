// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_GAMEPAD_GAMEPAD_SERVICE_H_
#define DEVICE_GAMEPAD_GAMEPAD_SERVICE_H_

#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

#include "base/bind.h"
#include "base/callback_forward.h"
#include "base/macros.h"
#include "base/memory/shared_memory.h"
#include "base/memory/singleton.h"
#include "device/gamepad/gamepad_export.h"
#include "device/gamepad/gamepad_provider.h"
#include "device/gamepad/public/mojom/gamepad.mojom.h"

namespace base {
class SingleThreadTaskRunner;
}  // namespace base

namespace service_manager {
class Connector;
}  // namespace service_manager

namespace device {
class GamepadConsumer;
class GamepadDataFetcher;
class GamepadProvider;

// Owns the GamepadProvider (the background polling thread) and keeps track of
// the number of consumers currently using the data (and pausing the provider
// when not in use).
class DEVICE_GAMEPAD_EXPORT GamepadService
    : public device::GamepadConnectionChangeClient {
 public:
  // Returns the GamepadService singleton.
  static GamepadService* GetInstance();

  // Sets the GamepadService instance. Exposed for tests.
  static void SetInstance(GamepadService*);

  // Initializes the GamepadService. |service_manager_connector| will be
  // passed to the GamepadProvider once it is created, to allow data fetchers
  // to access the device service from the polling thread.
  void StartUp(
      std::unique_ptr<service_manager::Connector> service_manager_connector);

  // Increments the number of users of the provider. The Provider is running
  // when there's > 0 users, and is paused when the count drops to 0.
  // |consumer| is registered to listen for gamepad connections. If this is the
  // first time it is added to the set of consumers it will be treated
  // specially: it will not be informed about connections before a new user
  // gesture is observed at which point it will be notified for every connected
  // gamepads.
  //
  // Returns true on success. If |consumer| is already active, returns false and
  // exits without modifying the consumer set.
  //
  // Must be called on the I/O thread.
  bool ConsumerBecameActive(GamepadConsumer* consumer);

  // Decrements the number of users of the provider. |consumer| will not be
  // informed about connections until it's added back via ConsumerBecameActive.
  //
  // Returns true on success. If |consumer| is not in the consumer set or is
  // already inactive, returns false and exits without modifying the consumer
  // set.
  //
  // Must be called on the I/O thread.
  bool ConsumerBecameInactive(GamepadConsumer* consumer);

  // Decrements the number of users of the provider and removes |consumer| from
  // the set of consumers. Should be matched with a a ConsumerBecameActive
  // call.
  //
  // Returns true on success, or false if |consumer| was not in the consumer
  // set.
  //
  // Must be called on the I/O thread.
  bool RemoveConsumer(GamepadConsumer* consumer);

  // Registers the given closure for calling when the user has interacted with
  // the device. This callback will only be issued once. Should only be called
  // while a consumer is active.
  void RegisterForUserGesture(const base::Closure& closure);

  // Returns a duplicate of the shared memory region of the gamepad data.
  base::ReadOnlySharedMemoryRegion DuplicateSharedMemoryRegion();

  // Stop/join with the background thread in GamepadProvider |provider_|.
  void Terminate();

  // Called on IO thread when a gamepad is connected.
  void OnGamepadConnected(uint32_t index, const Gamepad& pad);

  // Called on IO thread when a gamepad is disconnected.
  void OnGamepadDisconnected(uint32_t index, const Gamepad& pad);

  // Request playback of a haptic effect on the specified gamepad. Once effect
  // playback is complete or is preempted by a different effect, the callback
  // will be called.
  void PlayVibrationEffectOnce(
      uint32_t pad_index,
      mojom::GamepadHapticEffectType,
      mojom::GamepadEffectParametersPtr,
      mojom::GamepadHapticsManager::PlayVibrationEffectOnceCallback);

  // Resets the state of the vibration actuator on the specified gamepad. If any
  // effects are currently being played, they are preempted and vibration is
  // stopped.
  void ResetVibrationActuator(
      uint32_t pad_index,
      mojom::GamepadHapticsManager::ResetVibrationActuatorCallback);

  // Constructor for testing. This specifies the data fetcher to use for a
  // provider, bypassing the default platform one.
  GamepadService(std::unique_ptr<GamepadDataFetcher> fetcher);

  virtual ~GamepadService();

 private:
  friend struct base::DefaultSingletonTraits<GamepadService>;
  friend class GamepadServiceTest;

  GamepadService();

  void OnUserGesture();

  void OnGamepadConnectionChange(bool connected,
                                 uint32_t index,
                                 const Gamepad& pad) override;

  void SetSanitizationEnabled(bool sanitize);

  struct ConsumerInfo {
    ConsumerInfo(GamepadConsumer* consumer) : consumer(consumer) {}

    bool operator<(const ConsumerInfo& other) const {
      return consumer < other.consumer;
    }

    device::GamepadConsumer* consumer;
    mutable bool is_active = false;
    mutable bool did_observe_user_gesture = false;
  };

  std::unique_ptr<GamepadProvider> provider_;

  scoped_refptr<base::SingleThreadTaskRunner> main_thread_task_runner_;

  typedef std::set<ConsumerInfo> ConsumerSet;
  ConsumerSet consumers_;

  typedef std::unordered_map<device::GamepadConsumer*, std::vector<bool>>
      ConsumerConnectedStateMap;

  ConsumerConnectedStateMap inactive_consumer_state_;

  // The number of active consumers in |consumers_|.
  int num_active_consumers_ = 0;

  bool gesture_callback_pending_ = false;

  // Service manager connector. Must be used only on the main thread.
  std::unique_ptr<service_manager::Connector> service_manager_connector_;

  DISALLOW_COPY_AND_ASSIGN(GamepadService);
};

}  // namespace device

#endif  // DEVICE_GAMEPAD_GAMEPAD_SERVICE_H_
