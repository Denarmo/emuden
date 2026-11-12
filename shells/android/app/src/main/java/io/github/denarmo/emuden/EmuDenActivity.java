// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.res.AssetManager;
import android.os.Bundle;
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

    private StorageAccess storage;

    @Override
    protected String[] getLibraries() {
        // SDL is linked into libmain.so.
        return new String[] {"main"};
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        storage = new StorageAccess(this);
        storage.setHome();
        automationFromIntent();
        unpackAssets();
        super.onCreate(savedInstanceState);
        storage.askAtStart();
    }

    @Override
    protected void onResume() {
        super.onResume();
        storage.onResume();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == StorageAccess.REQUEST_FOLDER) {
            storage.onFolderPicked(resultCode, data);
            return;
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        if (!storage.onPermissionsResult(requestCode)) {
            super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        }
    }

    // Debug builds only: "am start ... --ei automation <port>" turns the automation endpoint
    // on (docs/interfaces.md); "adb forward tcp:<port> tcp:<port>" then reaches it.
    private void automationFromIntent() {
        if ((getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) == 0) {
            return;
        }
        int port = getIntent() != null ? getIntent().getIntExtra("automation", 0) : 0;
        if (port > 0) {
            try {
                Os.setenv("EMUDEN_AUTOMATION", Integer.toString(port), true);
            } catch (Exception e) {
                Log.w(TAG, "automation could not be turned on", e);
            }
        }
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
}
