// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_SETTINGS_DEVICE_SETTINGS_SERVICE_H_
#define CHROME_BROWSER_CHROMEOS_SETTINGS_DEVICE_SETTINGS_SERVICE_H_

#include <memory>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/containers/circular_deque.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/observer_list.h"
#include "chromeos/dbus/session_manager/session_manager_client.h"
#include "components/ownership/owner_settings_service.h"
#include "components/policy/core/common/cloud/cloud_policy_constants.h"
#include "components/policy/core/common/cloud/cloud_policy_validator.h"
#include "components/policy/proto/chrome_device_policy.pb.h"
#include "components/policy/proto/device_management_backend.pb.h"
#include "crypto/scoped_nss_types.h"

namespace ownership {
class OwnerKeyUtil;
class PublicKey;
}

namespace policy {
namespace off_hours {
class DeviceOffHoursController;
}  // namespace off_hours
}  // namespace policy

namespace chromeos {

class SessionManagerOperation;

// Deals with the low-level interface to Chrome OS device settings. Device
// settings are stored in a protobuf that's protected by a cryptographic
// signature generated by a key in the device owner's possession. Key and
// settings are brokered by the session_manager daemon.
//
// The purpose of DeviceSettingsService is to keep track of the current key and
// settings blob. For reading and writing device settings, use CrosSettings
// instead, which provides a high-level interface that allows for manipulation
// of individual settings.
//
// DeviceSettingsService generates notifications for key and policy update
// events so interested parties can reload state as appropriate.
class DeviceSettingsService : public SessionManagerClient::Observer {
 public:
  // Indicates ownership status of the device (listed in upgrade order).
  enum OwnershipStatus {
    OWNERSHIP_UNKNOWN = 0,
    // Not yet owned.
    OWNERSHIP_NONE,
    // Either consumer ownership, cloud management or Active Directory
    // management.
    OWNERSHIP_TAKEN
  };

  typedef base::Callback<void(OwnershipStatus)> OwnershipStatusCallback;

  // Status codes for Load() and Store().
  enum Status {
    STORE_SUCCESS,
    STORE_KEY_UNAVAILABLE,   // Owner key not yet configured.
    STORE_OPERATION_FAILED,  // IPC to session_manager daemon failed.
    STORE_NO_POLICY,         // No settings blob present.
    STORE_INVALID_POLICY,    // Invalid settings blob (proto parse failed).
    STORE_VALIDATION_ERROR,  // Policy validation failure.
  };

  // Observer interface.
  class Observer {
   public:
    virtual ~Observer();

    // Indicates device ownership status changes.  This is triggered upon every
    // browser start since the transition from uninitialized (OWNERSHIP_UNKNOWN)
    // to initialized (either of OWNERSHIP_{NONE,TAKEN}) also counts as an
    // ownership change.
    virtual void OwnershipStatusChanged();

    // Gets called after updates to the device settings.
    virtual void DeviceSettingsUpdated();

    virtual void OnDeviceSettingsServiceShutdown();
  };

  // Manage singleton instance.
  static void Initialize();
  static bool IsInitialized();
  static void Shutdown();
  static DeviceSettingsService* Get();

  // Returns a human-readable string describing |status|.
  static const char* StatusToString(Status status);

  // Creates a device settings service instance. This is meant for unit tests,
  // production code uses the singleton returned by Get() above.
  DeviceSettingsService();
  ~DeviceSettingsService() override;

  // To be called on startup once threads are initialized and D-Bus is ready.
  void SetSessionManager(SessionManagerClient* session_manager_client,
                         scoped_refptr<ownership::OwnerKeyUtil> owner_key_util);

  // Prevents the service from making further calls to session_manager_client
  // and stops any pending operations.
  void UnsetSessionManager();

  // Must only be used with a |device_mode| that has been read and verified by
  // the InstallAttributes class.
  void SetDeviceMode(policy::DeviceMode device_mode);

  const enterprise_management::PolicyData* policy_data() const {
    return policy_data_.get();
  }

  // Returns the currently active device settings. Returns nullptr if the device
  // settings have not been retrieved from session_manager yet.
  const enterprise_management::ChromeDeviceSettingsProto*
      device_settings() const {
    return device_settings_.get();
  }

  // Returns the currently used owner key.
  scoped_refptr<ownership::PublicKey> GetPublicKey();

  // Returns the status generated by the *last operation*.
  // WARNING: It is not correct to take this method as an indication of whether
  // DeviceSettingsService contains valid device settings. In order to answer
  // that question, simply check whether device_settings() is different from
  // nullptr.
  Status status() const { return store_status_; }

  // Returns the currently device off hours controller. The returned pointer is
  // guaranteed to be non-null.
  policy::off_hours::DeviceOffHoursController* device_off_hours_controller()
      const {
    return device_off_hours_controller_.get();
  }

  void SetDeviceOffHoursControllerForTesting(
      std::unique_ptr<policy::off_hours::DeviceOffHoursController> controller);

  // Triggers an attempt to pull the public half of the owner key from disk and
  // load the device settings.
  void Load();

  // Synchronously pulls the public key and loads the device settings.
  void LoadImmediately();

