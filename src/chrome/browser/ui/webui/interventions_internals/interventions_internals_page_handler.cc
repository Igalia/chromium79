// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/interventions_internals/interventions_internals_page_handler.h"

#include <utility>
#include <vector>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/metrics/field_trial_params.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/flag_descriptions.h"
#include "chrome/browser/ui/webui/interventions_internals/interventions_internals.mojom.h"
#include "chrome/common/chrome_switches.h"
#include "components/data_reduction_proxy/core/common/data_reduction_proxy_switches.h"
#include "components/previews/core/previews_experiments.h"
#include "components/previews/core/previews_switches.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/features.h"
#include "net/nqe/network_quality_estimator_params.h"
#include "services/network/public/cpp/network_quality_tracker.h"
#include "services/network/public/cpp/network_switches.h"

namespace {

// HTML DOM ID used in the JavaScript code. The IDs are generated here so that
// the DOM would have sensible name instead of autogenerated IDs.
const char kPreviewsAllowedHtmlId[] = "previews-allowed-status";
const char kLitePageRedirectHtmlId[] = "lite-page-redirect-status";
const char kNoScriptPreviewsHtmlId[] = "noscript-preview-status";
const char kResourceLoadingHintsHtmlId[] = "resource-loading-hints-status";
const char kOfflinePreviewsHtmlId[] = "offline-preview-status";
const char kDeferAllScriptPreviewsHtmlId[] = "defer-all-script-preview-status";

// Descriptions for previews.
const char kPreviewsAllowedDescription[] = "Previews Allowed";
const char kLitePageRedirectDescription[] =
    "Lite Page Redirect / Server Previews";
const char kNoScriptDescription[] = "NoScript Previews";
const char kResourceLoadingHintsDescription[] = "ResourceLoadingHints Previews";
const char kDeferAllScriptPreviewsDescription[] = "DeferAllScript Previews";
const char kOfflineDesciption[] = "Offline Previews";

// Flag feature name.
const char kPreviewsAllowedFeatureName[] = "Previews";
const char kLitePageRedirectFeatureName[] = "LitePageServerPreviews";
const char kNoScriptFeatureName[] = "NoScriptPreviews";
const char kResourceLoadingHintsFeatureName[] = "ResourceLoadingHints";
const char kDeferAllScriptFeatureName[] = "DeferAllScript";
#if defined(OS_ANDROID)
const char kOfflinePageFeatureName[] = "OfflinePreviews";
#endif  // OS_ANDROID

// HTML DOM ID used in the JavaScript code. The IDs are generated here so that
// the DOM would have sensible name instead of autogenerated IDs.
const char kPreviewsAllowedFlagHtmlId[] = "previews-flag";
const char kOfflinePageFlagHtmlId[] = "offline-page-flag";
const char kLitePageRedirectFlagHtmlId[] = "lite-page-redirect-flag";
const char kResourceLoadingHintsFlagHtmlId[] = "resource-loading-hints-flag";
const char kDeferAllScriptFlagHtmlId[] = "defer-all-script-flag";
const char kNoScriptFlagHtmlId[] = "noscript-flag";
const char kEctFlagHtmlId[] = "ect-flag";
const char kIgnorePreviewsBlacklistFlagHtmlId[] = "ignore-previews-blacklist";
const char kDataSaverAltConfigHtmlId[] =
    "data-reduction-proxy-server-experiment";

// Links to flags in chrome://flags.
// TODO(thanhdle): Refactor into vector of structs. crbug.com/787010.
const char kPreviewsAllowedFlagLink[] = "chrome://flags/#allow-previews";
const char kOfflinePageFlagLink[] = "chrome://flags/#enable-offline-previews";
const char kLitePageRedirectFlagLink[] =
    "chrome://flags/#enable-lite-page-server-previews";
const char kResourceLoadingHintsFlagLink[] =
    "chrome://flags/#enable-resource-loading-hints";
const char kDeferAllScriptFlagLink[] =
    "chrome://flags/#enable-defer-all-script";
const char kNoScriptFlagLink[] = "chrome://flags/#enable-noscript-previews";
const char kEctFlagLink[] = "chrome://flags/#force-effective-connection-type";
const char kIgnorePreviewsBlacklistLink[] =
    "chrome://flags/#ignore-previews-blacklist";
const char kDataSaverAltConfigLink[] =
    "chrome://flags/#enable-data-reduction-proxy-server-experiment";

const char kDefaultFlagValue[] = "Default";

// Check if the flag status of the flag is a forced value or not.
std::string GetFeatureFlagStatus(const std::string& feature_name) {
  std::string enabled_features =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          switches::kEnableFeatures);
  if (enabled_features.find(feature_name) != std::string::npos) {
    return "Enabled";
  }
  std::string disabled_features =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          switches::kDisableFeatures);
  if (disabled_features.find(feature_name) != std::string::npos) {
    return "Disabled";
  }
  return kDefaultFlagValue;
}

