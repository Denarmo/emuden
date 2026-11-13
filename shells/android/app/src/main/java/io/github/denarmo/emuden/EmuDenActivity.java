// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.res.AssetManager;
import android.content.res.Configuration;
import android.os.Build;
import android.os.Bundle;
import android.os.SystemClock;
import android.system.Os;
import android.util.Log;
import android.view.KeyEvent;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;

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
    private SecondScreen secondScreen;
    private TextBridge textBridge;
    private static EmuDenActivity self;
    private OnBackInvokedCallback backCallback;
    private long lastBack;

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
        secondScreen = new SecondScreen(this);
        textBridge = new TextBridge(this);
        self = this;
        super.onCreate(savedInstanceState);
        AppList.watch(this);
        trapBack();
        storage.askAtStart();
    }

    @Override
    protected void onStart() {
        super.onStart();
        secondScreen.onStart();
    }

    @Override
    protected void onStop() {
        textBridge.onStop();
        secondScreen.onStop();
        super.onStop();
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        secondScreen.onConfigurationChanged();
    }

    @Override
    protected void onDestroy() {
        AppList.unwatch(this);
        if (self == this) {
            self = null;
        }
        if (backCallback != null && Build.VERSION.SDK_INT >= 33) {
            getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(backCallback);
            backCallback = null;
        }
        super.onDestroy();
    }

    // Back is one thing for the app wherever it comes from: the gesture, the navigation bar,
    // a keyboard, or the Back key of a built-in pad. That last one arrives as KEYCODE_BACK
    // from a gamepad device (the Retroid's pad: scan code 158), which SDL would hand on as
    // the pad's Select button. All of them become SDL's Back key instead, which opens the
    // pause menu in a game and goes back a page elsewhere.
    private void trapBack() {
        if (Build.VERSION.SDK_INT >= 33) {
            // Android 13 and later: with enableOnBackInvokedCallback the system calls this
            // for the gesture, the bar and Back keys alike.
            backCallback = this::sendBack;
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                    OnBackInvokedDispatcher.PRIORITY_DEFAULT, backCallback);
        }
    }

    private void sendBack() {
        long now = SystemClock.uptimeMillis();
        if (mSDLThread == null || now - lastBack < 80) {
            return; // not running yet, or the same press reported twice
        }
        lastBack = now;
        try {
            onNativeKeyDown(KeyEvent.KEYCODE_BACK);
            onNativeKeyUp(KeyEvent.KEYCODE_BACK);
        } catch (UnsatisfiedLinkError e) {
            Log.w(TAG, "Back before the native library was loaded");
        }
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        // Where the system still delivers Back as a key (before Android 13, or a device that
        // skips the callback), it must not reach SDL's gamepad code.
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK) {
            if (event.getAction() == KeyEvent.ACTION_UP && !event.isCanceled()) {
                sendBack();
            }
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    // With an app opened on the other display this activity stays resumed and keeps drawing
    // and playing (SDL pauses on stop only); it just no longer has the keys and the pad.
    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        try {
            AppList.nativeFocus(hasFocus);
        } catch (UnsatisfiedLinkError e) {
            // Before the native library is loaded nobody asks.
        }
    }

    /** Called from native code (platform/src/android/apps_android.cpp), from a job's thread. */
    public static String[] appsList() {
        EmuDenActivity activity = self;
        return activity != null ? AppList.list(activity) : new String[0];
    }

    public static String appsLaunch(String id, int display) {
        EmuDenActivity activity = self;
        return activity != null ? AppList.launch(activity, id, display) : "error:The app is closing";
    }

    /** Called from native code (platform/src/android/text_input_android.cpp). */
    public static void textBegin(int session, String text, String hint, int maxLength, boolean second) {
        TextBridge.begin(session, text, hint, maxLength, second);
    }

    public static void textSet(int session, String text) {
        TextBridge.setText(session, text);
    }

    public static void textEnd(int session) {
        TextBridge.end(session);
    }

    /** Called from native code (platform/src/android): the second screen is wanted or not. */
    public static void secondScreenWanted(boolean wanted) {
        SecondScreen.setWanted(wanted);
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
