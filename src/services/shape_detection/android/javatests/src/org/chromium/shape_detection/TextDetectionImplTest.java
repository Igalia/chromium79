// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.shape_detection;

import androidx.test.filters.SmallTest;

import org.junit.Assert;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseJUnit4ClassRunner;
import org.chromium.base.test.util.Feature;
import org.chromium.gfx.mojom.RectF;
import org.chromium.shape_detection.mojom.TextDetection;
import org.chromium.shape_detection.mojom.TextDetectionResult;

import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.TimeUnit;

/**
 * Test suite for TextDetectionImpl.
 */
@RunWith(BaseJUnit4ClassRunner.class)
public class TextDetectionImplTest {
    private static final String[] DETECTION_EXPECTED_TEXT = {
            "The quick brown fox jumped over the lazy dog.", "Helvetica Neue 36."};
    private static final float[][] TEXT_BOUNDING_BOX = {
            {0.0f, 71.0f, 753.0f, 36.0f}, {4.0f, 173.0f, 307.0f, 28.0f}};
    private static final org.chromium.skia.mojom.Bitmap TEXT_DETECTION_BITMAP =
            TestUtils.mojoBitmapFromText(DETECTION_EXPECTED_TEXT);

    private static TextDetectionResult[] detect(org.chromium.skia.mojom.Bitmap mojoBitmap) {
        TextDetection detector = new TextDetectionImpl();

        final ArrayBlockingQueue<TextDetectionResult[]> queue = new ArrayBlockingQueue<>(1);
        detector.detect(mojoBitmap, new TextDetection.DetectResponse() {
            @Override
            public void call(TextDetectionResult[] results) {
                queue.add(results);
            }
        });
        TextDetectionResult[] toReturn = null;
        try {
            toReturn = queue.poll(5L, TimeUnit.SECONDS);
        } catch (InterruptedException e) {
            Assert.fail("Could not get TextDetectionResult: " + e.toString());
        }
        Assert.assertNotNull(toReturn);
        return toReturn;
    }

    @Test
    @SmallTest
    @Feature({"ShapeDetection"})
    public void testDetectSucceedsOnValidBitmap() {
        if (!TestUtils.IS_GMS_CORE_SUPPORTED) {
            return;
        }
        TextDetectionResult[] results = detect(TEXT_DETECTION_BITMAP);
        Assert.assertEquals(DETECTION_EXPECTED_TEXT.length, results.length);

        for (int i = 0; i < DETECTION_EXPECTED_TEXT.length; i++) {
            Assert.assertEquals(results[i].rawValue, DETECTION_EXPECTED_TEXT[i]);
            Assert.assertEquals(TEXT_BOUNDING_BOX[i][0], results[i].boundingBox.x, 0.0);
            Assert.assertEquals(TEXT_BOUNDING_BOX[i][1], results[i].boundingBox.y, 0.0);
            Assert.assertEquals(TEXT_BOUNDING_BOX[i][2], results[i].boundingBox.width, 0.0);
            Assert.assertEquals(TEXT_BOUNDING_BOX[i][3], results[i].boundingBox.height, 0.0);

            RectF cornerRectF = new RectF();
            cornerRectF.x = results[i].cornerPoints[0].x;
            cornerRectF.y = results[i].cornerPoints[0].y;
            cornerRectF.width = results[i].cornerPoints[1].x - cornerRectF.x;
            cornerRectF.height = results[i].cornerPoints[2].y - cornerRectF.y;
            Assert.assertEquals(results[i].boundingBox, cornerRectF);
            Assert.assertEquals(results[i].cornerPoints[3].x, results[i].cornerPoints[1].x, 0.0);
            Assert.assertEquals(results[i].cornerPoints[3].y, results[i].cornerPoints[2].y, 0.0);
        }
    }
}