std::string GetNonFlagEctValue() {
  std::map<std::string, std::string> nqe_params;
  base::GetFieldTrialParamsByFeature(net::features::kNetworkQualityEstimator,
                                     &nqe_params);
  if (nqe_params.find(net::kForceEffectiveConnectionType) != nqe_params.end()) {
    return "Fieldtrial forced " +
           nqe_params[net::kForceEffectiveConnectionType];
  }
  return kDefaultFlagValue;
}

// Check if the switch flag is enabled or disabled via flag/command-line.
std::string GetEnabledStateForSwitch(const std::string& switch_name) {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(switch_name)
             ? "Enabled"
             : "Disabled";
}

}  // namespace

InterventionsInternalsPageHandler::InterventionsInternalsPageHandler(
    mojo::PendingReceiver<mojom::InterventionsInternalsPageHandler> receiver,
    previews::PreviewsUIService* previews_ui_service,
    network::NetworkQualityTracker* network_quality_tracker)
    : receiver_(this, std::move(receiver)),
      previews_ui_service_(previews_ui_service),
      network_quality_tracker_(network_quality_tracker),
      current_estimated_ect_(net::EFFECTIVE_CONNECTION_TYPE_UNKNOWN) {
  logger_ = previews_ui_service_->previews_logger();
  DCHECK(logger_);
}

InterventionsInternalsPageHandler::~InterventionsInternalsPageHandler() {
  DCHECK(logger_);
  logger_->RemoveObserver(this);
  (network_quality_tracker_ ? network_quality_tracker_
                            : g_browser_process->network_quality_tracker())
      ->RemoveEffectiveConnectionTypeObserver(this);
}

void InterventionsInternalsPageHandler::SetClientPage(
    mojo::PendingRemote<mojom::InterventionsInternalsPage> page) {
  page_.Bind(std::move(page));
  DCHECK(page_);
  logger_->AddAndNotifyObserver(this);
  (network_quality_tracker_ ? network_quality_tracker_
                            : g_browser_process->network_quality_tracker())
      ->AddEffectiveConnectionTypeObserver(this);
}

void InterventionsInternalsPageHandler::OnEffectiveConnectionTypeChanged(
    net::EffectiveConnectionType type) {
  current_estimated_ect_ = type;
  if (!page_) {
    // Don't try to notify the page if |page_| is not ready.
    return;
  }
  std::string ect_name = net::GetNameForEffectiveConnectionType(type);
  std::string max_intervention_ect_name =
      net::GetNameForEffectiveConnectionType(
          previews::params::GetSessionMaxECTThreshold());
  page_->UpdateEffectiveConnectionType(ect_name, max_intervention_ect_name);

  // Log change ECT event.
  previews::PreviewsLogger::MessageLog message(
      "ECT Changed" /* event_type */,
      "Effective Connection Type changed to " + ect_name, GURL(""),
      base::Time::Now(), 0 /* page_id */);
  OnNewMessageLogAdded(message);
}

void InterventionsInternalsPageHandler::OnNewMessageLogAdded(
    const previews::PreviewsLogger::MessageLog& message) {
  mojom::MessageLogPtr mojo_message_ptr(mojom::MessageLog::New());

  mojo_message_ptr->type = message.event_type;
  mojo_message_ptr->description = message.event_description;
  mojo_message_ptr->url = message.url;
  mojo_message_ptr->time = message.time.ToJavaTime();
  mojo_message_ptr->page_id = message.page_id;

  page_->LogNewMessage(std::move(mojo_message_ptr));
}

