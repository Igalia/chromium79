// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(async function(testRunner) {
  var {page, session, dp} = await testRunner.startBlank(
      `Tests virtual time basics.`);
  dp.Emulation.onVirtualTimeBudgetExpired(data => testRunner.completeTest());

  await dp.Emulation.setVirtualTimePolicy({policy: 'pause'});
  await dp.Emulation.setVirtualTimePolicy({
      policy: 'pauseIfNetworkFetchesPending',
      budget: 1000, waitForNavigation: true});
  dp.Page.navigate({url: testRunner.url('/resources/blank.html')});
})
