// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileNotFoundException;
import java.security.SecureRandom;
import java.util.HashMap;
import java.util.Map;

/**
 * The game file an engine app was started with, read-only, as a content URI. Not exported:
 * only an app the URI was granted to (FLAG_GRANT_READ_URI_PERMISSION on the PLAY intent)
 * can open it, and only the files handed out here, by a random name.
 */
public final class GameProvider extends ContentProvider {
    private static final Map<String, File> FILES = new HashMap<>();
    private static final SecureRandom RANDOM = new SecureRandom();

    static Uri uriFor(Context context, File file) {
        byte[] bytes = new byte[16];
        RANDOM.nextBytes(bytes);
        StringBuilder key = new StringBuilder();
        for (byte b : bytes) {
            key.append(String.format("%02x", b));
        }
        synchronized (FILES) {
            FILES.put(key.toString(), file);
        }
        return new Uri.Builder().scheme("content").authority(context.getPackageName() + ".games")
                .appendPath(key.toString()).appendPath(file.getName()).build();
    }

    private static File fileOf(Uri uri) throws FileNotFoundException {
        if (uri.getPathSegments().isEmpty()) {
            throw new FileNotFoundException(uri.toString());
        }
        File file;
        synchronized (FILES) {
            file = FILES.get(uri.getPathSegments().get(0));
        }
        if (file == null || !file.isFile()) {
            throw new FileNotFoundException(uri.toString());
        }
        return file;
    }

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        if (!"r".equals(mode)) {
            throw new FileNotFoundException("read only");
        }
        return ParcelFileDescriptor.open(fileOf(uri), ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] args, String order) {
        File file;
        try {
            file = fileOf(uri);
        } catch (FileNotFoundException e) {
            return null;
        }
        String[] columns = projection != null ? projection : new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE};
        MatrixCursor cursor = new MatrixCursor(columns, 1);
        Object[] row = new Object[columns.length];
        for (int i = 0; i < columns.length; ++i) {
            if (OpenableColumns.DISPLAY_NAME.equals(columns[i])) {
                row[i] = file.getName();
            } else if (OpenableColumns.SIZE.equals(columns[i])) {
                row[i] = file.length();
            }
        }
        cursor.addRow(row);
        return cursor;
    }

    @Override
    public String getType(Uri uri) {
        return "application/octet-stream";
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        throw new UnsupportedOperationException();
    }

    @Override
    public int delete(Uri uri, String selection, String[] args) {
        throw new UnsupportedOperationException();
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] args) {
        throw new UnsupportedOperationException();
    }
}
