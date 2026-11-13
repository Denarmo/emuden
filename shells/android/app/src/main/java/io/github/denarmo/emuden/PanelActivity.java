// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.app.Activity;
import android.media.AudioManager;
import android.os.Build;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLControllerManager;

/**
 * The app's window on the second display: an activity in a task of its own, started there by
 * SecondScreen, in the same process as the main activity and drawn by the same native loop
 * and GL context. It is an activity and not a Presentation so that it does not depend on the
 * main activity: when another app covers the main display the main activity is stopped, and
 * this one stays resumed (Android 10 and later resume one activity per display), keeps the
 * process in the foreground and keeps showing the game or the quick settings. Another app
 * opened on this display simply lies over it, and it is shown again when that app is left.
 *
 * It can take the focus, so keys and the pad that arrive here go to SDL like the main
 * activity's, and Back is the app's Back.
 */
public class PanelActivity extends Activity {
    private boolean attached;
    private OnBackInvokedCallback backCallback;
    private long lastBack;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (!SecondScreen.attach(this)) {
            // Nobody wants it (the system restored the task, or the app changed its mind).
            finish();
            return;
        }
        attached = true;
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        setVolumeControlStream(AudioManager.STREAM_MUSIC);
        PanelSurface surface = new PanelSurface(this);
        surface.setFocusable(true);
        surface.setFocusableInTouchMode(true);
        setContentView(surface);
        surface.requestFocus();
        immersive();
        if (Build.VERSION.SDK_INT >= 33) {
            backCallback = this::back;
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                    OnBackInvokedDispatcher.PRIORITY_DEFAULT, backCallback);
        }
    }

    @Override
    protected void onDestroy() {
        if (attached) {
            SecondScreen.detach(this);
            AppBeside.focus(AppBeside.SECOND, false);
        }
        if (backCallback != null && Build.VERSION.SDK_INT >= 33) {
            getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(backCallback);
            backCallback = null;
        }
        super.onDestroy();
    }

    @SuppressWarnings("deprecation")
    int displayId() {
        return getWindowManager().getDefaultDisplay().getDisplayId();
    }

    @SuppressWarnings("deprecation")
    private void immersive() {
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            immersive();
        }
    }

    @Override
    public void onTopResumedActivityChanged(boolean isTopResumedActivity) {
        super.onTopResumedActivityChanged(isTopResumedActivity);
        AppBeside.focus(AppBeside.SECOND, isTopResumedActivity);
    }

    private void back() {
        long now = SystemClock.uptimeMillis();
        if (now - lastBack < 80) {
            return; // the same press reported twice
        }
        lastBack = now;
        // Over an app opened beside the game Back gives the display back to that app.
        if (!AppBeside.back(AppBeside.SECOND)) {
            EmuDenActivity.backFromPanel();
        }
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        int code = event.getKeyCode();
        if (code == KeyEvent.KEYCODE_BACK) {
            if (event.getAction() == KeyEvent.ACTION_UP && !event.isCanceled()) {
                back();
            }
            return true;
        }
        if (code == KeyEvent.KEYCODE_VOLUME_DOWN || code == KeyEvent.KEYCODE_VOLUME_UP
                || code == KeyEvent.KEYCODE_VOLUME_MUTE) {
            return super.dispatchKeyEvent(event); // the system's media volume
        }
        try {
            if (SDLActivity.handleKeyEvent(getWindow().getDecorView(), code, event, null)) {
                return true;
            }
        } catch (UnsatisfiedLinkError e) {
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if ((event.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) {
            try {
                return SDLControllerManager.handleJoystickMotionEvent(event);
            } catch (UnsatisfiedLinkError e) {
                return true;
            }
        }
        return super.dispatchGenericMotionEvent(event);
    }
}