void InterventionsInternalsPageHandler::SetIgnorePreviewsBlacklistDecision(
    bool ignored) {
  previews_ui_service_->SetIgnorePreviewsBlacklistDecision(ignored);
}

void InterventionsInternalsPageHandler::OnLastObserverRemove() {
  // Reset the status of ignoring PreviewsBlackList decisions to default value.
  previews_ui_service_->SetIgnorePreviewsBlacklistDecision(
      previews::switches::ShouldIgnorePreviewsBlacklist());
}

void InterventionsInternalsPageHandler::OnIgnoreBlacklistDecisionStatusChanged(
    bool ignored) {
  page_->OnIgnoreBlacklistDecisionStatusChanged(ignored);
}

void InterventionsInternalsPageHandler::OnNewBlacklistedHost(
    const std::string& host,
    base::Time time) {
  page_->OnBlacklistedHost(host, time.ToJavaTime());
}

void InterventionsInternalsPageHandler::OnUserBlacklistedStatusChange(
    bool blacklisted) {
  page_->OnUserBlacklistedStatusChange(blacklisted);
}

void InterventionsInternalsPageHandler::OnBlacklistCleared(base::Time time) {
  page_->OnBlacklistCleared(time.ToJavaTime());
}

void InterventionsInternalsPageHandler::GetPreviewsEnabled(
    GetPreviewsEnabledCallback callback) {
  std::vector<mojom::PreviewsStatusPtr> statuses;

  auto previews_allowed_status = mojom::PreviewsStatus::New();
  previews_allowed_status->description = kPreviewsAllowedDescription;
  previews_allowed_status->enabled = previews::params::ArePreviewsAllowed();
  previews_allowed_status->htmlId = kPreviewsAllowedHtmlId;
  statuses.push_back(std::move(previews_allowed_status));

  auto offline_status = mojom::PreviewsStatus::New();
  offline_status->description = kOfflineDesciption;
  offline_status->enabled = previews::params::IsOfflinePreviewsEnabled();
  offline_status->htmlId = kOfflinePreviewsHtmlId;
  statuses.push_back(std::move(offline_status));

  auto lite_page_redirect_status = mojom::PreviewsStatus::New();
  lite_page_redirect_status->description = kLitePageRedirectDescription;
  lite_page_redirect_status->enabled =
      previews::params::IsLitePageServerPreviewsEnabled();
  lite_page_redirect_status->htmlId = kLitePageRedirectHtmlId;
  statuses.push_back(std::move(lite_page_redirect_status));

  auto resource_loading_hints_status = mojom::PreviewsStatus::New();
  resource_loading_hints_status->description = kResourceLoadingHintsDescription;
  resource_loading_hints_status->enabled =
      previews::params::IsResourceLoadingHintsEnabled();
  resource_loading_hints_status->htmlId = kResourceLoadingHintsHtmlId;
  statuses.push_back(std::move(resource_loading_hints_status));

  auto defer_all_script_preview_status = mojom::PreviewsStatus::New();
  defer_all_script_preview_status->description =
      kDeferAllScriptPreviewsDescription;
  defer_all_script_preview_status->enabled =
      previews::params::IsDeferAllScriptPreviewsEnabled();
  defer_all_script_preview_status->htmlId = kDeferAllScriptPreviewsHtmlId;
  statuses.push_back(std::move(defer_all_script_preview_status));

  auto noscript_status = mojom::PreviewsStatus::New();
  noscript_status->description = kNoScriptDescription;
  noscript_status->enabled = previews::params::IsNoScriptPreviewsEnabled();
  noscript_status->htmlId = kNoScriptPreviewsHtmlId;
  statuses.push_back(std::move(noscript_status));

  std::move(callback).Run(std::move(statuses));
}

