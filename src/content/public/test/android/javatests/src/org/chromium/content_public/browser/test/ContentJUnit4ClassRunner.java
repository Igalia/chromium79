// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content_public.browser.test;

import android.support.test.InstrumentationRegistry;

import org.junit.runners.model.InitializationError;

import org.chromium.base.CommandLineInitUtil;
import org.chromium.base.test.BaseJUnit4ClassRunner;
import org.chromium.base.test.BaseTestResult.PreTestHook;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.SkipCheck;
import org.chromium.ui.test.util.UiDisableIfSkipCheck;
import org.chromium.ui.test.util.UiRestrictionSkipCheck;

import java.util.List;

/**
 * A custom runner for //content JUnit4 tests.
 */
public class ContentJUnit4ClassRunner extends BaseJUnit4ClassRunner {
    /**
     * Create a ContentJUnit4ClassRunner to run {@code klass} and initialize values
     *
     * @throws InitializationError if the test class malformed
     */
    public ContentJUnit4ClassRunner(final Class<?> klass) throws InitializationError {
        super(klass);
    }

    @Override
    protected List<SkipCheck> getSkipChecks() {
        return addToList(super.getSkipChecks(),
                new UiRestrictionSkipCheck(InstrumentationRegistry.getTargetContext()),
                new UiDisableIfSkipCheck(InstrumentationRegistry.getTargetContext()));
    }
    /**
     * Change this static function to add default {@code PreTestHook}s.
     */
    @Override
    protected List<PreTestHook> getPreTestHooks() {
        return addToList(super.getPreTestHooks(), CommandLineFlags.getRegistrationHook(),
                new ChildProcessAllocatorSettingsHook());
    }

    @Override
    protected void initCommandLineForTest() {
        CommandLineInitUtil.initCommandLine(CommandLineFlags.getTestCmdLineFile());
    }
}
