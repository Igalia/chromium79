// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

Polymer({
  is: 'discover-set-wallpaper-card',

  behaviors: [DiscoverModuleBehavior],

  onClick_: function() {
    this.discoverCall('discover.setWallpaper.launchWallpaperPicker');
  },
});
