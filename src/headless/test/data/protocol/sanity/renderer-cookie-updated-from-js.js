// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(async function(testRunner) {
  let {page, session, dp} = await testRunner.startWithFrameControl(
      'Tests renderer: cookie updated from js.');

  let RendererTestHelper =
      await testRunner.loadScript('../helpers/renderer-test-helper.js');
  let {httpInterceptor, frameNavigationHelper, virtualTimeController} =
      await (new RendererTestHelper(testRunner, dp, page)).init();

  httpInterceptor.addResponse(
      `http://www.example.com/`,
      `<html>
        <head>
          <script>
            let x = document.cookie;
            document.cookie = x + 'baz';
            document.title = document.cookie;
          </script>
       </head>
      <body>Pass</body>
      </html>`);

  await dp.Network.setCookie({url: 'http://www.example.com/',
      name: 'foo', value: 'bar'});

  await virtualTimeController.grantInitialTime(5000, 1000,
    null,
    async () => {
      testRunner.log(await session.evaluate('document.title'));
      testRunner.log(await session.evaluate('document.body.innerText'));
      testRunner.completeTest();
    }
  );

  await frameNavigationHelper.navigate('http://www.example.com/');
})
