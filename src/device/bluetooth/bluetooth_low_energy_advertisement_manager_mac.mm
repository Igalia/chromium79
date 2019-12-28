// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/bluetooth_low_energy_advertisement_manager_mac.h"

#include "base/bind.h"
#include "base/mac/scoped_nsobject.h"
#include "base/optional.h"
#include "base/strings/sys_string_conversions.h"
#include "device/bluetooth/bluetooth_advertisement.h"

namespace device {

BluetoothLowEnergyAdvertisementManagerMac::
    BluetoothLowEnergyAdvertisementManagerMac() {}

BluetoothLowEnergyAdvertisementManagerMac::
    ~BluetoothLowEnergyAdvertisementManagerMac() {}

void BluetoothLowEnergyAdvertisementManagerMac::Init(
    scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner,
    CBPeripheralManager* peripheral_manager) {
  ui_task_runner_ = ui_task_runner;
  peripheral_manager_ = peripheral_manager;
}

void BluetoothLowEnergyAdvertisementManagerMac::
    OnPeripheralManagerStateChanged() {
  // This function handles several cases:
  //   1. Error out when registering an advertisement, but the adapter is not
  //      powered on.
  //   2. Start advertising a registered advertisement if the adapter is powered
  //      on.
  //   3. Stop the advertisement when the adapter is powered off.
  //      Note that if the adapter is powered off while advertising, macOS will
  //      automatically restart advertising when the adapter is powered back on,
  //      so we need to explicitly stop advertising in this case.

  if (!active_advertisement_) {
    return;
  }

  CBPeripheralManagerState adapter_state = GetPeripheralManagerState();
  if (adapter_state == CBPeripheralManagerStateUnknown) {
    // Wait for the adapter to initialize.
    return;
  }

  if (active_advertisement_->is_waiting_for_adapter() &&
      adapter_state < CBPeripheralManagerStatePoweredOn) {
    DVLOG(1)
        << "Registration failed. Adapter changed to invalid adapter_state.";
    BluetoothAdvertisement::ErrorCode error_code =
        adapter_state == CBPeripheralManagerStatePoweredOff
            ? BluetoothAdvertisement::ERROR_ADAPTER_POWERED_OFF
            : BluetoothAdvertisement::ERROR_UNSUPPORTED_PLATFORM;
    active_advertisement_->OnAdvertisementError(ui_task_runner_.get(),
                                                error_code);
    active_advertisement_ = nullptr;
    return;
  }

  if (active_advertisement_->is_advertising() &&
      adapter_state == CBPeripheralManagerStateResetting) {
    DVLOG(1) << "Adapter resetting. Invalidating advertisement.";
    active_advertisement_->OnAdapterReset();
    active_advertisement_ = nullptr;
    return;
  }

  if (active_advertisement_->is_advertising() &&
      adapter_state == CBPeripheralManagerStatePoweredOff) {
    DVLOG(1) << "Adapter powered off. Stopping advertisement.";
    // Note: we purposefully don't unregister the active advertisement for
    // consistency with ChromeOS. The caller must manually unregister
    // the advertisement themselves.
    [peripheral_manager_ stopAdvertising];
    return;
  }

  if (active_advertisement_->is_waiting_for_adapter()) {
    StartAdvertising();
  }
}

void BluetoothLowEnergyAdvertisementManagerMac::StartAdvertising() {
  base::scoped_nsobject<NSMutableArray> service_uuid_array(
      [[NSMutableArray alloc]
          initWithCapacity:active_advertisement_->service_uuids().size()]);
  for (const std::string& service_uuid :
       active_advertisement_->service_uuids()) {
    NSString* uuid_string = base::SysUTF8ToNSString(service_uuid);
    [service_uuid_array addObject:[CBUUID UUIDWithString:uuid_string]];
  }

  active_advertisement_->OnAdvertisementPending();
  [peripheral_manager_ startAdvertising:@{
    CBAdvertisementDataServiceUUIDsKey : service_uuid_array
  }];
}

void BluetoothLowEnergyAdvertisementManagerMac::RegisterAdvertisement(
    std::unique_ptr<BluetoothAdvertisement::Data> advertisement_data,
    const BluetoothAdapter::CreateAdvertisementCallback& callback,
    const BluetoothAdapter::AdvertisementErrorCallback& error_callback) {
  base::Optional<BluetoothAdvertisement::ErrorCode> error_code;

  std::unique_ptr<BluetoothAdvertisement::UUIDList> service_uuids =
      advertisement_data->service_uuids();
  if (!service_uuids || advertisement_data->manufacturer_data() ||
      advertisement_data->solicit_uuids() ||
      advertisement_data->service_data()) {
    DVLOG(1) << "macOS only supports advertising service UUIDs.";
    error_code = BluetoothAdvertisement::ERROR_UNSUPPORTED_PLATFORM;
  }

  if (active_advertisement_ && active_advertisement_->status() !=
                                   BluetoothAdvertisementMac::UNREGISTERED) {
    DVLOG(1) << "Only one active BLE advertisement is currently supported.";
    error_code = BluetoothAdvertisement::ERROR_ADVERTISEMENT_ALREADY_EXISTS;
  }

  if (error_code) {
    ui_task_runner_->PostTask(
        FROM_HERE, base::BindOnce(std::move(error_callback), *error_code));
    return;
  }

  active_advertisement_ = new BluetoothAdvertisementMac(
      std::move(service_uuids), callback, error_callback, this);
  OnPeripheralManagerStateChanged();
}

void BluetoothLowEnergyAdvertisementManagerMac::UnregisterAdvertisement(
    BluetoothAdvertisementMac* advertisement,
    const BluetoothAdvertisement::SuccessCallback& success_callback,
    const BluetoothAdvertisement::ErrorCallback& error_callback) {
  if (advertisement != active_advertisement_.get()) {
    DVLOG(1) << "Cannot unregister none-active advertisement.";
    ui_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(error_callback),
                       BluetoothAdvertisement::ERROR_RESET_ADVERTISING));
    return;
  }

  active_advertisement_ = nullptr;
  [peripheral_manager_ stopAdvertising];
  ui_task_runner_->PostTask(FROM_HERE,
                            base::BindOnce(std::move(success_callback)));
}

void BluetoothLowEnergyAdvertisementManagerMac::DidStartAdvertising(
    NSError* error) {
  DCHECK(active_advertisement_ &&
         active_advertisement_->is_advertisement_pending());
  if (!active_advertisement_ ||
      !active_advertisement_->is_advertisement_pending()) {
    return;
  }

  if (error != nil) {
    DVLOG(1) << "Error advertising: "
             << base::SysNSStringToUTF8(error.localizedDescription);
    active_advertisement_->OnAdvertisementError(
        ui_task_runner_.get(),
        BluetoothAdvertisement::ERROR_STARTING_ADVERTISEMENT);
    active_advertisement_ = nullptr;
    return;
  }

  active_advertisement_->OnAdvertisementSuccess(ui_task_runner_.get());
}

CBPeripheralManagerState
BluetoothLowEnergyAdvertisementManagerMac::GetPeripheralManagerState() {
#if defined(MAC_OS_X_VERSION_10_13)
  // The state enum of this API changed in SDK 10.13, so for backwards
  // compatibility, we need to cast the state to a CBPeripheralManagerState.
  return static_cast<CBPeripheralManagerState>([peripheral_manager_ state]);
#else
  return [peripheral_manager_ state];
#endif
}

}  // device
