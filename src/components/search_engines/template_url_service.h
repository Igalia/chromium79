// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SEARCH_ENGINES_TEMPLATE_URL_SERVICE_H_
#define COMPONENTS_SEARCH_ENGINES_TEMPLATE_URL_SERVICE_H_

#include <stddef.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/callback_list.h"
#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/time/default_clock.h"
#include "build/build_config.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/search_engines/default_search_manager.h"
#include "components/search_engines/keyword_web_data_service.h"
#include "components/search_engines/search_host_to_urls_map.h"
#include "components/search_engines/search_terms_data.h"
#include "components/search_engines/template_url.h"
#include "components/sync/model/sync_change.h"
#include "components/sync/model/syncable_service.h"
#include "components/webdata/common/web_data_service_consumer.h"
#if defined(OS_ANDROID)
#include "base/android/scoped_java_ref.h"
#endif

class GURL;
class PrefService;
class TemplateURLServiceClient;
class TemplateURLServiceObserver;
struct TemplateURLData;
#if defined(OS_ANDROID)
class TemplateUrlServiceAndroid;
#endif

namespace rappor {
class RapporServiceImpl;
}

namespace syncer {
class SyncData;
class SyncErrorFactory;
}

namespace user_prefs {
class PrefRegistrySyncable;
}

// TemplateURLService is the backend for keywords. It's used by
// KeywordAutocomplete.
//
// TemplateURLService stores a vector of TemplateURLs. The TemplateURLs are
// persisted to the database maintained by KeywordWebDataService.
// *ALL* mutations to the TemplateURLs must funnel through TemplateURLService.
// This allows TemplateURLService to notify listeners of changes as well as keep
// the database in sync.
//
// TemplateURLService does not load the vector of TemplateURLs in its
// constructor (except for testing). Use the Load method to trigger a load.
// When TemplateURLService has completed loading, observers are notified via
// OnTemplateURLServiceChanged, or by a callback registered prior to calling
// the Load method.
//
// TemplateURLService takes ownership of any TemplateURL passed to it. If there
// is a KeywordWebDataService, deletion is handled by KeywordWebDataService,
// otherwise TemplateURLService handles deletion.

