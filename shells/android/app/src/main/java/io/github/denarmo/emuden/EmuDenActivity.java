// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.system.Os;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/** The one activity: SDL's, plus what the native platform layer asks of Android. */
public class EmuDenActivity extends SDLActivity {
    private static final String TAG = "EmuDEN";
    private static final String ASSET_ROOT = "emuden";

    @Override
    protected String[] getLibraries() {
        // SDL is linked into libmain.so.
        return new String[] {"main"};
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        try {
            // The folder browser starts at HOME.
            Os.setenv("HOME", Environment.getExternalStorageDirectory().getAbsolutePath(), true);
        } catch (Exception e) {
            Log.w(TAG, "HOME could not be set", e);
        }
        unpackAssets();
        super.onCreate(savedInstanceState);
        askForStorage();
    }

    /** Called from native code (platform/src/android). */
    public static HttpCall httpOpen(String url, String[] headers, int timeoutSeconds) {
        return HttpCall.open(url, headers, timeoutSeconds);
    }

    // The native code reads its assets as files: <files>/assets mirrors the APK's assets.
    private void unpackAssets() {
        File target = new File(getFilesDir(), "assets");
        File stampFile = new File(target, ".stamp");
        try {
            PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
            String stamp = info.versionName + ":" + info.lastUpdateTime;
            if (stampFile.isFile()
                    && stamp.equals(new String(Files.readAllBytes(stampFile.toPath()), StandardCharsets.UTF_8))) {
                return;
            }
            deleteTree(target);
            copyAsset(getAssets(), ASSET_ROOT, target);
            Files.write(stampFile.toPath(), stamp.getBytes(StandardCharsets.UTF_8));
        } catch (Exception e) {
            Log.e(TAG, "assets could not be unpacked", e);
        }
    }

    private static void copyAsset(AssetManager assets, String path, File target) throws IOException {
        String[] children = assets.list(path);
        if (children != null && children.length > 0) {
            if (!target.isDirectory() && !target.mkdirs()) {
                throw new IOException("cannot create " + target);
            }
            for (String child : children) {
                copyAsset(assets, path + "/" + child, new File(target, child));
            }
            return;
        }
        try (InputStream in = assets.open(path); OutputStream out = new FileOutputStream(target)) {
            byte[] buffer = new byte[64 * 1024];
            for (int n; (n = in.read(buffer)) > 0; ) {
                out.write(buffer, 0, n);
            }
        }
    }

    private static void deleteTree(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteTree(child);
            }
        }
        file.delete();
    }

    // Games are read by path, so the user's folders need "all files access" (Android 11+)
    // or the storage permission (older). Asked at every start until it is granted.
    private void askForStorage() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (Environment.isExternalStorageManager()) {
                return;
            }
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:" + getPackageName())));
            } catch (Exception e) {
                try {
                    startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                } catch (Exception inner) {
                    Log.w(TAG, "this device has no all-files-access screen", inner);
                }
            }
        } else if (checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)
                != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] {Manifest.permission.READ_EXTERNAL_STORAGE,
                    Manifest.permission.WRITE_EXTERNAL_STORAGE}, 1);
        }
    }
}
