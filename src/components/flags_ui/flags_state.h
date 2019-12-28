// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_FLAGS_UI_FLAGS_STATE_H_
#define COMPONENTS_FLAGS_UI_FLAGS_STATE_H_

#include <stddef.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/macros.h"

namespace base {
class ListValue;
}

namespace flags_ui {

// Internal functionality exposed for tests.
namespace internal {
// The trial group selected when feature variation parameters are registered via
// FlagsState::RegisterFeatureVariationParameters().
extern const char kTrialGroupAboutFlags[];
}  // namespace internal

struct FeatureEntry;
class FlagsStorage;
struct SwitchEntry;

// Enumeration of flag filters.
enum {
  kOsMac = 1 << 0,
  kOsWin = 1 << 1,
  kOsLinux = 1 << 2,
  kOsCrOS = 1 << 3,
  kOsAndroid = 1 << 4,
  kOsCrOSOwnerOnly = 1 << 5,
  kOsIos = 1 << 6,
  kDeprecated = 1 << 7,
  kOsFuchsia = 1 << 8,
};

// A flag controlling the behavior of the |ConvertFlagsToSwitches| function -
// whether it should add the sentinel switches around flags.
enum SentinelsMode { kNoSentinels, kAddSentinels };

// Differentiate between generic flags available on a per session base and flags
// that influence the whole machine and can be said by the admin only. This flag
// is relevant for ChromeOS for now only and dictates whether entries marked
// with the |kOsCrOSOwnerOnly| label should be enabled in the UI or not.
enum FlagAccess { kGeneralAccessFlagsOnly, kOwnerAccessToFlags };

// Stores and encapsulates the little state that about:flags has.
class FlagsState {
 public:
  // The |exclude_predicate| parameter is a predicate used to prevent flags from
  // actually applying, while retaining them in the list of feature entries.
  // This is used to implement flag expiration.
  FlagsState(
      const FeatureEntry* feature_entries,
      size_t num_feature_entries,
      base::RepeatingCallback<bool(const FeatureEntry&)> exclude_predicate);
  ~FlagsState();

  // Reads the state from |flags_storage| and adds the command line flags
  // belonging to the active feature entries to |command_line|. Features are
  // appended via |enable_features_flag_name| and |disable_features_flag_name|.
  void ConvertFlagsToSwitches(FlagsStorage* flags_storage,
                              base::CommandLine* command_line,
                              SentinelsMode sentinels,
                              const char* enable_features_flag_name,
                              const char* disable_features_flag_name);

  // Reads the state from |flags_storage| and fills |switches| with the set of
  // switches corresponding to enabled entries and |features| with the set of
  // strings corresponding to enabled/disabled base::Feature states. Feature
  // names are suffixed with ":enabled" or ":disabled" depending on their state.
  void GetSwitchesAndFeaturesFromFlags(FlagsStorage* flags_storage,
                                       std::set<std::string>* switches,
                                       std::set<std::string>* features) const;

  bool IsRestartNeededToCommitChanges();
  void SetFeatureEntryEnabled(FlagsStorage* flags_storage,
                              const std::string& internal_name,
                              bool enable);

  // Sets |value| as the command line switch for feature given by
  // |internal_name|. |value| contains a list of origins (serialized form of
  // url::Origin()) separated by whitespace and/or comma. Invalid values in this
  // list are ignored.
  void SetOriginListFlag(const std::string& internal_name,
                         const std::string& value,
                         FlagsStorage* flags_storage);

  void RemoveFlagsSwitches(base::CommandLine::SwitchMap* switch_list);
  void ResetAllFlags(FlagsStorage* flags_storage);
  void Reset();

  // Registers variations parameter values selected for features in about:flags.
  // The selected flags are retrieved from |flags_storage|, the registered
  // variation parameters are connected to their corresponding features in
  // |feature_list|. Returns the (possibly empty) comma separated list of
  // additional variation ids to register in the MetricsService that come from
  // variations selected using chrome://flags.
  std::vector<std::string> RegisterAllFeatureVariationParameters(
      FlagsStorage* flags_storage,
      base::FeatureList* feature_list);

  // Gets the list of feature entries. Entries that are available for the
  // current platform are appended to |supported_entries|; all other entries are
  // appended to |unsupported_entries|.
  void GetFlagFeatureEntries(
      FlagsStorage* flags_storage,
      FlagAccess access,
      base::ListValue* supported_entries,
      base::ListValue* unsupported_entries,
      base::Callback<bool(const FeatureEntry&)> skip_feature_entry);

  // Returns the value for the current platform. This is one of the values
  // defined by the OS enum above.
  // This is exposed only for testing.
  static int GetCurrentPlatform();

