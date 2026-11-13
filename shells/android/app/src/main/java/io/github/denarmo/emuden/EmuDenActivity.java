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
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLControllerManager;

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
    private OnBackInvokedCallback backCallback;
    private long lastBack;
    private static EmuDenActivity current;

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
        current = this;
        super.onCreate(savedInstanceState);
        trapBack();
        storage.askAtStart();
    }

    @Override
    protected void onStart() {
        super.onStart();
        secondScreen.onStart();
        WebPage.onActivityStart();
    }

    @Override
    protected void onStop() {
        WebPage.onActivityStop();
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
        if (backCallback != null && Build.VERSION.SDK_INT >= 33) {
            getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(backCallback);
            backCallback = null;
        }
        WebPage.onActivityDestroy();
        if (current == this) {
            current = null;
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
        // The browser's page may hold the focus (for typing); the pad still belongs to the app.
        if (WebPage.showing() && mSurface != null && SDLControllerManager.isDeviceSDLJoystick(event.getDeviceId())) {
            return handleKeyEvent(mSurface, event.getKeyCode(), event, null);
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (WebPage.showing() && mSurface != null && SDLControllerManager.isDeviceSDLJoystick(event.getDeviceId())) {
            View.OnGenericMotionListener pad = getMotionListener();
            return pad.onGenericMotion(mSurface, event);
        }
        return super.dispatchGenericMotionEvent(event);
    }

    // The in-app browser (WebPage, platform/src/android/webview_android.cpp).
    static EmuDenActivity current() {
        return current;
    }

    static ViewGroup webLayout() {
        return mLayout;
    }

    static View webSurface() {
        return mSurface;
    }

    /** Called from native code (platform/src/android). */
    public static boolean webAvailable() {
        return WebPage.available();
    }

    /** Called from native code (platform/src/android). */
    public static void webCommand(long handle, int op, int a, int b, int c, int d, byte[] s, byte[] t) {
        WebPage.command(handle, op, a, b, c, d, s, t);
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
