// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.res.AssetManager;
import android.content.res.Configuration;
import android.media.AudioManager;
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

/**
 * The main activity: SDL's, plus what the native platform layer asks of Android. The second
 * display has an activity of its own (PanelActivity) that shares this one's native loop.
 */
public class EmuDenActivity extends SDLActivity {
    private static final String TAG = "EmuDEN";
    private static final String ASSET_ROOT = "emuden";

    private StorageAccess storage;
    private SecondScreen secondScreen;
    private TextBridge textBridge;
    private static EmuDenActivity self;
    private OnBackInvokedCallback backCallback;
    private long lastBack;
    private boolean windowFocus = true;
    private boolean topResumed = true;

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
        setVolumeControlStream(AudioManager.STREAM_MUSIC);
        AppList.watch(this);
        AppBeside.watch(this);
        trapBack();
        storage.askAtStart();
    }

    @Override
    protected void onStart() {
        tellStopped(false);
        super.onStart();
        secondScreen.onStart();
        WebPage.onActivityStart();
    }

    // Another app covers this display, or the user left. The native loop is not ended or
    // blocked by this: with the game on the other display (PanelActivity) it goes on there,
    // otherwise the app holds the game itself until this activity is shown again.
    @Override
    protected void onStop() {
        textBridge.onStop();
        WebPage.onActivityStop();
        secondScreen.onStop();
        super.onStop();
        tellStopped(true);
    }

    private static void tellStopped(boolean stopped) {
        try {
            nativeStopped(stopped);
        } catch (UnsatisfiedLinkError e) {
            // Not loaded yet: it starts as not stopped.
        }
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        secondScreen.onConfigurationChanged();
    }

    @Override
    protected void onDestroy() {
        tellStopped(false); // the loop must see the quit that follows
        AppList.unwatch(this);
        AppBeside.unwatch(this);
        secondScreen.onDestroy();
        if (self == this) {
            self = null;
        }
        if (backCallback != null && Build.VERSION.SDK_INT >= 33) {
            getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(backCallback);
            backCallback = null;
        }
        WebPage.onActivityDestroy();
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
            backCallback = this::onBack;
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                    OnBackInvokedDispatcher.PRIORITY_DEFAULT, backCallback);
        }
    }

    // Over an app opened beside the game on this display Back gives the display back to it.
    private void onBack() {
        if (!AppBeside.back(AppBeside.MAIN)) {
            sendBack();
        }
    }

    /** Back on the second display's activity is the app's Back as well. */
    static void backFromPanel() {
        EmuDenActivity activity = self;
        if (activity != null) {
            activity.sendBack();
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
                onBack();
            }
            return true;
        }
        // The browser's page may hold the focus (for typing); the pad still belongs to the app.
        if (WebPage.showing() && mSurface != null && SDLControllerManager.isDeviceSDLJoystick(event.getDeviceId())) {
            return handleKeyEvent(mSurface, event.getKeyCode(), event, null);
        }
        return super.dispatchKeyEvent(event);
    }

    // With an app opened on the other display this activity stays resumed and keeps drawing
    // and playing; it just no longer has the keys and the pad. Each display keeps a focused
    // window of its own, so the window does not lose its focus then: what moves on is which
    // activity is the top resumed one (this one, PanelActivity, or the other app).
    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        windowFocus = hasFocus;
        tellFocus();
        askGameFocus();
    }

    @Override
    public void onTopResumedActivityChanged(boolean isTopResumedActivity) {
        super.onTopResumedActivityChanged(isTopResumedActivity);
        topResumed = isTopResumedActivity;
        tellFocus();
        if (isTopResumedActivity && gameFocusHop) {
            gameFocusHop = false;
            getWindow().getDecorView().postDelayed(this::askGameFocus, 200);
        }
    }

    private void tellFocus() {
        AppBeside.focus(AppBeside.MAIN, windowFocus && topResumed);
    }

    /** Called from native code (platform/src/android/apps_android.cpp). */
    public static void besideRaise(int display) {
        EmuDenActivity activity = self;
        if (activity != null) {
            AppBeside.raise(activity, display);
        }
    }

    public static void besideLower(int display, String app) {
        EmuDenActivity activity = self;
        if (activity != null) {
            AppBeside.lower(activity, display, app);
        }
    }

    public static boolean besideHandle(int display) {
        EmuDenActivity activity = self;
        return activity != null && AppBeside.showHandle(activity, display);
    }

    public static void besideHandleHide(int display) {
        EmuDenActivity activity = self;
        if (activity != null) {
            AppBeside.hideHandle(activity);
        }
    }

    /** Called from native code (platform/src/android/lifecycle_android.cpp): the media volume. */
    public static int volumeSteps() {
        AudioManager audio = audio();
        return audio != null ? audio.getStreamMaxVolume(AudioManager.STREAM_MUSIC) : 0;
    }

    public static int volumeGet() {
        AudioManager audio = audio();
        return audio != null ? audio.getStreamVolume(AudioManager.STREAM_MUSIC) : 0;
    }

    public static void volumeSet(int step) {
        AudioManager audio = audio();
        if (audio != null) {
            int top = audio.getStreamMaxVolume(AudioManager.STREAM_MUSIC);
            try {
                audio.setStreamVolume(AudioManager.STREAM_MUSIC, Math.max(0, Math.min(top, step)), 0);
            } catch (SecurityException e) {
                Log.w(TAG, "the media volume cannot be changed now (do not disturb)");
            }
        }
    }

    /**
     * Called from native code (lifecycle_android.cpp). The dual-screen panel of the Retroid
     * Pocket Duo and the AYN Thor (one firmware lineage) sets each screen's volume and
     * brightness, which no other app may do; the device's dual-screen button opens it.
     */
    public static boolean screenControlsAvailable() {
        EmuDenActivity activity = self;
        if (activity == null) {
            return false;
        }
        for (String panel : SCREEN_PANELS) {
            try {
                activity.getPackageManager().getPackageInfo(panel, 0);
                return true;
            } catch (android.content.pm.PackageManager.NameNotFoundException e) {
                // not this one
            }
        }
        return false;
    }

    /**
     * Presses that button: the system opens the panel for scan code 194 sent to the focused
     * window, ours. The key has no key code, so nothing else takes it for Home.
     */
    public static void screenControlsOpen() {
        new Thread(() -> {
            try {
                android.app.Instrumentation input = new android.app.Instrumentation();
                long now = android.os.SystemClock.uptimeMillis();
                for (int action : new int[] {android.view.KeyEvent.ACTION_DOWN, android.view.KeyEvent.ACTION_UP}) {
                    input.sendKeySync(new android.view.KeyEvent(now, now, action, 0, 0, 0,
                            android.view.KeyCharacterMap.VIRTUAL_KEYBOARD, SCREEN_PANEL_SCAN_CODE, 0,
                            android.view.InputDevice.SOURCE_KEYBOARD));
                }
            } catch (RuntimeException e) {
                Log.w(TAG, "the screen controls did not open: " + e);
            }
        }, "screen-controls").start();
    }

    private static android.media.AudioFocusRequest gameFocus;
    private static boolean gameFocusWanted;
    private static boolean gameFocusHop;

    /**
     * Called from native code: hold the audio focus for a game on the main display, or let it
     * go. On the Retroid Pocket Duo the window the device last noted as focused when it is asked
     * for decides which screen's volume the sound follows: this one, not the quick settings on
     * the other display. The game goes on when another app takes it.
     */
    public static void gameFocus(boolean hold) {
        EmuDenActivity activity = self;
        if (activity == null) {
            return;
        }
        activity.runOnUiThread(() -> {
            gameFocusWanted = hold;
            if (!hold) {
                gameFocusHop = false;
            }
            AudioManager audio = (AudioManager) activity.getSystemService(AUDIO_SERVICE);
            if (!hold && gameFocus != null && audio != null) {
                audio.abandonAudioFocusRequest(gameFocus);
                gameFocus = null;
                // The device sorts the apps by screen again only when the activities change.
                FocusHop.to(activity, activity.getDisplay().getDisplayId(), null);
            }
            if (hold && gameFocus == null && !gameFocusHop) {
                // The device notes the window only when a display's focused activity changes:
                // one that ends at once on this display makes it note this window again.
                gameFocusHop = FocusHop.to(activity, activity.getDisplay().getDisplayId(), () -> { });
            }
            activity.askGameFocus();
        });
    }

    private void askGameFocus() {
        if (!gameFocusWanted || gameFocus != null || gameFocusHop || !windowFocus || !topResumed) {
            return;
        }
        AudioManager audio = (AudioManager) getSystemService(AUDIO_SERVICE);
        if (audio == null) {
            return;
        }
        gameFocus = new android.media.AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                .setAudioAttributes(new android.media.AudioAttributes.Builder()
                        .setUsage(android.media.AudioAttributes.USAGE_GAME)
                        .setContentType(android.media.AudioAttributes.CONTENT_TYPE_MUSIC)
                        .build())
                .setOnAudioFocusChangeListener(change -> { })
                .build();
        audio.requestAudioFocus(gameFocus);
    }

    private static final String[] SCREEN_PANELS = {
        "com.retroidpocket.dualscreen.assistant", // Retroid Pocket Duo
        "com.odin.dualscreen.assistant",          // AYN Thor
    };
    private static final int SCREEN_PANEL_SCAN_CODE = 194; // KEY_F24, the dual-screen button on both

    private static AudioManager audio() {
        EmuDenActivity activity = self;
        return activity != null ? (AudioManager) activity.getSystemService(AUDIO_SERVICE) : null;
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
        return self;
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

    private static native void nativeStopped(boolean stopped);

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
