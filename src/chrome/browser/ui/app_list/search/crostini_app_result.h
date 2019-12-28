// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_APP_LIST_SEARCH_CROSTINI_APP_RESULT_H_
#define CHROME_BROWSER_UI_APP_LIST_SEARCH_CROSTINI_APP_RESULT_H_

#include <memory>
#include <string>

#include "ash/public/cpp/app_list/app_list_metrics.h"
#include "base/macros.h"
#include "chrome/browser/ui/app_list/search/app_result.h"

class CrostiniAppContextMenu;

namespace app_list {

// Result of CrostiniSearchProvider.
class CrostiniAppResult : public AppResult {
 public:
  CrostiniAppResult(Profile* profile,
                    const std::string& app_id,
                    AppListControllerDelegate* controller,
                    bool is_recommendation);

  ~CrostiniAppResult() override;

  // ChromeSearchResult overrides:
  void Open(int event_flags) override;
  void GetContextMenuModel(GetMenuModelCallback callback) override;
  void ExecuteLaunchCommand(int event_flags) override;
  ash::SearchResultType GetSearchResultType() const override;

 private:
  // ChromeSearchResult overrides:
  AppContextMenu* GetAppContextMenu() override;

  std::unique_ptr<CrostiniAppContextMenu> context_menu_;

  DISALLOW_COPY_AND_ASSIGN(CrostiniAppResult);
};

}  // namespace app_list

#endif  // CHROME_BROWSER_UI_APP_LIST_SEARCH_CROSTINI_APP_RESULT_H_
