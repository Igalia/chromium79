// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_EXTENSIONS_TEST_EXTERNAL_CACHE_H_
#define CHROME_BROWSER_CHROMEOS_EXTENSIONS_TEST_EXTERNAL_CACHE_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/macros.h"
#include "base/values.h"
#include "chrome/browser/chromeos/extensions/external_cache.h"

namespace chromeos {

class ExternalCacheDelegate;

// External cache implementation to be used in tests - instead of using a
// real local extension cache, which saves cached extension data to disk, it
// keeps cached extension CRX info in memory.
class TestExternalCache : public ExternalCache {
 public:
  TestExternalCache(ExternalCacheDelegate* delegate,
                    bool always_check_for_updates);
  ~TestExternalCache() override;

  // ExternalCache:
  const base::DictionaryValue* GetCachedExtensions() override;
  void Shutdown(base::OnceClosure callback) override;
  void UpdateExtensionsList(
      std::unique_ptr<base::DictionaryValue> prefs) override;
  void OnDamagedFileDetected(const base::FilePath& path) override;
  void RemoveExtensions(const std::vector<std::string>& ids) override;
  bool GetExtension(const std::string& id,
                    base::FilePath* file_path,
                    std::string* version) override;
  bool ExtensionFetchPending(const std::string& id) override;
  void PutExternalExtension(const std::string& id,
                            const base::FilePath& crx_file_path,
                            const std::string& version,
                            PutExternalExtensionCallback callback) override;

  // Simulates extension CRX download succeeding - it adds the extension
  // information to |cache_|.
  // |id| - the "downloaded" extension ID.
  // |crx_path| - the path to which the CRX is "downloaded".
  // |version| - the "downloaded" extension version.
  // Returns whether the extension information has actually been saved. This
  // will return false if the cache does not think that the extension's
  // downaload is pending.
  bool SimulateExtensionDownloadFinished(const std::string& id,
                                         const std::string& crx_path,
                                         const std::string& version);

  // Simulates the extension's download failure. This will remove the extension
  // info from the set of extensions tracked in this external cache.
  // Returns whether the extension information has actually been changed. This
  // will return false if the cache does not think that the extension's
  // downaload is pending.
  bool SimulateExtensionDownloadFailed(const std::string& id);

  // Set of the extension IDs in download pending state. Note that |this| does
  // not actually initiate the download, the user is expected to call either
  // SimulateExtensionDownloadFinished(), or SimulateExtensionDownloadFailed()
  // in order to remove an extension from this state and update the extension's
  // CRX information tracked by the external cache.
  const std::set<std::string>& pending_downloads() const {
    return pending_downloads_;
  }

 private:
  // An extension CRX information tracked by |this|.
  struct CrxCacheEntry {
    // The extension CRX path.
    std::string path;

    // The extension version associtated with the extension CRX.
    std::string version;
  };

  // Updates locally tracked state - called when the set of
  // |configured_extensions_| changes. It updates |cached_extensions_| and
  // adds extensions to |pending_downloads_| as needed.
  void UpdateCachedExtensions();

  // Adds an extension's CRX information to |crx_cache_|.
  void AddEntryToCrxCache(const std::string& id,
                          const std::string& crx_path,
                          const std::string& version);

  ExternalCacheDelegate* const delegate_;
  const bool always_check_for_updates_;

  std::unique_ptr<base::DictionaryValue> configured_extensions_;
  base::DictionaryValue cached_extensions_;

  std::set<std::string> pending_downloads_;
  std::map<std::string, CrxCacheEntry> crx_cache_;

  DISALLOW_COPY_AND_ASSIGN(TestExternalCache);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_EXTENSIONS_TEST_EXTERNAL_CACHE_H_
