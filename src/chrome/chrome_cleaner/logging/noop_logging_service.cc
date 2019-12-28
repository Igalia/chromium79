// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/chrome_cleaner/logging/noop_logging_service.h"

#include <vector>

#include "base/logging.h"
#include "base/memory/singleton.h"
#include "chrome/chrome_cleaner/os/disk_util_types.h"

namespace chrome_cleaner {

NoOpLoggingService* NoOpLoggingService::GetInstance() {
  return base::Singleton<NoOpLoggingService>::get();
}

NoOpLoggingService::NoOpLoggingService() {}

void NoOpLoggingService::Initialize(RegistryLogger* registry_logger) {}

void NoOpLoggingService::Terminate() {}

void NoOpLoggingService::SendLogsToSafeBrowsing(
    const UploadResultCallback& done_callback,
    RegistryLogger* registry_logger) {
  done_callback.Run(false);
}

void NoOpLoggingService::CancelWaitForShutdown() {}

void NoOpLoggingService::EnableUploads(bool enabled,
                                       RegistryLogger* registry_logger) {
  DCHECK(!enabled);
}

bool NoOpLoggingService::uploads_enabled() const {
  return false;
}

void NoOpLoggingService::SetDetailedSystemReport(
    bool /*detailed_system_report*/) {}

bool NoOpLoggingService::detailed_system_report_enabled() const {
  return false;
}

void NoOpLoggingService::AddFoundUwS(const std::string& /*found_uws_name*/) {}

void NoOpLoggingService::AddDetectedUwS(const PUPData::PUP* /*found_uws*/,
                                        UwSDetectedFlags flags) {}

void NoOpLoggingService::AddDetectedUwS(const UwS& uws) {}

void NoOpLoggingService::SetExitCode(ResultCode /*exit_code*/) {}

void NoOpLoggingService::AddLoadedModule(
    const base::string16& /*name*/,
    ModuleHost /*host*/,
    const internal::FileInformation& /*file_information*/) {}

void NoOpLoggingService::AddService(
    const base::string16& /*display_name*/,
    const base::string16& /*service_name*/,
    const internal::FileInformation& /*file_information*/) {}

void NoOpLoggingService::AddInstalledProgram(
    const base::FilePath& /*folder_path*/) {}

void NoOpLoggingService::AddProcess(
    const base::string16& /*name*/,
    const internal::FileInformation& /*file_information*/) {}

void NoOpLoggingService::AddRegistryValue(
    const internal::RegistryValue& /*registry_value*/,
    const std::vector<internal::FileInformation>& /*file_informations*/) {}

void NoOpLoggingService::AddLayeredServiceProvider(
    const std::vector<base::string16>& /*guids*/,
    const internal::FileInformation& /*file_information*/) {}

void NoOpLoggingService::SetWinInetProxySettings(
    const base::string16& /*config*/,
    const base::string16& /*bypass*/,
    const base::string16& /*auto_config_url*/,
    bool /*autodetect*/) {}

void NoOpLoggingService::SetWinHttpProxySettings(
    const base::string16& /*config*/,
    const base::string16& /*bypass*/) {}

void NoOpLoggingService::AddInstalledExtension(
    const base::string16& extension_id,
    ExtensionInstallMethod install_method,
    const std::vector<internal::FileInformation>& extension_files) {}

void NoOpLoggingService::AddScheduledTask(
    const base::string16& /*name*/,
    const base::string16& /*description*/,
    const std::vector<internal::FileInformation>& /*actions*/) {}

void NoOpLoggingService::AddShortcutData(
    const base::string16& /*lnk_path*/,
    const base::string16& /*executable_path*/,
    const std::string& /*executable _hash*/,
    const std::vector<base::string16>& /*command_line_arguments*/) {}

void NoOpLoggingService::SetFoundModifiedChromeShortcuts(
    bool /*found_modified_shortcuts*/) {}

void NoOpLoggingService::SetScannedLocations(
    const std::vector<UwS::TraceLocation>& /*scanned_locations*/) {}

void NoOpLoggingService::LogProcessInformation(
    SandboxType /*process_type*/,
    const SystemResourceUsage& /*usage*/) {}

bool NoOpLoggingService::AllExpectedRemovalsConfirmed() const {
  // This function should never be called on no-op logging service as it's used
  // only in the reporter. Return |false| as the default value to indicate error
  // if it ever happens.
  NOTREACHED();
  return false;
}

std::string NoOpLoggingService::RawReportContent() {
  return std::string();
}

bool NoOpLoggingService::ReadContentFromFile(const base::FilePath& log_file) {
  return true;
}

void NoOpLoggingService::ScheduleFallbackLogsUpload(
    RegistryLogger* registry_logger,
    ResultCode result_code) {}

}  // namespace chrome_cleaner