  // Stores a policy blob to session_manager. The result of the operation is
  // reported through |callback|. If successful, the updated device settings are
  // present in policy_data() and device_settings() when the callback runs.
  void Store(std::unique_ptr<enterprise_management::PolicyFetchResponse> policy,
             const base::Closure& callback);

  // Returns the ownership status. May return OWNERSHIP_UNKNOWN if the disk
  // hasn't been checked yet.
  OwnershipStatus GetOwnershipStatus();

  // Determines the ownership status and reports the result to |callback|. This
  // is guaranteed to never return OWNERSHIP_UNKNOWN.
  void GetOwnershipStatusAsync(const OwnershipStatusCallback& callback);

  // Checks whether we have the private owner key.
  //
  // DEPRECATED (ygorshenin@, crbug.com/433840): this method should
  // not be used since private key is a profile-specific resource and
  // should be checked and used in a profile-aware manner, through
  // OwnerSettingsService.
  bool HasPrivateOwnerKey();

  // Sets the identity of the user that's interacting with the service. This is
  // relevant only for writing settings through SignAndStore().
  //
  // TODO (ygorshenin@, crbug.com/433840): get rid of the method when
  // write path for device settings will be removed from
  // DeviceSettingsProvider and all existing clients will be switched
  // to OwnerSettingsServiceChromeOS.
  void InitOwner(const std::string& username,
                 const base::WeakPtr<ownership::OwnerSettingsService>&
                     owner_settings_service);

  const std::string& GetUsername() const;

  ownership::OwnerSettingsService* GetOwnerSettingsService() const;

  // Mark that the device will establish consumer ownership. If the flag is set
  // and ownership is not taken, policy reload will be deferred until InitOwner
  // is called. So that the ownership status is flipped after the private part
  // of owner is fully loaded.
  void MarkWillEstablishConsumerOwnership();

  // Adds an observer.
  void AddObserver(Observer* observer);
  // Removes an observer.
  void RemoveObserver(Observer* observer);

  // SessionManagerClient::Observer:
  void OwnerKeySet(bool success) override;
  void PropertyChangeComplete(bool success) override;

 private:
  friend class OwnerSettingsServiceChromeOS;

  // Enqueues a new operation. Takes ownership of |operation| and starts it
  // right away if there is no active operation currently.
  void Enqueue(std::unique_ptr<SessionManagerOperation> operation);

  // Enqueues a load operation.
  void EnqueueLoad(bool request_key_load);

  // Makes sure there's a reload operation so changes to the settings (and key,
  // in case |request_key_load| is set) are getting picked up.
  void EnsureReload(bool request_key_load);

  // Runs the next pending operation.
  void StartNextOperation();

  // Updates status, policy data and owner key from a finished operation.
  void HandleCompletedOperation(const base::Closure& callback,
                                SessionManagerOperation* operation,
                                Status status);

  // Same as HandleCompletedOperation(), but also starts the next pending
  // operation if available.
  void HandleCompletedAsyncOperation(const base::Closure& callback,
                                     SessionManagerOperation* operation,
                                     Status status);

  // Run OwnershipStatusChanged() for observers.
  void NotifyOwnershipStatusChanged() const;

  // Run DeviceSettingsUpdated() for observers.
  void NotifyDeviceSettingsUpdated() const;

  // Processes pending callbacks from GetOwnershipStatusAsync().
  void RunPendingOwnershipStatusCallbacks();

  SessionManagerClient* session_manager_client_ = nullptr;
  scoped_refptr<ownership::OwnerKeyUtil> owner_key_util_;

  Status store_status_ = STORE_SUCCESS;

  std::vector<OwnershipStatusCallback> pending_ownership_status_callbacks_;

  std::string username_;
  scoped_refptr<ownership::PublicKey> public_key_;
  base::WeakPtr<ownership::OwnerSettingsService> owner_settings_service_;
  // Ownership status before the current session manager operation.
  OwnershipStatus previous_ownership_status_ = OWNERSHIP_UNKNOWN;

  std::unique_ptr<enterprise_management::PolicyData> policy_data_;
  std::unique_ptr<enterprise_management::ChromeDeviceSettingsProto>
      device_settings_;

  policy::DeviceMode device_mode_ = policy::DEVICE_MODE_PENDING;

  // The queue of pending operations. The first operation on the queue is
  // currently active; it gets removed and destroyed once it completes.
  base::circular_deque<std::unique_ptr<SessionManagerOperation>>
      pending_operations_;

  base::ObserverList<Observer>::Unchecked observers_;

  // Whether the device will be establishing consumer ownership.
  bool will_establish_consumer_ownership_ = false;

  std::unique_ptr<policy::off_hours::DeviceOffHoursController>
      device_off_hours_controller_;

  base::WeakPtrFactory<DeviceSettingsService> weak_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(DeviceSettingsService);
};

// Helper class for tests. Initializes the DeviceSettingsService singleton on
// construction and tears it down again on destruction.
class ScopedTestDeviceSettingsService {
 public:
  ScopedTestDeviceSettingsService();
  ~ScopedTestDeviceSettingsService();

 private:
  DISALLOW_COPY_AND_ASSIGN(ScopedTestDeviceSettingsService);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_SETTINGS_DEVICE_SETTINGS_SERVICE_H_
