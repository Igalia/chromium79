// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/policy/device_local_account_policy_store.h"

#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/sequenced_task_runner.h"
#include "chrome/browser/chromeos/policy/value_validation/onc_user_policy_value_validator.h"
#include "components/ownership/owner_key_util.h"
#include "components/policy/core/common/cloud/device_management_service.h"
#include "components/policy/core/common/external_data_fetcher.h"
#include "components/policy/core/common/policy_map.h"
#include "components/policy/core/common/policy_types.h"
#include "components/policy/proto/cloud_policy.pb.h"
#include "components/policy/proto/device_management_backend.pb.h"

using RetrievePolicyResponseType =
    chromeos::SessionManagerClient::RetrievePolicyResponseType;

namespace em = enterprise_management;

namespace policy {

DeviceLocalAccountPolicyStore::DeviceLocalAccountPolicyStore(
    const std::string& account_id,
    chromeos::SessionManagerClient* session_manager_client,
    chromeos::DeviceSettingsService* device_settings_service,
    scoped_refptr<base::SequencedTaskRunner> background_task_runner)
    : UserCloudPolicyStoreBase(background_task_runner,
                               PolicyScope::POLICY_SCOPE_USER,
                               PolicySource::POLICY_SOURCE_CLOUD),
      account_id_(account_id),
      session_manager_client_(session_manager_client),
      device_settings_service_(device_settings_service) {}

DeviceLocalAccountPolicyStore::~DeviceLocalAccountPolicyStore() {}

void DeviceLocalAccountPolicyStore::Load() {
  // Cancel all pending requests.
  weak_factory_.InvalidateWeakPtrs();

  session_manager_client_->RetrieveDeviceLocalAccountPolicy(
      account_id_,
      base::Bind(&DeviceLocalAccountPolicyStore::ValidateLoadedPolicyBlob,
                 weak_factory_.GetWeakPtr(), true /*validate_in_background*/));
}

std::unique_ptr<UserCloudPolicyValidator>
DeviceLocalAccountPolicyStore::CreateValidator(
    std::unique_ptr<em::PolicyFetchResponse> policy,
    CloudPolicyValidatorBase::ValidateTimestampOption option) {
  auto validator =
      UserCloudPolicyStoreBase::CreateValidator(std::move(policy), option);
  validator->ValidateValues(std::make_unique<ONCUserPolicyValueValidator>());
  return validator;
}

void DeviceLocalAccountPolicyStore::LoadImmediately() {
  // This blocking D-Bus call is in the startup path and will block the UI
  // thread. This only happens when the Profile is created synchronously, which
  // on Chrome OS happens whenever the browser is restarted into the same
  // session, that is when the browser crashes, or right after signin if
  // the user has flags configured in about:flags.
  // However, on those paths we must load policy synchronously so that the
  // Profile initialization never sees unmanaged prefs, which would lead to
  // data loss. http://crbug.com/263061

  // Cancel all running async loads.
  weak_factory_.InvalidateWeakPtrs();

  std::string policy_blob;
  RetrievePolicyResponseType response =
      session_manager_client_->BlockingRetrieveDeviceLocalAccountPolicy(
          account_id_, &policy_blob);
  ValidateLoadedPolicyBlob(false /*validate_in_background*/, response,
                           policy_blob);
}

void DeviceLocalAccountPolicyStore::Store(
    const em::PolicyFetchResponse& policy) {
  // Cancel all pending requests.
  weak_factory_.InvalidateWeakPtrs();

  CheckKeyAndValidate(
      true, std::make_unique<em::PolicyFetchResponse>(policy),
      true /*validate_in_background*/,
      base::Bind(&DeviceLocalAccountPolicyStore::OnPolicyToStoreValidated,
                 weak_factory_.GetWeakPtr()));
}

void DeviceLocalAccountPolicyStore::ValidateLoadedPolicyBlob(
    bool validate_in_background,
    RetrievePolicyResponseType response_type,
    const std::string& policy_blob) {
  if (response_type != RetrievePolicyResponseType::SUCCESS ||
      policy_blob.empty()) {
    status_ = CloudPolicyStore::STATUS_LOAD_ERROR;
    NotifyStoreError();
  } else {
    std::unique_ptr<em::PolicyFetchResponse> policy(
        new em::PolicyFetchResponse());
    if (policy->ParseFromString(policy_blob)) {
      CheckKeyAndValidate(
          false, std::move(policy), validate_in_background,
          base::Bind(&DeviceLocalAccountPolicyStore::UpdatePolicy,
                     weak_factory_.GetWeakPtr()));
    } else {
      status_ = CloudPolicyStore::STATUS_PARSE_ERROR;
      NotifyStoreError();
    }
  }
}

void DeviceLocalAccountPolicyStore::UpdatePolicy(
    const std::string& signature_validation_public_key,
    UserCloudPolicyValidator* validator) {
  DCHECK(!signature_validation_public_key.empty());

  validation_result_ = validator->GetValidationResult();
  if (!validator->success()) {
    status_ = STATUS_VALIDATION_ERROR;
    NotifyStoreError();
    return;
  }

  InstallPolicy(std::move(validator->policy_data()),
                std::move(validator->payload()),
                signature_validation_public_key);
  status_ = STATUS_OK;
  NotifyStoreLoaded();
}

void DeviceLocalAccountPolicyStore::OnPolicyToStoreValidated(
    const std::string& signature_validation_public_key_unused,
    UserCloudPolicyValidator* validator) {
  validation_result_ = validator->GetValidationResult();
  if (!validator->success()) {
    status_ = STATUS_VALIDATION_ERROR;
    NotifyStoreError();
    return;
  }

  std::string policy_blob;
  if (!validator->policy()->SerializeToString(&policy_blob)) {
    status_ = CloudPolicyStore::STATUS_SERIALIZE_ERROR;
    NotifyStoreError();
    return;
  }

  session_manager_client_->StoreDeviceLocalAccountPolicy(
      account_id_, policy_blob,
      base::Bind(&DeviceLocalAccountPolicyStore::HandleStoreResult,
                 weak_factory_.GetWeakPtr()));
}

void DeviceLocalAccountPolicyStore::HandleStoreResult(bool success) {
  if (!success) {
    status_ = CloudPolicyStore::STATUS_STORE_ERROR;
    NotifyStoreError();
  } else {
    Load();
  }
}

void DeviceLocalAccountPolicyStore::CheckKeyAndValidate(
    bool valid_timestamp_required,
    std::unique_ptr<em::PolicyFetchResponse> policy,
    bool validate_in_background,
    const ValidateCompletionCallback& callback) {
  if (validate_in_background) {
    device_settings_service_->GetOwnershipStatusAsync(
        base::Bind(&DeviceLocalAccountPolicyStore::Validate,
                   weak_factory_.GetWeakPtr(), valid_timestamp_required,
                   base::Passed(&policy), callback, validate_in_background));
  } else {
    chromeos::DeviceSettingsService::OwnershipStatus ownership_status =
        device_settings_service_->GetOwnershipStatus();
    Validate(valid_timestamp_required, std::move(policy), callback,
             validate_in_background, ownership_status);
  }
}

void DeviceLocalAccountPolicyStore::Validate(
    bool valid_timestamp_required,
    std::unique_ptr<em::PolicyFetchResponse> policy_response,
    const ValidateCompletionCallback& callback,
    bool validate_in_background,
    chromeos::DeviceSettingsService::OwnershipStatus ownership_status) {
  DCHECK_NE(chromeos::DeviceSettingsService::OWNERSHIP_UNKNOWN,
            ownership_status);
  const em::PolicyData* device_policy_data =
      device_settings_service_->policy_data();
  // Note that the key is obtained through the device settings service instead
  // of using |policy_signature_public_key_| member, as the latter one is
  // updated only after the successful installation of the policy.
  scoped_refptr<ownership::PublicKey> key =
      device_settings_service_->GetPublicKey();
  if (!key.get() || !key->is_loaded() || !device_policy_data) {
    LOG(ERROR) << "Failed policy validation, key: " << (key.get() != nullptr)
               << ", is_loaded: " << (key.get() ? key->is_loaded() : false)
               << ", device_policy_data: " << (device_policy_data != nullptr);
    status_ = CloudPolicyStore::STATUS_BAD_STATE;
    NotifyStoreLoaded();
    return;
  }

  auto validator = std::make_unique<UserCloudPolicyValidator>(
      std::move(policy_response), background_task_runner());
  validator->ValidateUsername(account_id_);
  validator->ValidatePolicyType(dm_protocol::kChromePublicAccountPolicyType);
  // The timestamp is verified when storing a new policy downloaded from the
  // server but not when loading a cached policy from disk.
  // See SessionManagerOperation::ValidateDeviceSettings for the rationale.
  validator->ValidateAgainstCurrentPolicy(
      policy(),
      valid_timestamp_required
          ? CloudPolicyValidatorBase::TIMESTAMP_VALIDATED
          : CloudPolicyValidatorBase::TIMESTAMP_NOT_VALIDATED,
      CloudPolicyValidatorBase::DM_TOKEN_NOT_REQUIRED,
      CloudPolicyValidatorBase::DEVICE_ID_NOT_REQUIRED);

  // Validate the DMToken to match what device policy has.
  validator->ValidateDMToken(device_policy_data->request_token(),
                             CloudPolicyValidatorBase::DM_TOKEN_REQUIRED);

  // Validate the device id to match what device policy has.
  validator->ValidateDeviceId(device_policy_data->device_id(),
                              CloudPolicyValidatorBase::DEVICE_ID_REQUIRED);

  validator->ValidatePayload();
  validator->ValidateSignature(key->as_string());

  if (validate_in_background) {
    UserCloudPolicyValidator::StartValidation(
        std::move(validator), base::Bind(callback, key->as_string()));
  } else {
    validator->RunValidation();

    UpdatePolicy(key->as_string(), validator.get());
  }
}

}  // namespace policy
