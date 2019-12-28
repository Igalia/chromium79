// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/predictors/loading_predictor.h"

#include <algorithm>
#include <vector>

#include "base/metrics/histogram_macros.h"
#include "chrome/browser/predictors/loading_data_collector.h"
#include "chrome/browser/predictors/loading_stats_collector.h"
#include "chrome/browser/predictors/navigation_id.h"
#include "chrome/browser/predictors/resource_prefetch_predictor.h"
#include "content/public/browser/browser_thread.h"
#include "net/base/network_isolation_key.h"
#include "url/origin.h"

namespace predictors {

namespace {

const base::TimeDelta kMinDelayBetweenPreresolveRequests =
    base::TimeDelta::FromSeconds(60);
const base::TimeDelta kMinDelayBetweenPreconnectRequests =
    base::TimeDelta::FromSeconds(10);

// Returns true iff |prediction| is not empty.
bool AddInitialUrlToPreconnectPrediction(const GURL& initial_url,
                                         PreconnectPrediction* prediction) {
  GURL initial_origin = initial_url.GetOrigin();
  // Open minimum 2 sockets to the main frame host to speed up the loading if a
  // main page has a redirect to the same host. This is because there can be a
  // race between reading the server redirect response and sending a new request
  // while the connection is still in use.
  static const int kMinSockets = 2;

  if (!prediction->requests.empty() &&
      prediction->requests.front().origin == initial_origin) {
    prediction->requests.front().num_sockets =
        std::max(prediction->requests.front().num_sockets, kMinSockets);
  } else if (initial_origin.is_valid() &&
             initial_origin.SchemeIsHTTPOrHTTPS()) {
    url::Origin origin = url::Origin::Create(initial_origin);
    prediction->requests.emplace(prediction->requests.begin(), initial_origin,
                                 kMinSockets,
                                 net::NetworkIsolationKey(origin, origin));
  }

  return !prediction->requests.empty();
}

}  // namespace

LoadingPredictor::LoadingPredictor(const LoadingPredictorConfig& config,
                                   Profile* profile)
    : config_(config),
      profile_(profile),
      resource_prefetch_predictor_(
          std::make_unique<ResourcePrefetchPredictor>(config, profile)),
      stats_collector_(std::make_unique<LoadingStatsCollector>(
          resource_prefetch_predictor_.get(),
          config)),
      loading_data_collector_(std::make_unique<LoadingDataCollector>(
          resource_prefetch_predictor_.get(),
          stats_collector_.get(),
          config)) {}

LoadingPredictor::~LoadingPredictor() {
  DCHECK(shutdown_);
}

void LoadingPredictor::PrepareForPageLoad(const GURL& url,
                                          HintOrigin origin,
                                          bool preconnectable) {
  if (shutdown_)
    return;

  if (origin == HintOrigin::OMNIBOX) {
    // Omnibox hints are lightweight and need a special treatment.
    HandleOmniboxHint(url, preconnectable);
    return;
  }

  if (active_hints_.find(url) != active_hints_.end())
    return;

  bool has_preconnect_prediction = false;
  PreconnectPrediction prediction;
  has_preconnect_prediction =
      resource_prefetch_predictor_->PredictPreconnectOrigins(url, &prediction);
  // Try to preconnect to the |url| even if the predictor has no
  // prediction.
  has_preconnect_prediction =
      AddInitialUrlToPreconnectPrediction(url, &prediction);

  if (!has_preconnect_prediction)
    return;

  ++total_hints_activated_;
  active_hints_.emplace(url, base::TimeTicks::Now());
  if (IsPreconnectAllowed(profile_))
    MaybeAddPreconnect(url, std::move(prediction.requests), origin);
}

void LoadingPredictor::CancelPageLoadHint(const GURL& url) {
  if (shutdown_)
    return;

  CancelActiveHint(active_hints_.find(url));
}

void LoadingPredictor::StartInitialization() {
  if (shutdown_)
    return;

  resource_prefetch_predictor_->StartInitialization();
}

LoadingDataCollector* LoadingPredictor::loading_data_collector() {
  return loading_data_collector_.get();
}

ResourcePrefetchPredictor* LoadingPredictor::resource_prefetch_predictor() {
  return resource_prefetch_predictor_.get();
}

PreconnectManager* LoadingPredictor::preconnect_manager() {
  if (shutdown_ || !IsPreconnectFeatureEnabled())
    return nullptr;

  if (!preconnect_manager_) {
    preconnect_manager_ =
        std::make_unique<PreconnectManager>(GetWeakPtr(), profile_);
  }

  return preconnect_manager_.get();
}

void LoadingPredictor::Shutdown() {
  DCHECK(!shutdown_);
  resource_prefetch_predictor_->Shutdown();
  shutdown_ = true;
}

void LoadingPredictor::OnNavigationStarted(const NavigationID& navigation_id) {
  if (shutdown_)
    return;

  loading_data_collector()->RecordStartNavigation(navigation_id);
  CleanupAbandonedHintsAndNavigations(navigation_id);
  active_navigations_.emplace(navigation_id);
  PrepareForPageLoad(navigation_id.main_frame_url, HintOrigin::NAVIGATION);
}

void LoadingPredictor::OnNavigationFinished(
    const NavigationID& old_navigation_id,
    const NavigationID& new_navigation_id,
    bool is_error_page) {
  if (shutdown_)
    return;

  loading_data_collector()->RecordFinishNavigation(
      old_navigation_id, new_navigation_id, is_error_page);
  active_navigations_.erase(old_navigation_id);
  CancelPageLoadHint(old_navigation_id.main_frame_url);
}

std::map<GURL, base::TimeTicks>::iterator LoadingPredictor::CancelActiveHint(
    std::map<GURL, base::TimeTicks>::iterator hint_it) {
  if (hint_it == active_hints_.end())
    return hint_it;

  const GURL& url = hint_it->first;
  MaybeRemovePreconnect(url);
  return active_hints_.erase(hint_it);
}

void LoadingPredictor::CleanupAbandonedHintsAndNavigations(
    const NavigationID& navigation_id) {
  base::TimeTicks time_now = base::TimeTicks::Now();
  const base::TimeDelta max_navigation_age =
      base::TimeDelta::FromSeconds(config_.max_navigation_lifetime_seconds);

  // Hints.
  for (auto it = active_hints_.begin(); it != active_hints_.end();) {
    base::TimeDelta prefetch_age = time_now - it->second;
    if (prefetch_age > max_navigation_age) {
      // Will go to the last bucket in the duration reported in
      // CancelActiveHint() meaning that the duration was unlimited.
      it = CancelActiveHint(it);
    } else {
      ++it;
    }
  }

  // Navigations.
  for (auto it = active_navigations_.begin();
       it != active_navigations_.end();) {
    if ((it->tab_id == navigation_id.tab_id) ||
        (time_now - it->creation_time > max_navigation_age)) {
      CancelActiveHint(active_hints_.find(it->main_frame_url));
      it = active_navigations_.erase(it);
    } else {
      ++it;
    }
  }
}

void LoadingPredictor::MaybeAddPreconnect(
    const GURL& url,
    std::vector<PreconnectRequest> requests,
    HintOrigin origin) {
  DCHECK(!shutdown_);
  preconnect_manager()->Start(url, std::move(requests));
}

void LoadingPredictor::MaybeRemovePreconnect(const GURL& url) {
  DCHECK(!shutdown_);
  if (!preconnect_manager_)
    return;

  preconnect_manager_->Stop(url);
}

void LoadingPredictor::HandleOmniboxHint(const GURL& url, bool preconnectable) {
  if (!url.is_valid() || !url.has_host() || !IsPreconnectAllowed(profile_))
    return;

  GURL origin = url.GetOrigin();
  bool is_new_origin = origin != last_omnibox_origin_;
  last_omnibox_origin_ = origin;
  base::TimeTicks now = base::TimeTicks::Now();
  if (preconnectable) {
    if (is_new_origin || now - last_omnibox_preconnect_time_ >=
                             kMinDelayBetweenPreconnectRequests) {
      last_omnibox_preconnect_time_ = now;
      // Not to be confused with |origin|.
      url::Origin url_origin = url::Origin::Create(url);
      preconnect_manager()->StartPreconnectUrl(
          url, true, net::NetworkIsolationKey(url_origin, url_origin));
    }
    return;
  }

  if (is_new_origin || now - last_omnibox_preresolve_time_ >=
                           kMinDelayBetweenPreresolveRequests) {
    last_omnibox_preresolve_time_ = now;
    preconnect_manager()->StartPreresolveHost(url);
  }
}

void LoadingPredictor::PreconnectFinished(
    std::unique_ptr<PreconnectStats> stats) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (shutdown_)
    return;

  DCHECK(stats);
  active_hints_.erase(stats->url);
  stats_collector_->RecordPreconnectStats(std::move(stats));
}

}  // namespace predictors
