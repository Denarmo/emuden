// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.Settings;
import android.system.Os;
import android.util.Log;
import android.widget.Toast;

import java.io.File;

/**
 * How the app gets at the user's games. They are read by path, which needs "all files
 * access" on Android 11 and later. When that is refused the user can pick one folder
 * instead; a picked folder cannot be read by path, so its games are copied into the app's
 * own folder on shared storage (FolderImport).
 */
final class StorageAccess {
    private static final String TAG = "EmuDEN";
    static final int REQUEST_FOLDER = 0x4401;
    private static final int REQUEST_LEGACY = 0x4402;

    private final Activity activity;
    private boolean sentToSettings;
    private AlertDialog dialog;

    StorageAccess(Activity activity) {
        this.activity = activity;
    }

    boolean allFiles() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            return Environment.isExternalStorageManager();
        }
        return activity.checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)
                == PackageManager.PERMISSION_GRANTED;
    }

    /** Where copies of picked folders live; readable by path without any permission. */
    File importRoot() {
        File base = activity.getExternalFilesDir(null);
        return new File(base != null ? base : activity.getFilesDir(), "Games");
    }

    private boolean hasImports() {
        String[] names = importRoot().list();
        return names != null && names.length > 0;
    }

    /** HOME is where the app's folder browser starts. */
    void setHome() {
        File home = allFiles() ? Environment.getExternalStorageDirectory() : importRoot();
        try {
            if (!home.isDirectory()) {
                home.mkdirs();
            }
            Os.setenv("HOME", home.getAbsolutePath(), true);
        } catch (Exception e) {
            Log.w(TAG, "HOME could not be set", e);
        }
    }

    /** At every start until access is granted or a folder has been copied. */
    void askAtStart() {
        if (allFiles() || hasImports()) {
            return;
        }
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            activity.requestPermissions(new String[] {Manifest.permission.READ_EXTERNAL_STORAGE,
                    Manifest.permission.WRITE_EXTERNAL_STORAGE}, REQUEST_LEGACY);
            return;
        }
        show(activity.getString(R.string.storage_message), true);
    }

    /** The activity is in front again, maybe back from the settings screen. */
    void onResume() {
        if (!sentToSettings) {
            return;
        }
        sentToSettings = false;
        if (allFiles()) {
            setHome();
            Toast.makeText(activity, R.string.storage_allowed, Toast.LENGTH_LONG).show();
        } else {
            show(activity.getString(R.string.storage_refused), false);
        }
    }

    /** True when the result was for a request made here. */
    boolean onPermissionsResult(int requestCode) {
        if (requestCode != REQUEST_LEGACY) {
            return false;
        }
        setHome();
        return true;
    }

    void onFolderPicked(int resultCode, Intent data) {
        Uri tree = resultCode == Activity.RESULT_OK && data != null ? data.getData() : null;
        if (tree == null) {
            return;
        }
        try {
            activity.getContentResolver().takePersistableUriPermission(tree,
                    Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (Exception e) {
            Log.w(TAG, "the folder grant is for this run only", e);
        }
        new FolderImport(activity, tree, importRoot()).start();
    }

    private void show(String message, boolean offerAllFiles) {
        if (dialog != null && dialog.isShowing()) {
            return;
        }
        AlertDialog.Builder builder = new AlertDialog.Builder(activity)
                .setTitle(R.string.storage_title)
                .setMessage(message)
                .setNeutralButton(R.string.storage_folder, (d, which) -> pickFolder())
                .setNegativeButton(R.string.storage_later, null);
        if (offerAllFiles) {
            builder.setPositiveButton(R.string.storage_allow, (d, which) -> openSettings());
        }
        dialog = builder.show();
    }

    private void openSettings() {
        sentToSettings = true;
        try {
            activity.startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + activity.getPackageName())));
        } catch (Exception e) {
            try {
                activity.startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            } catch (Exception inner) {
                Log.w(TAG, "this device has no all-files-access screen", inner);
                sentToSettings = false;
                pickFolder();
            }
        }
    }

    private void pickFolder() {
        try {
            activity.startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)
                    .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                            | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION), REQUEST_FOLDER);
        } catch (Exception e) {
            Log.w(TAG, "this device has no folder picker", e);
            Toast.makeText(activity, R.string.storage_no_picker, Toast.LENGTH_LONG).show();
        }
    }
}
