// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.util;

import android.content.Context;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.MediaStore;
import android.support.v4.content.FileProvider;

import java.io.File;
import java.io.FileNotFoundException;
import java.util.Arrays;

/**
 * A file provider class that can share a potentially non-existent file and blocks the client
 * application from accessing the file till it is written.
 *
 * This class serves as the default file provider, but also lets us share blocked files.
 * For blocked files, it generates an unique identifier for the file to be shared and the embedder
 * must write the file and notify that the file is ready using the unique uri generated. The client
 * application is blocked from accessing the file till the file is ready. This provider allows only
 * one blocked file to be shared at a given time.
 */
public class ChromeFileProvider extends FileProvider {
    private static final String AUTHORITY_SUFFIX = ".FileProvider";
    private static final String BLOCKED_FILE_PREFIX = "BlockedFile_";

    // All these static objects must be accesseed in a synchronized block:
    private static Object sLock = new Object();
    private static boolean sIsFileReady;
    private static Uri sCurrentBlockingUri;
    private static Uri sFileUri;

    /**
     * Returns an unique uri to identify the file to be shared and block access to it till
     * notifyFileReady is called.
     *
     * This function clobbers any uri that was previously created and the client application
     * accessing those uri will get a null file descriptor.
     * @param context Activity context that is used to access package manager.
     */
    public static Uri generateUriAndBlockAccess(final Context context) {
        String authority = getAuthority(context);
        String fileName = BLOCKED_FILE_PREFIX + String.valueOf(System.nanoTime());
        Uri blockingUri = new Uri.Builder()
                                  .scheme(UrlConstants.CONTENT_SCHEME)
                                  .authority(authority)
                                  .path(fileName)
                                  .build();
        synchronized (sLock) {
            sCurrentBlockingUri = blockingUri;
            sFileUri = null;
            sIsFileReady = false;
            // In case the previous file never got ready.
            sLock.notify();
        }
        return blockingUri;
    }

    /**
     * Returns an unique uri to identify the file to be shared.
     *
     * @param context Activity context that is used to access package manager.
     * @param file File for which the Uri is generated.
     */
    public static Uri generateUri(final Context context, File file)
            throws IllegalArgumentException {
        return getUriForFile(context, getAuthority(context), file);
    }

    /**
     * Notify that the file is ready to be accessed by the client application.
     *
     * @param blockingUri The unique uri that was generated by generateUriAndBlockAccess.
     * @param fileUri The Uri for actual file given by FileProvider.
     */
    public static void notifyFileReady(Uri blockingUri, Uri fileUri) {
        synchronized (sLock) {
            sFileUri = fileUri;
            // Ready is set only if the current file is ready.
            sIsFileReady = doesMatchCurrentBlockingUri(blockingUri);
            sLock.notify();
        }
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        Uri fileUri = getFileUriWhenReady(uri);
        return fileUri != null ? super.openFile(fileUri, mode) : null;
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs,
            String sortOrder) {
        Uri fileUri = getFileUriWhenReady(uri);
        if (fileUri == null) return null;

        // Workaround for a bad assumption that particular MediaStore columns exist by certain third
        // party applications.
        // http://crbug.com/467423.
        Cursor source = super.query(fileUri, projection, selection, selectionArgs, sortOrder);

        String[] columnNames = source.getColumnNames();
        String[] newColumnNames = columnNamesWithData(columnNames);
        if (columnNames == newColumnNames) return source;

        MatrixCursor cursor = new MatrixCursor(newColumnNames, source.getCount());

        source.moveToPosition(-1);
        while (source.moveToNext()) {
            MatrixCursor.RowBuilder row = cursor.newRow();
            for (int i = 0; i < columnNames.length; i++) {
                switch (source.getType(i)) {
                    case Cursor.FIELD_TYPE_INTEGER:
                        row.add(source.getInt(i));
                        break;
                    case Cursor.FIELD_TYPE_FLOAT:
                        row.add(source.getFloat(i));
                        break;
                    case Cursor.FIELD_TYPE_STRING:
                        row.add(source.getString(i));
                        break;
                    case Cursor.FIELD_TYPE_BLOB:
                        row.add(source.getBlob(i));
                        break;
                    case Cursor.FIELD_TYPE_NULL:
                    default:
                        row.add(null);
                        break;
                }
            }
        }

        source.close();
        return cursor;
    }

    @Override
    public String getType(Uri uri) {
        Uri fileUri = getFileUriWhenReady(uri);
        return fileUri != null ? super.getType(fileUri) : null;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        if (uri != null && uri.getPath().contains(BLOCKED_FILE_PREFIX)) {
            synchronized (sLock) {
                if (!doesMatchCurrentBlockingUri(uri)) return 0;
                sFileUri = null;
                sIsFileReady = false;
                sCurrentBlockingUri = null;
            }
        }
        return super.delete(uri, selection, selectionArgs);
    }

    /**
     * Waits and returns file uri iff the file is ready to be accessed, or returns null if file is
     * replaced.
     */
    protected static Uri getFileUriWhenReady(Uri uri) {
        // If the uri passed is not a blocked file, then the given uri can be directly used.
        if (uri == null || !uri.getPath().contains(BLOCKED_FILE_PREFIX)) return uri;

        synchronized (sLock) {
            // Wait only if the file is not ready and the current file has not changed.
            while (!sIsFileReady && doesMatchCurrentBlockingUri(uri)) {
                try {
                    sLock.wait();
                } catch (InterruptedException e) {
                    break;
                }
            }
            // If the current file has changed while waiting, return null.
            if (doesMatchCurrentBlockingUri(uri)) return sFileUri;
        }
        return null;
    }

    /**
     * Gets the authority string for content URI generation.
     * @param context Activity context that is used to access package manager.
     */
    private static String getAuthority(Context context) {
        return context.getPackageName() + AUTHORITY_SUFFIX;
    }

    private static boolean doesMatchCurrentBlockingUri(Uri uri) {
        return uri != null && sCurrentBlockingUri != null && sCurrentBlockingUri.equals(uri);
    }

    private String[] columnNamesWithData(String[] columnNames) {
        for (String columnName : columnNames) {
            if (MediaStore.MediaColumns.DATA.equals(columnName)) return columnNames;
        }

        String[] newColumnNames = Arrays.copyOf(columnNames, columnNames.length + 1);
        newColumnNames[columnNames.length] = MediaStore.MediaColumns.DATA;
        return newColumnNames;
    }
}
