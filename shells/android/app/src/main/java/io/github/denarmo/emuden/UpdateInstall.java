// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Intent;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.Build;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.util.Log;

import java.io.File;
import java.io.FileNotFoundException;
import java.security.SecureRandom;

/**
 * An update of EmuDEN itself (platform/src/android/self_update_android.cpp): the APK the app
 * downloaded and checked goes to the system's package installer as a content URI, and the user
 * confirms the install there. Nothing is installed without that.
 */
public final class UpdateInstall {
    private static final String TAG = "EmuDEN";
    static final String APK_TYPE = "application/vnd.android.package-archive";

    private UpdateInstall() {}

    static String install(Activity activity, String path) {
        File apk = new File(path);
        if (!apk.isFile()) {
            return "error:The download is missing";
        }
        if (Build.VERSION.SDK_INT >= 26 && !activity.getPackageManager().canRequestPackageInstalls()) {
            // The user allows EmuDEN to install apps first; the next try goes on from there.
            try {
                activity.startActivity(new Intent(android.provider.Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                        Uri.parse("package:" + activity.getPackageName())));
            } catch (RuntimeException e) {
                Log.w(TAG, "no settings page for installing apps", e);
            }
            return "error:Allow EmuDEN to install apps, then choose Download and install again";
        }
        Uri uri = Provider.share(activity, apk);
        @SuppressWarnings("deprecation")
        Intent install = new Intent(Intent.ACTION_INSTALL_PACKAGE).setDataAndType(uri, APK_TYPE);
        install.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
        install.putExtra(Intent.EXTRA_RETURN_RESULT, false);
        try {
            activity.startActivity(install);
            return "";
        } catch (ActivityNotFoundException e) {
            // A system without that action still opens APKs it is shown.
            Intent view = new Intent(Intent.ACTION_VIEW).setDataAndType(uri, APK_TYPE);
            view.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
            try {
                activity.startActivity(view);
                return "";
            } catch (RuntimeException again) {
                Log.w(TAG, "no package installer", again);
                return "error:The system has no package installer";
            }
        } catch (RuntimeException e) {
            Log.w(TAG, "the installer could not be started", e);
            return "error:The installer could not be started";
        }
    }

    /**
     * The one APK handed to the installer, read-only, under a random name. Not exported: only
     * the app the URI was granted to can open it.
     */
    public static final class Provider extends ContentProvider {
        private static final SecureRandom RANDOM = new SecureRandom();
        private static String key;
        private static File file;

        static synchronized Uri share(Activity activity, File apk) {
            byte[] bytes = new byte[16];
            RANDOM.nextBytes(bytes);
            StringBuilder name = new StringBuilder();
            for (byte b : bytes) {
                name.append(String.format("%02x", b));
            }
            key = name.toString();
            file = apk;
            return new Uri.Builder().scheme("content").authority(activity.getPackageName() + ".updates")
                    .appendPath(key).appendPath(apk.getName()).build();
        }

        private static synchronized File fileOf(Uri uri) throws FileNotFoundException {
            if (key == null || uri.getPathSegments().isEmpty() || !key.equals(uri.getPathSegments().get(0))
                    || file == null || !file.isFile()) {
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
            File apk;
            try {
                apk = fileOf(uri);
            } catch (FileNotFoundException e) {
                return null;
            }
            String[] columns = projection != null ? projection
                                                  : new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE};
            MatrixCursor cursor = new MatrixCursor(columns, 1);
            Object[] row = new Object[columns.length];
            for (int i = 0; i < columns.length; ++i) {
                if (OpenableColumns.DISPLAY_NAME.equals(columns[i])) {
                    row[i] = apk.getName();
                } else if (OpenableColumns.SIZE.equals(columns[i])) {
                    row[i] = apk.length();
                }
            }
            cursor.addRow(row);
            return cursor;
        }

        @Override
        public String getType(Uri uri) {
            return APK_TYPE;
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
}
