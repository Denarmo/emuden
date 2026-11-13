// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.ActivityOptions;
import android.app.Presentation;
import android.content.Context;
import android.content.Intent;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.Display;
import android.view.Surface;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;

/**
 * The second screen of a two-screen device (Retroid Pocket Duo, AYN Thor) or an attached
 * display. One PanelSurface is shown on it; its Surface and touches go to the native
 * platform layer (platform/src/android/second_display_android.cpp), which draws there with
 * the app's own GL context. Nothing is drawn or decided here.
 *
 * The surface lives in PanelActivity, an activity of its own on that display, wherever the
 * system lets one start there (Android 10 and later): it stays while the main activity is
 * stopped, so a game on the second display goes on under an app that covers the main one,
 * and an app opened on the second display lies over it like over any activity. Where the
 * display takes no activities the surface is in a Presentation as before, which exists only
 * while the main activity is started and lies over every app of its display.
 */
final class SecondScreen implements DisplayManager.DisplayListener {
    private static final String TAG = "EmuDEN";
    private static final long LAUNCH_AGAIN_MS = 2000;

    private static SecondScreen instance;

    private final Activity activity;
    private final DisplayManager displays;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private Screen shown;        // the Presentation, where no activity can be there
    private PanelActivity panel; // the activity on the other display
    private long launchedAt;     // when it was last asked for
    private boolean started;
    private boolean wanted;

    SecondScreen(Activity activity) {
        this.activity = activity;
        displays = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
        displays.registerDisplayListener(this, ui);
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
        update();
    }

    void onStop() {
        started = false;
        update();
    }

    void onDestroy() {
        displays.unregisterDisplayListener(this);
        wanted = false;
        update();
        if (instance == this) {
            instance = null;
        }
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
        if (shown != null && shown.getDisplay().getDisplayId() == ownDisplayId(activity)) {
            update();
        }
    }

    /** The display the activity is on, for code that opens something beside it. */
    @SuppressWarnings("deprecation")
    static int ownDisplayId(Activity activity) {
        Display own = activity.getWindowManager().getDefaultDisplay();
        return own != null ? own.getDisplayId() : Display.DEFAULT_DISPLAY;
    }

    /** The other display of the device (where the second screen goes), or null without one. */
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

    /** The second screen is an activity of ours, so another app can be opened over it. */
    static boolean hostsActivity() {
        SecondScreen self = instance;
        return self != null && self.panel != null;
    }

    /** From PanelActivity.onCreate. False when it is not wanted (any more). */
    static boolean attach(PanelActivity created) {
        SecondScreen self = instance;
        if (self == null || !self.wanted) {
            return false;
        }
        if (self.panel != null && self.panel != created) {
            self.panel.finish();
        }
        self.panel = created;
        if (self.started) {
            // Starting it moved the keys and the pad to its display: back to the main one.
            FocusHop.home(self.activity);
        }
        return true;
    }

    static void detach(PanelActivity destroyed) {
        SecondScreen self = instance;
        if (self != null && self.panel == destroyed) {
            self.panel = null;
            self.launchedAt = 0;
            // Destroyed by the system while it is still wanted: it is started again.
            self.ui.post(self::update);
        }
    }

    /** On the UI thread: the second screen comes to the front of its display, with the keys. */
    static void raise() {
        SecondScreen self = instance;
        if (self == null || !self.wanted) {
            return;
        }
        if (self.panel == null) {
            self.launchedAt = 0;
            self.update();
            return;
        }
        self.launch(self.panel.displayId());
    }

    /** On the UI thread: it goes behind the app under it on its display. */
    static void lower() {
        SecondScreen self = instance;
        if (self != null && self.panel != null) {
            self.panel.moveTaskToBack(true);
        }
    }

    private Intent panelIntent() {
        return new Intent(activity, PanelActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT
                        | Intent.FLAG_ACTIVITY_NO_ANIMATION);
    }

    private boolean takesActivities(Display display) {
        if (Build.VERSION.SDK_INT < 29) {
            return false; // one resumed activity for the whole device before Android 10
        }
        ActivityManager manager = (ActivityManager) activity.getSystemService(Context.ACTIVITY_SERVICE);
        return manager != null
                && manager.isActivityStartAllowedOnDisplay(activity, display.getDisplayId(), panelIntent());
    }

    private boolean launch(int displayId) {
        try {
            ActivityOptions options = ActivityOptions.makeBasic();
            options.setLaunchDisplayId(displayId);
            // The main activity may be stopped; the panel or the application can start it too.
            Context from = started || panel == null ? activity : panel;
            from.startActivity(panelIntent(), options.toBundle());
            return true;
        } catch (RuntimeException e) {
            Log.w(TAG, "the second screen could not be opened as an activity", e);
            return false;
        }
    }

    private void update() {
        Display target = otherDisplay(activity);
        tell(target != null);
        boolean want = wanted && target != null;
        boolean byActivity = want && takesActivities(target);

        if (!byActivity && panel != null) {
            panel.finish();
            panel = null;
            launchedAt = 0;
        }
        if (byActivity) {
            if (panel != null && panel.displayId() != target.getDisplayId()) {
                panel.finish(); // the screens swapped
                panel = null;
                launchedAt = 0;
            }
            long now = SystemClock.uptimeMillis();
            if (panel == null && (launchedAt == 0 || now - launchedAt > LAUNCH_AGAIN_MS)) {
                launchedAt = now;
                if (!launch(target.getDisplayId())) {
                    byActivity = false;
                }
            }
        }

        boolean present = want && !byActivity && started;
        if (shown != null && (!present || shown.getDisplay().getDisplayId() != target.getDisplayId())) {
            shown.dismiss();
            shown = null;
        }
        if (shown == null && present) {
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

    static void surface(Surface surface, int width, int height) {
        try {
            nativeSurface(surface, width, height);
        } catch (UnsatisfiedLinkError e) {
            // Not loaded yet: the surface is told when it changes next.
        }
    }

    static void touch(int kind, float x, float y) {
        try {
            nativeTouch(kind, x, y);
        } catch (UnsatisfiedLinkError e) {
            // Nobody to tell.
        }
    }

    /** Where the display takes no activity: a layer above its apps that cannot take the focus. */
    private static final class Screen extends Presentation {
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
            setContentView(new PanelSurface(getContext()));
        }
    }

    private static native void nativeDisplay(boolean present);

    private static native void nativeSurface(Surface surface, int width, int height);

    private static native void nativeTouch(int kind, float x, float y);
}
