// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef COMPONENTS_SIGNIN_CORE_BROWSER_COOKIE_REMINTER_H_
#define COMPONENTS_SIGNIN_CORE_BROWSER_COOKIE_REMINTER_H_

#include <vector>

#include "components/signin/public/identity_manager/identity_manager.h"

// Stores accounts with invalid cookies, which cannot be detected by
// /ListAccounts, so we can force cookie reminting when account is
// reauthenticated.
//
// Subscribed to |OnRefreshTokenUpdatedForAccount| of IdentityManager, calls
// |AccountsCookieMutator::LogOutAllAccounts| after refresh token update of
// any of the accounts that have been added to
// |ForceCookieRemintingOnNextTokenUpdate|.
class CookieReminter : public signin::IdentityManager::Observer {
 public:
  explicit CookieReminter(signin::IdentityManager* identity_manager);
  ~CookieReminter() override;

  // Forces a cookie reminting if/when the refresh token for |account_info| is
  // updated.
  void ForceCookieRemintingOnNextTokenUpdate(
      const CoreAccountInfo& account_info);

  // If there are accounts that require cookie reminting, calls
  // |AccountsCookieMutator::LogOutAllAccounts| and returns true. Otherwise
  // returns false.
  bool RemintCookieIfRequired();

  // Overridden from signin::IdentityManager::Observer.
  void OnRefreshTokenUpdatedForAccount(
      const CoreAccountInfo& account_info) override;

 private:
  bool is_forced_cookie_reminting_required_ = false;
  signin::IdentityManager* identity_manager_;
  std::vector<CoreAccountInfo> accounts_requiring_cookie_remint_;
};

#endif  // COMPONENTS_SIGNIN_CORE_BROWSER_COOKIE_REMINTER_H_
