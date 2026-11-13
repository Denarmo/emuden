// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.app.Activity;
import android.app.Presentation;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Point;
import android.graphics.drawable.ColorDrawable;
import android.os.Build;
import android.util.Log;
import android.util.TypedValue;
import android.view.Display;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.Window;
import android.view.WindowManager;

/**
 * An app open beside the game, for the native side (platform/src/android/apps_android.cpp),
 * which decides everything: this app's own window on a display is raised over the other app
 * or lowered behind it, a small handle floats over the other app, and Back, the handle and
 * the system's Home are reported. The app itself is opened by AppList over PanelActivity or
 * the main activity like over any other activity.
 */
final class AppBeside {
    static final int MAIN = 1, SECOND = 2;
    private static final String TAG = "EmuDEN";
    private static final String PREFS = "beside";
    private static final int EVENT_HANDLE = 0, EVENT_HOME = 2;

    private static Handle handle;
    private static BroadcastReceiver homeReceiver;
    private static boolean mainFocus = true, panelFocus;

    private AppBeside() {
    }

    /** Home is told to nobody, but the system closes its dialogs for it and says why. */
    @SuppressWarnings("deprecation")
    static void watch(Context context) {
        if (homeReceiver != null) {
            return;
        }
        homeReceiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context c, Intent intent) {
                if ("homekey".equals(intent.getStringExtra("reason"))) {
                    event(EVENT_HOME, 0);
                }
            }
        };
        IntentFilter filter = new IntentFilter(Intent.ACTION_CLOSE_SYSTEM_DIALOGS);
        Context app = context.getApplicationContext();
        if (Build.VERSION.SDK_INT >= 33) {
            app.registerReceiver(homeReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            app.registerReceiver(homeReceiver, filter);
        }
    }

    static void unwatch(Context context) {
        if (homeReceiver != null) {
            context.getApplicationContext().unregisterReceiver(homeReceiver);
            homeReceiver = null;
        }
        hideHandle();
    }

    /** One of our two activities is the one keys and the pad go to, or neither is. */
    static void focus(int which, boolean focused) {
        if (which == SECOND) {
            panelFocus = focused;
        } else {
            mainFocus = focused;
        }
        try {
            AppList.nativeFocus(mainFocus || panelFocus);
        } catch (UnsatisfiedLinkError e) {
            // Before the native library is loaded nobody asks.
        }
    }

    /** Back on a display. True when the native side takes it (back to the app beside). */
    static boolean back(int display) {
        try {
            return nativeBack(display);
        } catch (UnsatisfiedLinkError e) {
            return false;
        }
    }

    private static void event(int kind, int display) {
        try {
            nativeEvent(kind, display);
        } catch (UnsatisfiedLinkError e) {
            // Not loaded: nothing is beside anything yet.
        }
    }

    /** From the native side: our window on that display over whatever is there. */
    static void raise(Activity main, int display) {
        main.runOnUiThread(() -> {
            if (display == SECOND) {
                SecondScreen.raise();
            } else {
                FocusHop.home(main);
            }
        });
    }

    /**
     * From the native side: the app under our window on that display is in front again. Its
     * task is brought forward the way a launcher does, which leaves our window right under
     * it: the app left with Back shows the quick settings. Sending our own task to the back
     * would put it behind the launcher as well.
     */
    static void lower(Activity main, int display, String app) {
        main.runOnUiThread(() -> {
            if (AppList.toFront(main, app, display)) {
                return;
            }
            if (display == SECOND) {
                SecondScreen.lower();
            } else {
                main.moveTaskToBack(true);
            }
        });
    }

    /**
     * From the native side: the handle over the other app on that display. A presentation
     * display takes a window of ours over other apps as it is; any other display (the main
     * one, with the game on the second) only with "Display over other apps", which the user
     * grants once: the first time it is missing, its page in the system settings opens.
     * False when the handle cannot be shown there.
     */
    static boolean showHandle(Activity main, int display) {
        Display target = display == SECOND ? SecondScreen.otherDisplay(main) : ownDisplay(main);
        if (target == null) {
            return false;
        }
        boolean presentation = (target.getFlags() & Display.FLAG_PRESENTATION) != 0;
        if (!presentation && !canOverlay(main, target)) {
            askForOverlay(main);
            return false;
        }
        main.runOnUiThread(() -> {
            hideHandle();
            try {
                Handle made = new Handle(main, target, display, presentation);
                made.show();
                handle = made;
            } catch (RuntimeException e) {
                Log.w(TAG, "the handle could not be shown", e);
            }
        });
        return true;
    }

    /** An overlay window goes on the default display only, and needs the user's leave. */
    private static boolean canOverlay(Context context, Display target) {
        return Build.VERSION.SDK_INT >= 26 && target.getDisplayId() == Display.DEFAULT_DISPLAY
                && android.provider.Settings.canDrawOverlays(context);
    }

    private static void askForOverlay(Activity main) {
        SharedPreferences prefs = main.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        if (Build.VERSION.SDK_INT < 26 || prefs.getBoolean("overlay_asked", false)) {
            return;
        }
        prefs.edit().putBoolean("overlay_asked", true).apply();
        main.runOnUiThread(() -> {
            try {
                main.startActivity(new Intent(android.provider.Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                        android.net.Uri.parse("package:" + main.getPackageName()))
                        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            } catch (RuntimeException e) {
                Log.w(TAG, "the overlay permission page could not be opened", e);
            }
        });
    }

    static void hideHandle(Activity main) {
        main.runOnUiThread(AppBeside::hideHandle);
    }

    private static void hideHandle() {
        Handle shown = handle;
        handle = null;
        if (shown != null) {
            try {
                shown.dismiss();
            } catch (RuntimeException e) {
                Log.w(TAG, "the handle was already gone", e);
            }
        }
    }

    @SuppressWarnings("deprecation")
    private static Display ownDisplay(Activity main) {
        return main.getWindowManager().getDefaultDisplay();
    }

    /**
     * The one control of ours over the other app: a small round tab that brings the quick
     * settings back. It can be dragged anywhere and comes to rest on the nearest edge, where
     * it is found again the next time; it is half transparent until it is touched. It lives
     * in a presentation on a presentation display, in an overlay window on the main one.
     */
    private static final class Handle implements View.OnTouchListener {
        private static final int SIZE_DP = 44;
        private static final float IDLE_ALPHA = 0.45f;
        private static final long FADE_MS = 2500;

        private final Context context;
        private final int which;
        private final Point screen = new Point();
        private final int size;
        private final View tab;
        private final WindowManager.LayoutParams params;
        private Presentation presentation; // on a presentation display
        private WindowManager overlay;      // on the main display
        private float downX, downY;
        private int startX, startY;
        private boolean moved;
        private final Runnable fade = () -> setAlpha(IDLE_ALPHA);

        @SuppressWarnings("deprecation")
        Handle(Activity main, Display display, int which, boolean onPresentation) {
            this.which = which;
            display.getRealSize(screen);
            if (onPresentation) {
                presentation = new Presentation(main, display, android.R.style.Theme_Translucent_NoTitleBar);
                presentation.setCancelable(false);
                context = presentation.getContext();
            } else {
                context = main.getApplicationContext();
                overlay = (WindowManager) context.getSystemService(Context.WINDOW_SERVICE);
            }
            float dp = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 1,
                    context.getResources().getDisplayMetrics());
            size = Math.round(SIZE_DP * dp);
            tab = new Tab(context, dp);
            tab.setOnTouchListener(this);
            if (Build.VERSION.SDK_INT >= 29) {
                // It rests on an edge, where a drag would otherwise be the system's Back gesture.
                tab.setSystemGestureExclusionRects(
                        java.util.Collections.singletonList(new android.graphics.Rect(0, 0, size, size)));
            }
            int flags = WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                    | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
                    | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS;
            if (presentation != null) {
                presentation.setContentView(tab, new android.view.ViewGroup.LayoutParams(size, size));
                Window window = presentation.getWindow();
                window.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
                window.clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
                window.addFlags(flags);
                params = window.getAttributes();
            } else {
                params = new WindowManager.LayoutParams(size, size,
                        WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY, flags,
                        android.graphics.PixelFormat.TRANSLUCENT);
            }
            params.gravity = Gravity.TOP | Gravity.LEFT;
            params.width = size;
            params.height = size;
            params.alpha = IDLE_ALPHA;
            // Where it was left: an edge (0 left, 1 top, 2 right, 3 bottom) and how far along.
            SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
            place(prefs.getInt("handle_edge_" + which, 2), prefs.getFloat("handle_along_" + which, 0.3f));
        }

        void show() {
            if (presentation != null) {
                presentation.getWindow().setAttributes(params);
                presentation.show();
            } else {
                overlay.addView(tab, params);
            }
        }

        void dismiss() {
            if (presentation != null) {
                presentation.dismiss();
            } else {
                overlay.removeView(tab);
            }
        }

        private void apply() {
            if (presentation != null) {
                if (presentation.isShowing()) {
                    presentation.getWindow().setAttributes(params);
                }
            } else if (tab.isAttachedToWindow()) {
                overlay.updateViewLayout(tab, params);
            }
        }

        private void place(int edge, float along) {
            int roomX = Math.max(0, screen.x - size), roomY = Math.max(0, screen.y - size);
            float t = Math.max(0f, Math.min(1f, along));
            params.x = edge == 0 ? 0 : edge == 2 ? roomX : Math.round(roomX * t);
            params.y = edge == 1 ? 0 : edge == 3 ? roomY : Math.round(roomY * t);
        }

        private void setAlpha(float alpha) {
            params.alpha = alpha;
            apply();
        }

        @Override
        public boolean onTouch(View view, MotionEvent event) {
            int roomX = Math.max(0, screen.x - size), roomY = Math.max(0, screen.y - size);
            switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                view.removeCallbacks(fade);
                downX = event.getRawX();
                downY = event.getRawY();
                startX = params.x;
                startY = params.y;
                moved = false;
                setAlpha(1f);
                return true;
            case MotionEvent.ACTION_MOVE: {
                float dx = event.getRawX() - downX, dy = event.getRawY() - downY;
                if (!moved && Math.hypot(dx, dy) > ViewConfiguration.get(context).getScaledTouchSlop()) {
                    moved = true;
                }
                if (moved) {
                    params.x = Math.max(0, Math.min(roomX, startX + Math.round(dx)));
                    params.y = Math.max(0, Math.min(roomY, startY + Math.round(dy)));
                    apply();
                }
                return true;
            }
            case MotionEvent.ACTION_UP:
                view.postDelayed(fade, FADE_MS);
                if (!moved) {
                    event(EVENT_HANDLE, which);
                    return true;
                }
                // To the nearest edge, and remembered.
                int left = params.x, right = roomX - params.x, top = params.y, bottom = roomY - params.y;
                int nearest = Math.min(Math.min(left, right), Math.min(top, bottom));
                int edge = nearest == left ? 0 : nearest == right ? 2 : nearest == top ? 1 : 3;
                float along = edge == 0 || edge == 2 ? (roomY > 0 ? params.y / (float) roomY : 0f)
                        : (roomX > 0 ? params.x / (float) roomX : 0f);
                place(edge, along);
                apply();
                context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                        .putInt("handle_edge_" + which, edge)
                        .putFloat("handle_along_" + which, along)
                        .apply();
                return true;
            case MotionEvent.ACTION_CANCEL:
                view.postDelayed(fade, FADE_MS);
                return true;
            default:
                return false;
            }
        }
    }

    /** A dark disc with a light rim and three bars: no words, so it stays small. */
    private static final class Tab extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final float dp;

        Tab(Context context, float dp) {
            super(context);
            this.dp = dp;
        }

        @Override
        protected void onDraw(Canvas canvas) {
            float cx = getWidth() / 2f, cy = getHeight() / 2f, r = Math.min(cx, cy) - dp;
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(Color.argb(225, 24, 24, 24));
            canvas.drawCircle(cx, cy, r, paint);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(Math.max(1f, 1.5f * dp));
            paint.setColor(Color.argb(190, 255, 255, 255));
            canvas.drawCircle(cx, cy, r, paint);
            paint.setStrokeWidth(2f * dp);
            paint.setColor(Color.WHITE);
            for (int line = -1; line <= 1; ++line) {
                float y = cy + line * 6f * dp;
                canvas.drawLine(cx - 9f * dp, y, cx + 9f * dp, y, paint);
            }
        }
    }

    private static native void nativeEvent(int kind, int display);

    private static native boolean nativeBack(int display);
}