  // Compares a set of switches of the two provided command line objects and
  // returns true if they are the same and false otherwise.
  // If |out_difference| is not NULL, it's filled with set_symmetric_difference
  // between sets.
  // Only switches between --flag-switches-begin and --flag-switches-end are
  // compared. The embedder may use |extra_flag_sentinel_begin_flag_name| and
  // |extra_sentinel_end_flag_name| to specify other delimiters, if supported.
  static bool AreSwitchesIdenticalToCurrentCommandLine(
      const base::CommandLine& new_cmdline,
      const base::CommandLine& active_cmdline,
      std::set<base::CommandLine::StringType>* out_difference,
      const char* extra_flag_sentinel_begin_flag_name,
      const char* extra_flag_sentinel_end_flag_name);

 private:
  // Keeps track of affected switches for each FeatureEntry, based on which
  // choice is selected for it.
  struct SwitchEntry;

  // Adds mapping to |name_to_switch_map| to set the given switch name/value.
  void AddSwitchMapping(
      const std::string& key,
      const std::string& switch_name,
      const std::string& switch_value,
      std::map<std::string, SwitchEntry>* name_to_switch_map) const;

  // Adds mapping to |name_to_switch_map| to toggle base::Feature |feature_name|
  // to state |feature_state|.
  void AddFeatureMapping(
      const std::string& key,
      const std::string& feature_name,
      bool feature_state,
      std::map<std::string, SwitchEntry>* name_to_switch_map) const;

  // Updates the switches in |command_line| by applying the modifications
  // specified in |name_to_switch_map| for each entry in |enabled_entries|.
  // |enable_features_flag_name| and |disable_features_flag_name| are switches
  // used by the embedder to enable/disable features respectively if supported.
  void AddSwitchesToCommandLine(
      const std::set<std::string>& enabled_entries,
      const std::map<std::string, SwitchEntry>& name_to_switch_map,
      SentinelsMode sentinels,
      base::CommandLine* command_line,
      const char* enable_features_flag_name,
      const char* disable_features_flag_name);

  // Updates |command_line| by merging the value of the --enable-features= or
  // --disable-features= list (per the |switch_name| param) with corresponding
  // entries in |feature_switches| that have value |feature_state|. Keeps track
  // of the changes by updating |appended_switches|.
  void MergeFeatureCommandLineSwitch(
      const std::map<std::string, bool>& feature_switches,
      const char* switch_name,
      bool feature_state,
      base::CommandLine* command_line);

  // Sanitizes |enabled_entries| to only contain entries that are defined in the
  // |feature_entries_| and whose |supported_platforms| matches |platform_mask|.
  // Pass -1 to |platform_mask| to not do platform filtering.
  std::set<std::string> SanitizeList(
      const std::set<std::string>& enabled_entries,
      int platform_mask) const;

  // Gets sanitized entries from |flags_storage|, filtering out any entries that
  // don't exist in |feature_entries_|, and updates |flags_storage|.
  void GetSanitizedEnabledFlags(FlagsStorage* flags_storage,
                                std::set<std::string>* result) const;

  // Variant of GetSanitizedEnabledFlags that also removes any flags that aren't
  // enabled on the current platform.
  void GetSanitizedEnabledFlagsForCurrentPlatform(
      FlagsStorage* flags_storage,
      std::set<std::string>* result) const;

  // Generates a flags to switches mapping based on the set of enabled flags
  // from |flags_storage|. On output, |enabled_entries| will contain the
  // internal names of enabled flags and |name_to_switch_map| will contain
  // information on how they map to command-line flags or features.
  void GenerateFlagsToSwitchesMapping(
      FlagsStorage* flags_storage,
      std::set<std::string>* enabled_entries,
      std::map<std::string, SwitchEntry>* name_to_switch_map) const;

  // Returns the FeatureEntry named |internal_name|. Returns null if no entry is
  // matched.
  const FeatureEntry* FindFeatureEntryByName(
      const std::string& internal_name) const;

  // Returns whether there is a FeatureEntry named by |name| in
  // |feature_entries_| that:
  // a) Is supported on this |platform_mask|, and
  // b) Is not excluded by |exclude_predicate_|, if it is set (i.e. for which
  //    |exclude_predicate_| returns false).
  bool IsSupportedFeature(const std::string& name, int platform_mask) const;

  const FeatureEntry* feature_entries_;
  size_t num_feature_entries_;

  bool needs_restart_;
  std::map<std::string, std::string> flags_switches_;

  // Map from switch name to a set of string, that keeps track which strings
  // were appended to existing (list value) switches.
  std::map<std::string, std::set<std::string>> appended_switches_;

  // Used as a predicate to exclude FeatureEntries from applying to
  // switches or base::Features; those for which this predicate returns true
  // will not have any effect.
  base::RepeatingCallback<bool(const FeatureEntry&)> exclude_predicate_;

  DISALLOW_COPY_AND_ASSIGN(FlagsState);
};

}  // namespace flags_ui

#endif  // COMPONENTS_FLAGS_UI_FLAGS_STATE_H_