class TemplateURLService : public WebDataServiceConsumer,
                           public KeyedService,
                           public syncer::SyncableService {
 public:
  using QueryTerms = std::map<std::string, std::string>;
  using TemplateURLVector = TemplateURL::TemplateURLVector;
  using OwnedTemplateURLVector = TemplateURL::OwnedTemplateURLVector;
  using SyncDataMap = std::map<std::string, syncer::SyncData>;
  using Subscription = base::CallbackList<void(void)>::Subscription;

  // We may want to treat the keyword in a TemplateURL as being a different
  // length than it actually is.  For example, for keywords that end in a
  // registry, e.g., '.com', we want to consider the registry characters as not
  // a meaningful part of the keyword and not penalize for the user not typing
  // those.)
  using TURLAndMeaningfulLength = std::pair<TemplateURL*, size_t>;
  using TURLsAndMeaningfulLengths = std::vector<TURLAndMeaningfulLength>;

  // Struct used for initializing the data store with fake data.
  // Each initializer is mapped to a TemplateURL.
  struct Initializer {
    const char* const keyword;
    const char* const url;
    const char* const content;
  };

  struct URLVisitedDetails {
    GURL url;
    bool is_keyword_transition;
  };

  TemplateURLService(
      PrefService* prefs,
      std::unique_ptr<SearchTermsData> search_terms_data,
      const scoped_refptr<KeywordWebDataService>& web_data_service,
      std::unique_ptr<TemplateURLServiceClient> client,
      rappor::RapporServiceImpl* rappor_service,
      const base::RepeatingClosure& dsp_change_callback);
  // The following is for testing.
  TemplateURLService(const Initializer* initializers, const int count);
  ~TemplateURLService() override;

  // Register Profile preferences in |registry|.
  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

#if defined(OS_ANDROID)
  base::android::ScopedJavaLocalRef<jobject> GetJavaObject();
#endif

  // Returns true if there is no TemplateURL that conflicts with the
  // keyword/url pair, or there is one but it can be replaced. If there is an
  // existing keyword that can be replaced and template_url_to_replace is
  // non-NULL, template_url_to_replace is set to the keyword to replace.
  //
  // |url| is the URL of the search query.  This is used to prevent auto-adding
  // a keyword for hosts already associated with a manually-edited keyword.
  bool CanAddAutogeneratedKeyword(const base::string16& keyword,
                                  const GURL& url,
                                  const TemplateURL** template_url_to_replace);

  // Returns whether the engine is a "pre-existing" engine, either from the
  // prepopulate list or created by policy.
  bool IsPrepopulatedOrCreatedByPolicy(const TemplateURL* template_url) const;

  // Returns whether |template_url| should be shown in the list of engines
  // most likely to be selected as a default engine. This is meant to highlight
  // the current default, as well as the other most likely choices of default
  // engine, separately from a full list of all TemplateURLs (which might be
  // very long).
  bool ShowInDefaultList(const TemplateURL* template_url) const;

  // Adds to |matches| all TemplateURLs whose keywords begin with |prefix|,
  // sorted shortest-keyword-first. If |supports_replacement_only| is true, only
  // TemplateURLs that support replacement are returned.
  void AddMatchingKeywords(const base::string16& prefix,
                           bool supports_replacement_only,
                           TURLsAndMeaningfulLengths* matches);

  // Adds to |matches| all TemplateURLs for search engines with the domain
  // name part of the keyword starts with |prefix|, sorted
  // shortest-domain-name-first. If |supports_replacement_only| is true, only
  // TemplateURLs that support replacement are returned.  Does not bother
  // searching/returning keywords that would've been found with an identical
  // call to FindMatchingKeywords(); i.e., doesn't search keywords for which
  // the domain name is the keyword.
  void AddMatchingDomainKeywords(const base::string16& prefix,
                                 bool supports_replacement_only,
                                 TURLsAndMeaningfulLengths* matches);

  // Looks up |keyword| and returns the element it maps to.  Returns NULL if
  // the keyword was not found.
  // The caller should not try to delete the returned pointer; the data store
  // retains ownership of it.
  TemplateURL* GetTemplateURLForKeyword(const base::string16& keyword);
  const TemplateURL* GetTemplateURLForKeyword(
      const base::string16& keyword) const;

  // Returns that TemplateURL with the specified GUID, or NULL if not found.
  // The caller should not try to delete the returned pointer; the data store
  // retains ownership of it.
  TemplateURL* GetTemplateURLForGUID(const std::string& sync_guid);
  const TemplateURL* GetTemplateURLForGUID(const std::string& sync_guid) const;

  // Returns the first TemplateURL found with a URL using the specified |host|,
  // or NULL if there are no such TemplateURLs
  TemplateURL* GetTemplateURLForHost(const std::string& host);
  const TemplateURL* GetTemplateURLForHost(const std::string& host) const;

  // Adds a new TemplateURL to this model.
  //
  // This function guarantees that on return the model will not have two non-
  // extension TemplateURLs with the same keyword.  If that means that it cannot
  // add the provided argument, it will return null.  Otherwise it will return
  // the raw pointer to the TemplateURL.
  //
  // Returns a raw pointer to |template_url| if the addition succeeded, or null
  // on failure.  (Many callers need still need a raw pointer to the TemplateURL
  // so they can access it later.)
  TemplateURL* Add(std::unique_ptr<TemplateURL> template_url);

  // Like Add(), but overwrites the |template_url|'s values with the provided
  // ones.
  TemplateURL* AddWithOverrides(std::unique_ptr<TemplateURL> template_url,
                                const base::string16& short_name,
                                const base::string16& keyword,
                                const std::string& url);

  // Removes the keyword from the model. This deletes the supplied TemplateURL.
  // This fails if the supplied template_url is the default search provider.
  void Remove(const TemplateURL* template_url);

  // Removes any TemplateURL of the specified |type| associated with
  // |extension_id|. Unlike with Remove(), this can be called when the
  // TemplateURL in question is the current default search provider.
  void RemoveExtensionControlledTURL(const std::string& extension_id,
                                     TemplateURL::Type type);

  // Removes all auto-generated keywords that were created on or after the
  // date passed in.
  void RemoveAutoGeneratedSince(base::Time created_after);

  // Removes all auto-generated keywords that were created in the specified
  // range.
  void RemoveAutoGeneratedBetween(base::Time created_after,
                                  base::Time created_before);

  // Removes all auto-generated keywords that were created in the specified
  // range and match |url_filter|. If |url_filter| is_null(), deletes all
  // auto-generated keywords in the range.
  void RemoveAutoGeneratedForUrlsBetween(
      const base::Callback<bool(const GURL&)>& url_filter,
      base::Time created_after,
      base::Time created_before);

  // Adds a TemplateURL for an extension with an omnibox keyword.
  // Only 1 keyword is allowed for a given extension. If a keyword
  // already exists for this extension, does nothing.
  void RegisterOmniboxKeyword(const std::string& extension_id,
                              const std::string& extension_name,
                              const std::string& keyword,
                              const std::string& template_url_string,
                              const base::Time& extension_install_time);

  // Returns the set of URLs describing the keywords. The elements are owned
  // by TemplateURLService and should not be deleted.
  TemplateURLVector GetTemplateURLs();

  // Increment the usage count of a keyword.
  // Called when a URL is loaded that was generated from a keyword.
  void IncrementUsageCount(TemplateURL* url);

  // Resets the title, keyword and search url of the specified TemplateURL.
  // The TemplateURL is marked as not replaceable.
  void ResetTemplateURL(TemplateURL* url,
                        const base::string16& title,
                        const base::string16& keyword,
                        const std::string& search_url);

  // Creates TemplateURL, populating it with data from Play API. If TemplateURL
  // with matching keyword already exists then merges Play API data into it.
  // Sets |created_from_play_api| flag.
  TemplateURL* CreateOrUpdateTemplateURLFromPlayAPIData(
      const base::string16& title,
      const base::string16& keyword,
      const std::string& search_url,
      const std::string& suggestions_url,
      const std::string& favicon_url);

  // Updates any search providers matching |potential_search_url| with the new
  // favicon location |favicon_url|.
  void UpdateProviderFavicons(const GURL& potential_search_url,
                              const GURL& favicon_url);

  // Return true if the given |url| can be made the default. This returns false
  // regardless of |url| if the default search provider is managed by policy or
  // controlled by an extension.
  bool CanMakeDefault(const TemplateURL* url) const;

  // Set the default search provider.  |url| may be null.
  // This will assert if the default search is managed; the UI should not be
  // invoking this method in that situation.
  void SetUserSelectedDefaultSearchProvider(TemplateURL* url);

  // Returns the default search provider. If the TemplateURLService hasn't been
  // loaded, the default search provider is pulled from preferences.
  //
  // NOTE: This may return null in certain circumstances such as:
  //       1.) Unit test mode
  //       2.) The default search engine is disabled by policy.
  const TemplateURL* GetDefaultSearchProvider() const;

  // Returns true if the |url| is a search results page from the default search
  // provider.
  bool IsSearchResultsPageFromDefaultSearchProvider(const GURL& url) const;

  // Returns true if the default search is managed through group policy.
  bool is_default_search_managed() const {
    return default_search_provider_source_ == DefaultSearchManager::FROM_POLICY;
  }

  // Returns true if the default search provider is controlled by an extension.
  bool IsExtensionControlledDefaultSearch() const;

  // Returns the default search specified in the prepopulated data, if it
  // exists.  If not, returns first URL in |template_urls_|, or NULL if that's
  // empty. The returned object is owned by TemplateURLService and can be
  // destroyed at any time so should be used right after the call.
  TemplateURL* FindNewDefaultSearchProvider();

  // Performs the same actions that happen when the prepopulate data version is
  // revved: all existing prepopulated entries are checked against the current
  // prepopulate data, any now-extraneous safe_for_autoreplace() entries are
  // removed, any existing engines are reset to the provided data (except for
  // user-edited names or keywords), and any new prepopulated engines are
  // added.
  //
  // After this, the default search engine is reset to the default entry in the
  // prepopulate data.
  void RepairPrepopulatedSearchEngines();

  // Observers used to listen for changes to the model.
  // TemplateURLService does NOT delete the observers when deleted.
  void AddObserver(TemplateURLServiceObserver* observer);
  void RemoveObserver(TemplateURLServiceObserver* observer);

  // Loads the keywords. This has no effect if the keywords have already been
  // loaded.
  // Observers are notified when loading completes via the method
  // OnTemplateURLServiceChanged.
  void Load();

  // Registers a callback to be called when the service has loaded.
  //
  // If the service has already loaded, this function does nothing.
  std::unique_ptr<Subscription> RegisterOnLoadedCallback(
      const base::RepeatingClosure& callback);

#if defined(UNIT_TEST)
  void set_loaded(bool value) { loaded_ = value; }

  // Turns Load() into a no-op.
  void set_disable_load(bool value) { disable_load_ = value; }
#endif

  // Whether or not the keywords have been loaded.
  bool loaded() { return loaded_; }

  // Notification that the keywords have been loaded.
  // This is invoked from WebDataService, and should not be directly
  // invoked.
  void OnWebDataServiceRequestDone(
      KeywordWebDataService::Handle h,
      std::unique_ptr<WDTypedResult> result) override;

  // Returns the locale-direction-adjusted short name for the given keyword.
  // Also sets the out param to indicate whether the keyword belongs to an
  // Omnibox extension.
  base::string16 GetKeywordShortName(
      const base::string16& keyword,
      bool* is_omnibox_api_extension_keyword) const;

  // Called by the history service when a URL is visited.
  void OnHistoryURLVisited(const URLVisitedDetails& details);

  // KeyedService implementation.
  void Shutdown() override;

  // syncer::SyncableService implementation.

  // Waits until keywords have been loaded.
  void WaitUntilReadyToSync(base::OnceClosure done) override;

  // Returns all syncable TemplateURLs from this model as SyncData. This should
  // include every search engine and no Extension keywords.
  syncer::SyncDataList GetAllSyncData(syncer::ModelType type) const override;
  // Process new search engine changes from Sync, merging them into our local
  // data. This may send notifications if local search engines are added,
  // updated or removed.
  syncer::SyncError ProcessSyncChanges(
      const base::Location& from_here,
      const syncer::SyncChangeList& change_list) override;
  // Merge initial search engine data from Sync and push any local changes up
  // to Sync. This may send notifications if local search engines are added,
  // updated or removed.
  syncer::SyncMergeResult MergeDataAndStartSyncing(
      syncer::ModelType type,
      const syncer::SyncDataList& initial_sync_data,
      std::unique_ptr<syncer::SyncChangeProcessor> sync_processor,
      std::unique_ptr<syncer::SyncErrorFactory> sync_error_factory) override;
  void StopSyncing(syncer::ModelType type) override;

  // Processes a local TemplateURL change for Sync. |turl| is the TemplateURL
  // that has been modified, and |type| is the Sync ChangeType that took place.
  // This may send a new SyncChange to the cloud. If our model has not yet been
  // associated with Sync, or if this is triggered by a Sync change, then this
  // does nothing.
  void ProcessTemplateURLChange(const base::Location& from_here,
                                const TemplateURL* turl,
                                syncer::SyncChange::SyncChangeType type);

  // Returns a SearchTermsData which can be used to call TemplateURL methods.
  const SearchTermsData& search_terms_data() const {
    return *search_terms_data_;
  }

  // Returns a SyncData with a sync representation of the search engine data
  // from |turl|.
  static syncer::SyncData CreateSyncDataFromTemplateURL(
      const TemplateURL& turl);

  // Creates a new heap-allocated TemplateURL* which is populated by overlaying
  // |sync_data| atop |existing_turl|.  |existing_turl| may be NULL; if not it
  // remains unmodified.  The caller owns the returned TemplateURL*.
  //
  // If the created TemplateURL is migrated in some way from out-of-date sync
  // data, an appropriate SyncChange is added to |change_list|.  If the sync
  // data is bad for some reason, an ACTION_DELETE change is added and the
  // function returns NULL.
  static std::unique_ptr<TemplateURL>
  CreateTemplateURLFromTemplateURLAndSyncData(
      TemplateURLServiceClient* client,
      PrefService* prefs,
      const SearchTermsData& search_terms_data,
      const TemplateURL* existing_turl,
      const syncer::SyncData& sync_data,
      syncer::SyncChangeList* change_list);

  // Returns a map mapping Sync GUIDs to pointers to syncer::SyncData.
  static SyncDataMap CreateGUIDToSyncDataMap(
      const syncer::SyncDataList& sync_data);

#if defined(UNIT_TEST)
  void set_clock(std::unique_ptr<base::Clock> clock) {
    clock_ = std::move(clock);
  }
#endif

 private:
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceTest, TestManagedDefaultSearch);
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceTest,
                           UpdateKeywordSearchTermsForURL);
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceTest,
                           DontUpdateKeywordSearchForNonReplaceable);
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceTest, ChangeGoogleBaseValue);
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceTest, MergeDeletesUnusedProviders);
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceTest, AddOmniboxExtensionKeyword);
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceTest, ExtensionsWithSameKeywords);
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceTest,
                           CheckEnginesWithSameKeywords);
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceTest, LastVisitedTimeUpdate);
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceTest,
                           RepairPrepopulatedSearchEngines);
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceSyncTest, UniquifyKeyword);
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceSyncTest,
                           IsLocalTemplateURLBetter);
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceSyncTest,
                           ResolveSyncKeywordConflict);
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceSyncTest, PreSyncDeletes);
  FRIEND_TEST_ALL_PREFIXES(TemplateURLServiceSyncTest, MergeInSyncTemplateURL);
  FRIEND_TEST_ALL_PREFIXES(LocationBarModelTest, GoogleBaseURL);

  friend class InstantUnitTestBase;
  friend class Scoper;
  friend class TemplateURLServiceTestUtil;
  friend class TemplateUrlServiceAndroid;

  using GUIDToTURL = std::map<std::string, TemplateURL*>;

  // A mapping from keywords to the corresponding TemplateURLs and their
  // meaningful keyword lengths.  A keyword can appear only once here because
  // there can be only one active TemplateURL associated with a given keyword.
  using KeywordToTURLAndMeaningfulLength =
      std::map<base::string16, TURLAndMeaningfulLength>;

  // A mapping from domain names to corresponding TemplateURLs and their
  // meaningful keyword lengths.  Specifically, for a keyword that is a
  // hostname containing more than just a domain name, e.g., 'abc.def.com',
  // the keyword is added to this map under the domain key 'def.com'.  This
  // means multiple keywords from the same domain share the same key, so this
  // must be a multimap.
  using KeywordDomainToTURLAndMeaningfulLength =
      std::multimap<base::string16, TURLAndMeaningfulLength>;

  // Declaration of values to be used in an enumerated histogram to tally
  // changes to the default search provider from various entry points. In
  // particular, we use this to see what proportion of changes are from Sync
  // entry points, to help spot erroneous Sync activity.
  enum DefaultSearchChangeOrigin {
    // Various known Sync entry points.
    DSP_CHANGE_SYNC_PREF,
    DSP_CHANGE_SYNC_ADD,
    DSP_CHANGE_SYNC_DELETE,
    DSP_CHANGE_SYNC_NOT_MANAGED,
    // "Other" origins. We differentiate between Sync and not Sync so we know if
    // certain changes were intentionally from the system, or possibly some
    // unintentional change from when we were Syncing.
    DSP_CHANGE_SYNC_UNINTENTIONAL,
    // All changes that don't fall into another category; we can't reorder the
    // list for clarity as this would screw up stat collection.
    DSP_CHANGE_OTHER,
    // Changed through "Profile Reset" feature.
    DSP_CHANGE_PROFILE_RESET,
    // Changed by an extension through the Override Settings API.
    DSP_CHANGE_OVERRIDE_SETTINGS_EXTENSION,
    // New DSP during database/prepopulate data load, which was not previously
    // in the known engine set, and with no previous value in prefs.  The
    // typical time to see this is during first run.
    DSP_CHANGE_NEW_ENGINE_NO_PREFS,
    // Boundary value.
    DSP_CHANGE_MAX,
  };

  // Helper functor for FindMatchingKeywords(), for finding the range of
  // keywords which begin with a prefix.
  class LessWithPrefix;

  // Used to defer notifications until the last Scoper is destroyed by leaving
  // the scope of a code block.
  class Scoper;

  void Init(const Initializer* initializers, int num_initializers);

  // Given two engines with the same keyword, returns which should take
  // precedence.  While normal engines must all have distinct keywords,
  // extension-controlled and omnibox API engines may have the same keywords as
  // each other or as normal engines.  In these cases, omnibox API engines
  // override extension-controlled engines, which override normal engines; if
  // there is still a conflict after this, the most recently-added extension
  // wins.
  TemplateURL* BestEngineForKeyword(TemplateURL* engine1, TemplateURL* engine2);

  // Removes |template_url| from various internal maps
  // (|keyword_to_turl_and_length_|, |keyword_domain_to_turl_and_length_|,
  // |guid_to_turl_|, |provider_map_|).
  void RemoveFromMaps(const TemplateURL* template_url);

  // Adds |template_url| to various internal maps
  // (|keyword_to_turl_and_length_|, |keyword_domain_to_turl_and_length_|,
  // |guid_to_turl_|, |provider_map_|) if appropriate.  (It might not be
  // appropriate if, for instance, |template_url|'s keyword conflicts with
  // the keyword of a custom search engine already existing in the maps that
  // is not allowed to be replaced.)
  void AddToMaps(TemplateURL* template_url);

  // Helper function for removing an element from
  // |keyword_domain_to_turl_and_length_|.
  void RemoveFromDomainMap(const TemplateURL* template_url);

  // Helper fuction for adding an element to
  // |keyword_domain_to_turl_and_length_| if appropriate.
  void AddToDomainMap(TemplateURL* template_url);

  // Helper function for adding an element to |keyword_to_turl_and_length_|.
  void AddToMap(TemplateURL* template_url);

  // Sets the keywords. This is used once the keywords have been loaded.
  // This does NOT notify the delegate or the database.
  void SetTemplateURLs(std::unique_ptr<OwnedTemplateURLVector> urls);

  // Transitions to the loaded state.
  void ChangeToLoadedState();

  // Applies a DSE change and reports metrics if appropriate.
  void ApplyDefaultSearchChange(const TemplateURLData* new_dse_data,
                                DefaultSearchManager::Source source);


  // Applies a DSE change. May be called at startup or after transitioning to
  // the loaded state. Returns true if a change actually occurred.
  bool ApplyDefaultSearchChangeNoMetrics(const TemplateURLData* new_dse_data,
                                         DefaultSearchManager::Source source);

  // Returns false if there is a TemplateURL that has a search url with the
  // specified host and that TemplateURL has been manually modified.
  bool CanAddAutogeneratedKeywordForHost(const std::string& host) const;

  // Returns true if the TemplateURL is replaceable. This doesn't look at the
  // uniqueness of the keyword or host and is intended to be called after those
  // checks have been done. This returns true if the TemplateURL doesn't appear
  // in the default list and is marked as safe_for_autoreplace.
  bool CanReplace(const TemplateURL* t_url) const;

  // Like GetTemplateURLForKeyword(), but ignores extension-provided keywords.
  TemplateURL* FindNonExtensionTemplateURLForKeyword(
      const base::string16& keyword);

  // Updates the information in |existing_turl| using the information from
  // |new_values|, but the ID for |existing_turl| is retained. Returns whether
  // |existing_turl| was found in |template_urls_| and thus could be updated.
  //
  // NOTE: This should not be called with an extension keyword as there are no
  // updates needed in that case.
  bool Update(TemplateURL* existing_turl, const TemplateURL& new_values);

  // If the TemplateURL comes from a prepopulated URL available in the current
  // country, update all its fields save for the keyword, short name and id so
  // that they match the internal prepopulated URL. TemplateURLs not coming from
  // a prepopulated URL are not modified.
  static void UpdateTemplateURLIfPrepopulated(TemplateURL* existing_turl,
                                              PrefService* prefs);

  // If the TemplateURL's sync GUID matches the kSyncedDefaultSearchProviderGUID
  // preference it will be used to update the DSE in prefs.
  // OnDefaultSearchChange may be triggered as a result.
  void MaybeUpdateDSEViaPrefs(TemplateURL* synced_turl);

  // Iterates through the TemplateURLs to see if one matches the visited url.
  // For each TemplateURL whose url matches the visited url
  // SetKeywordSearchTermsForURL is invoked.
  void UpdateKeywordSearchTermsForURL(const URLVisitedDetails& details);

  // Updates the last_visited time of |url| to the current time.
  void UpdateTemplateURLVisitTime(TemplateURL* url);

  // If necessary, generates a visit for the site http:// + t_url.keyword().
  void AddTabToSearchVisit(const TemplateURL& t_url);

  // Adds a new TemplateURL to this model.
  //
  // If |newly_adding| is false, we assume that this TemplateURL was already
  // part of the model in the past, and therefore we don't need to do things
  // like assign it an ID or notify sync.
  //
  // This function guarantees that on return the model will not have two non-
  // extension TemplateURLs with the same keyword.  If that means that it cannot
  // add the provided argument, it will return null.  Otherwise it will return
  // the raw pointer to the TemplateURL.
  //
  // Returns a raw pointer to |template_url| if the addition succeeded, or null
  // on failure.  (Many callers need still need a raw pointer to the TemplateURL
  // so they can access it later.)
  TemplateURL* Add(std::unique_ptr<TemplateURL> template_url,
                   bool newly_adding);

  // Updates |template_urls| so that the only "created by policy" entry is
  // |default_from_prefs|. |default_from_prefs| may be NULL if there is no
  // policy-defined DSE in effect.
  void UpdateProvidersCreatedByPolicy(
      OwnedTemplateURLVector* template_urls,
      const TemplateURLData* default_from_prefs);

  // Resets the sync GUID of the specified TemplateURL and persists the change
  // to the database. This does not notify observers.
  void ResetTemplateURLGUID(TemplateURL* url, const std::string& guid);

  // Attempts to generate a unique keyword for |turl| based on its original
  // keyword. If its keyword is already unique, that is returned. Otherwise, it
  // tries to return the autogenerated keyword if that is unique to the Service,
  // and finally it repeatedly appends special characters to the keyword until
  // it is unique to the Service. If |force| is true, then this will only
  // execute the special character appending functionality.
  base::string16 UniquifyKeyword(const TemplateURL& turl, bool force);

  // Returns true iff |local_turl| is considered "better" than |sync_turl| for
  // the purposes of resolving conflicts. |local_turl| must be a TemplateURL
  // known to the local model (though it may already be synced), and |sync_turl|
  // is a new TemplateURL known to Sync but not yet known to the local model.
  // The criteria for if |local_turl| is better than |sync_turl| is whether any
  // of the following are true:
  //  * |local_turl|'s last_modified timestamp is newer than sync_turl.
  //  * |local_turl| is created by policy.
  //  * |prefer_local_default| is true and |local_turl| is the local default
  //    search provider
  bool IsLocalTemplateURLBetter(const TemplateURL* local_turl,
                                const TemplateURL* sync_turl,
                                bool prefer_local_default = true) const;

  // Given two synced TemplateURLs with a conflicting keyword, one of which
  // needs to be added to or updated in the local model (|unapplied_sync_turl|)
  // and one which is already known to the local model (|applied_sync_turl|),
  // prepares the local model so that |unapplied_sync_turl| can be added to it,
  // or applied as an update to an existing TemplateURL.
  // Since both entries are known to Sync and one of their keywords will change,
  // an ACTION_UPDATE will be appended to |change_list| to reflect this change.
  // Note that |applied_sync_turl| must not be an extension keyword.
  void ResolveSyncKeywordConflict(TemplateURL* unapplied_sync_turl,
                                  TemplateURL* applied_sync_turl,
                                  syncer::SyncChangeList* change_list);

  // Adds |sync_turl| into the local model, possibly removing or updating a
  // local TemplateURL to make room for it. This expects |sync_turl| to be a new
  // entry from Sync, not currently known to the local model. |sync_data| should
  // be a SyncDataMap where the contents are entries initially known to Sync
  // during MergeDataAndStartSyncing.
  // Any necessary updates to Sync will be appended to |change_list|. This can
  // include updates on local TemplateURLs, if they are found in |sync_data|.
  // |initial_data| should be a SyncDataMap of the entries known to the local
  // model during MergeDataAndStartSyncing. If |sync_turl| replaces a local
  // entry, that entry is removed from |initial_data| to prevent it from being
  // sent up to Sync.
  // |merge_result| tracks the changes made to the local model. Added/modified/
  // deleted are updated depending on how the |sync_turl| is merged in.
  // This should only be called from MergeDataAndStartSyncing.
  void MergeInSyncTemplateURL(TemplateURL* sync_turl,
                              const SyncDataMap& sync_data,
                              syncer::SyncChangeList* change_list,
                              SyncDataMap* local_data,
                              syncer::SyncMergeResult* merge_result);

  // Goes through a vector of TemplateURLs and ensure that both the in-memory
  // and database copies have valid sync_guids. This is to fix crbug.com/102038,
  // where old entries were being pushed to Sync without a sync_guid.
  void PatchMissingSyncGUIDs(OwnedTemplateURLVector* template_urls);

  void OnSyncedDefaultSearchProviderGUIDChanged();

  // Adds to |matches| all TemplateURLs stored in |keyword_to_turl_and_length|
  // whose keywords begin with |prefix|, sorted shortest-keyword-first.  If
  // |supports_replacement_only| is true, only TemplateURLs that support
  // replacement are returned.
  template <typename Container>
  void AddMatchingKeywordsHelper(
      const Container& keyword_to_turl_and_length,
      const base::string16& prefix,
      bool supports_replacement_only,
      TURLsAndMeaningfulLengths* matches);

  // Returns the TemplateURL corresponding to |prepopulated_id|, if any.
  TemplateURL* FindPrepopulatedTemplateURL(int prepopulated_id);

  // Returns the TemplateURL associated with |extension_id|, if any.
  TemplateURL* FindTemplateURLForExtension(const std::string& extension_id,
                                           TemplateURL::Type type);

  // Finds any NORMAL_CONTROLLED_BY_EXTENSION engine that matches |data| and
  // wants to be default. Returns nullptr if not found.
  TemplateURL* FindMatchingDefaultExtensionTemplateURL(
      const TemplateURLData& data);

  // Returns whether |template_urls_| contains more than one normal engine with
  // same keyword. Used to validate state after search engines are
  // added/updated.
  bool HasDuplicateKeywords() const;

  // ---------- Browser state related members ---------------------------------
  PrefService* prefs_ = nullptr;

  std::unique_ptr<SearchTermsData> search_terms_data_ =
      std::make_unique<SearchTermsData>();

  // ---------- Dependencies on other components ------------------------------
  // Service used to store entries.
  scoped_refptr<KeywordWebDataService> web_data_service_ = nullptr;

  std::unique_ptr<TemplateURLServiceClient> client_;

  // ---------- Metrics related members ---------------------------------------
  rappor::RapporServiceImpl* rappor_service_ = nullptr;

  // This closure is run when the default search provider is set to Google.
  base::RepeatingClosure dsp_change_callback_;

  PrefChangeRegistrar pref_change_registrar_;

  // Mapping from keyword to the TemplateURL.
  KeywordToTURLAndMeaningfulLength keyword_to_turl_and_length_;

  // Mapping from keyword domain to the TemplateURL.
  // Entries are only allowed here if there is a corresponding entry in
  // |keyword_to_turl_and_length_|, i.e., if a template URL doesn't have an
  // entry in |keyword_to_turl_and_length_| because it's subsumed by another
  // template URL with an identical keyword, the template URL will not have an
  // entry in this map either.  This map will also not bother including entries
  // for keywords in which the keyword is the domain name, with no subdomain
  // before the domain name.  (The ordinary |keyword_to_turl_and_length|
  // suffices for that.)
  KeywordDomainToTURLAndMeaningfulLength keyword_domain_to_turl_and_length_;

  // Mapping from Sync GUIDs to the TemplateURL.
  GUIDToTURL guid_to_turl_;

  OwnedTemplateURLVector template_urls_;

  base::ObserverList<TemplateURLServiceObserver> model_observers_;

  // Maps from host to set of TemplateURLs whose search url host is host.
  std::unique_ptr<SearchHostToURLsMap> provider_map_ =
      std::make_unique<SearchHostToURLsMap>();

  // Whether the keywords have been loaded.
  bool loaded_ = false;

  // Set when the web data service fails to load properly.  This prevents
  // further communication with sync or writing to prefs, so we don't persist
  // inconsistent state data anywhere.
  bool load_failed_ = false;

  // Whether Load() is disabled. True only in testing contexts.
  bool disable_load_ = false;

  // If non-zero, we're waiting on a load.
  KeywordWebDataService::Handle load_handle_ = 0;

  // All visits that occurred before we finished loading. Once loaded
  // UpdateKeywordSearchTermsForURL is invoked for each element of the vector.
  std::vector<URLVisitedDetails> visits_to_add_;

  // Once loaded, the default search provider.  This is a pointer to a
  // TemplateURL owned by |template_urls_|.
  TemplateURL* default_search_provider_ = nullptr;

  // A temporary location for the DSE until Web Data has been loaded and it can
  // be merged into |template_urls_|.
  std::unique_ptr<TemplateURL> initial_default_search_provider_;

  // Source of the default search provider.
  DefaultSearchManager::Source default_search_provider_source_;

  // ID assigned to next TemplateURL added to this model. This is an ever
  // increasing integer that is initialized from the database.
  TemplateURLID next_id_ = kInvalidTemplateURLID + 1;

  // Used to retrieve the current time, in base::Time units.
  std::unique_ptr<base::Clock> clock_ = std::make_unique<base::DefaultClock>();

  // Do we have an active association between the TemplateURLs and sync models?
  // Set in MergeDataAndStartSyncing, reset in StopSyncing. While this is not
  // set, we ignore any local search engine changes (when we start syncing we
  // will look up the most recent values anyways).
  bool models_associated_ = false;

  // Whether we're currently processing changes from the syncer. While this is
  // true, we ignore any local search engine changes, since we triggered them.
  bool processing_syncer_changes_ = false;

  // Sync's syncer::SyncChange handler. We push all our changes through this.
  std::unique_ptr<syncer::SyncChangeProcessor> sync_processor_;

  // Sync's error handler. We use it to create a sync error.
  std::unique_ptr<syncer::SyncErrorFactory> sync_error_factory_;

  // A set of sync GUIDs denoting TemplateURLs that have been removed from this
  // model or the underlying KeywordWebDataService prior to
  // MergeDataAndStartSyncing.
  // This set is used to determine what entries from the server we want to
  // ignore locally and return a delete command for.
  std::set<std::string> pre_sync_deletes_;

  // This is used to log the origin of changes to the default search provider.
  // We set this value to increasingly specific values when we know what is the
  // cause/origin of a default search change.
  DefaultSearchChangeOrigin dsp_change_origin_ = DSP_CHANGE_OTHER;

  // Stores a list of callbacks to be run after TemplateURLService has loaded.
  base::CallbackList<void(void)> on_loaded_callbacks_;

  // Similar to |on_loaded_callbacks_| but used for WaitUntilReadyToSync().
  base::OnceClosure on_loaded_callback_for_sync_;

  // Helper class to manage the default search engine.
  DefaultSearchManager default_search_manager_;

  // This tracks how many Scoper handles exist. When the number of handles drops
  // to zero, a notification is made to observers if
  // |model_mutated_notification_pending_| is true.
  int outstanding_scoper_handles_ = 0;

  // Used to track if a notification is necessary due to the model being
  // mutated. The outermost Scoper handles, can be used to defer notifications,
  // but if no model mutation occurs, the deferred notification can be skipped.
  bool model_mutated_notification_pending_ = false;

#if defined(OS_ANDROID)
  // Manage and fetch the java object that wraps this TemplateURLService on
  // android.
  std::unique_ptr<TemplateUrlServiceAndroid> template_url_service_android_;
#endif

  DISALLOW_COPY_AND_ASSIGN(TemplateURLService);
};

#endif  // COMPONENTS_SEARCH_ENGINES_TEMPLATE_URL_SERVICE_H_