void InterventionsInternalsPageHandler::GetPreviewsFlagsDetails(
    GetPreviewsFlagsDetailsCallback callback) {
  std::vector<mojom::PreviewsFlagPtr> flags;

  auto previews_allowed_status = mojom::PreviewsFlag::New();
  previews_allowed_status->description =
      flag_descriptions::kPreviewsAllowedName;
  previews_allowed_status->link = kPreviewsAllowedFlagLink;
  previews_allowed_status->value =
      GetFeatureFlagStatus(kPreviewsAllowedFeatureName);
  previews_allowed_status->htmlId = kPreviewsAllowedFlagHtmlId;
  flags.push_back(std::move(previews_allowed_status));

  auto offline_page_status = mojom::PreviewsFlag::New();
#if defined(OS_ANDROID)
  offline_page_status->description =
      flag_descriptions::kEnableOfflinePreviewsName;
  offline_page_status->value = GetFeatureFlagStatus(kOfflinePageFeatureName);
#else
  offline_page_status->description = "Offline Page Previews";
  offline_page_status->value = "Only support on Android";
#endif  // OS_ANDROID
  offline_page_status->link = kOfflinePageFlagLink;
  offline_page_status->htmlId = kOfflinePageFlagHtmlId;
  flags.push_back(std::move(offline_page_status));

  auto lite_page_redirect_status = mojom::PreviewsFlag::New();
  lite_page_redirect_status->description =
      flag_descriptions::kEnableLitePageServerPreviewsName;
  lite_page_redirect_status->link = kLitePageRedirectFlagLink;
  lite_page_redirect_status->value =
      GetFeatureFlagStatus(kLitePageRedirectFeatureName);
  lite_page_redirect_status->htmlId = kLitePageRedirectFlagHtmlId;
  flags.push_back(std::move(lite_page_redirect_status));

  auto resource_loading_hints_status = mojom::PreviewsFlag::New();
  resource_loading_hints_status->description =
      flag_descriptions::kEnableResourceLoadingHintsName;
  resource_loading_hints_status->link = kResourceLoadingHintsFlagLink;
  resource_loading_hints_status->value =
      GetFeatureFlagStatus(kResourceLoadingHintsFeatureName);
  resource_loading_hints_status->htmlId = kResourceLoadingHintsFlagHtmlId;
  flags.push_back(std::move(resource_loading_hints_status));

  auto defer_all_script_status = mojom::PreviewsFlag::New();
  defer_all_script_status->description =
      flag_descriptions::kEnableDeferAllScriptName;
  defer_all_script_status->link = kDeferAllScriptFlagLink;
  defer_all_script_status->value =
      GetFeatureFlagStatus(kDeferAllScriptFeatureName);
  defer_all_script_status->htmlId = kDeferAllScriptFlagHtmlId;
  flags.push_back(std::move(defer_all_script_status));

  auto noscript_status = mojom::PreviewsFlag::New();
  noscript_status->description = flag_descriptions::kEnableNoScriptPreviewsName;
  noscript_status->link = kNoScriptFlagLink;
  noscript_status->value = GetFeatureFlagStatus(kNoScriptFeatureName);
  noscript_status->htmlId = kNoScriptFlagHtmlId;
  flags.push_back(std::move(noscript_status));

  auto ect_status = mojom::PreviewsFlag::New();
  ect_status->description =
      flag_descriptions::kForceEffectiveConnectionTypeName;
  ect_status->link = kEctFlagLink;
  std::string ect_value =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          network::switches::kForceEffectiveConnectionType);
  ect_status->value = ect_value.empty() ? GetNonFlagEctValue() : ect_value;
  ect_status->htmlId = kEctFlagHtmlId;
  flags.push_back(std::move(ect_status));

  auto ignore_previews_blacklist = mojom::PreviewsFlag::New();
  ignore_previews_blacklist->description =
      flag_descriptions::kIgnorePreviewsBlacklistName;
  ignore_previews_blacklist->link = kIgnorePreviewsBlacklistLink;
  ignore_previews_blacklist->value =
      GetEnabledStateForSwitch(previews::switches::kIgnorePreviewsBlacklist);
  ignore_previews_blacklist->htmlId = kIgnorePreviewsBlacklistFlagHtmlId;
  flags.push_back(std::move(ignore_previews_blacklist));

  auto alt_config_status = mojom::PreviewsFlag::New();
  alt_config_status->description =
      flag_descriptions::kEnableDataReductionProxyServerExperimentDescription;
  alt_config_status->link = kDataSaverAltConfigLink;
  alt_config_status->value =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          data_reduction_proxy::switches::kDataReductionProxyExperiment);
  if (alt_config_status->value.empty())
    alt_config_status->value = kDefaultFlagValue;
  alt_config_status->htmlId = kDataSaverAltConfigHtmlId;
  flags.push_back(std::move(alt_config_status));

  std::move(callback).Run(std::move(flags));
}
