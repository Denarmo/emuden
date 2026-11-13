// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.app.Activity;
import android.app.Presentation;
import android.content.Context;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Display;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;

/**
 * The second screen of a two-screen device (Retroid Pocket Duo, AYN Thor) or an attached
 * display. A Presentation with one SurfaceView is shown on it; its Surface goes to the native
 * platform layer (platform/src/android/second_display_android.cpp), which draws there with the
 * app's own GL context, and its touches go the same way. Nothing is drawn or decided here.
 *
 * The Presentation exists while the activity is started, a presentation display exists and
 * the native side wants the screen. It cannot take the focus, so keys and the pad stay with
 * the activity.
 */
final class SecondScreen implements DisplayManager.DisplayListener {
    private static final String TAG = "EmuDEN";

    private static SecondScreen instance;

    private final Activity activity;
    private final DisplayManager displays;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private Screen shown;
    private boolean started;
    private boolean wanted;

    SecondScreen(Activity activity) {
        this.activity = activity;
        displays = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
        instance = this;
    }

    /** From the native side, on its own thread. */
    static void setWanted(boolean wanted) {
        SecondScreen self = instance;
        if (self != null) {
            self.ui.post(() -> {
                self.wanted = wanted;
                self.update();
            });
        }
    }

    void onStart() {
        started = true;
        displays.registerDisplayListener(this, ui);
        update();
    }

    void onStop() {
        started = false;
        displays.unregisterDisplayListener(this);
        update();
    }

    /** The activity may have moved to the other display: the screens swap. */
    void onConfigurationChanged() {
        update();
    }

    @Override
    public void onDisplayAdded(int displayId) {
        update();
    }

    @Override
    public void onDisplayRemoved(int displayId) {
        update();
    }

    @Override
    public void onDisplayChanged(int displayId) {
        // Sizes arrive through the surface. Only a swap of the screens matters here.
        if (shown != null && shown.getDisplay().getDisplayId() == ownDisplayId()) {
            update();
        }
    }

    @SuppressWarnings("deprecation")
    private int ownDisplayId() {
        Display own = activity.getWindowManager().getDefaultDisplay();
        return own != null ? own.getDisplayId() : Display.DEFAULT_DISPLAY;
    }

    /** The display the activity is on, for code that opens something beside it. */
    @SuppressWarnings("deprecation")
    static int ownDisplayId(Activity activity) {
        Display own = activity.getWindowManager().getDefaultDisplay();
        return own != null ? own.getDisplayId() : Display.DEFAULT_DISPLAY;
    }

    /** The other display of the device (where the Presentation goes), or null without one. */
    static Display otherDisplay(Activity activity) {
        DisplayManager manager = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
        int own = ownDisplayId(activity);
        for (Display display : manager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            if (display.getDisplayId() != own && display.isValid()) {
                return display;
            }
        }
        return null;
    }

    static int otherDisplayId(Activity activity) {
        Display other = otherDisplay(activity);
        return other != null ? other.getDisplayId() : -1;
    }

    // The display a Presentation may be shown on that the activity is not on itself.
    private Display pick() {
        int own = ownDisplayId();
        for (Display display : displays.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)) {
            if (display.getDisplayId() != own && display.isValid()) {
                return display;
            }
        }
        return null;
    }

    private void update() {
        Display target = pick();
        tell(target != null);
        if (!started || !wanted) {
            target = null;
        }
        if (shown != null && (target == null || shown.getDisplay().getDisplayId() != target.getDisplayId())) {
            shown.dismiss();
            shown = null;
        }
        if (shown == null && target != null) {
            try {
                Screen screen = new Screen(activity, target);
                screen.show();
                shown = screen;
            } catch (RuntimeException e) {
                // The display went away in between, or does not take windows.
                Log.w(TAG, "the second screen could not be opened", e);
            }
        }
    }

    private static void tell(boolean present) {
        try {
            nativeDisplay(present);
        } catch (UnsatisfiedLinkError e) {
            // The native library is not loaded yet; the next update tells it.
        }
    }

    private static final class Screen extends Presentation implements SurfaceHolder.Callback, View.OnTouchListener {
        private int pointer = -1;

        Screen(Context context, Display display) {
            super(context, display);
        }

        @Override
        @SuppressWarnings("deprecation")
        protected void onCreate(Bundle savedInstanceState) {
            super.onCreate(savedInstanceState);
            setCancelable(false);
            Window window = getWindow();
            if (window != null) {
                window.addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                        | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
                        | WindowManager.LayoutParams.FLAG_FULLSCREEN
                        | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS);
                window.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
            }
            SurfaceView view = new SurfaceView(getContext());
            view.getHolder().addCallback(this);
            view.setOnTouchListener(this);
            setContentView(view);
        }

        @Override
        public void surfaceCreated(SurfaceHolder holder) {
        }

        @Override
        public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            nativeSurface(holder.getSurface(), width, height);
        }

        @Override
        public void surfaceDestroyed(SurfaceHolder holder) {
            // Returns once the native side has let go of the surface.
            nativeSurface(null, 0, 0);
        }

        @Override
        public boolean onTouch(View view, MotionEvent event) {
            int index = event.getActionIndex();
            switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                pointer = event.getPointerId(index);
                nativeTouch(0, event.getX(index), event.getY(index));
                break;
            case MotionEvent.ACTION_MOVE: {
                int at = event.findPointerIndex(pointer);
                if (at >= 0) {
                    nativeTouch(1, event.getX(at), event.getY(at));
                }
                break;
            }
            case MotionEvent.ACTION_POINTER_UP:
                if (event.getPointerId(index) != pointer) {
                    break;
                }
                // The first finger left: the press ends with it.
            case MotionEvent.ACTION_UP:
                if (pointer >= 0) {
                    nativeTouch(2, event.getX(index), event.getY(index));
                    pointer = -1;
                }
                break;
            case MotionEvent.ACTION_CANCEL:
                if (pointer >= 0) {
                    // Ends the press somewhere it cannot count as a tap.
                    nativeTouch(2, -100000.0f, -100000.0f);
                    pointer = -1;
                }
                break;
            default:
                break;
            }
            return true;
        }
    }

    private static native void nativeDisplay(boolean present);

    private static native void nativeSurface(Surface surface, int width, int height);

    private static native void nativeTouch(int kind, float x, float y);
}
