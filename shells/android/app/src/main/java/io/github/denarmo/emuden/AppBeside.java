// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.app.Activity;
import android.app.ActivityOptions;
import android.app.Presentation;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.util.Log;
import android.util.TypedValue;
import android.view.Display;
import android.view.Gravity;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.TextView;
import android.widget.Toast;

/**
 * An app on the display with the quick settings, beside the game. A Presentation lies over
 * every activity of its display, so the quick settings make room: they are taken away while
 * the app is there (SecondScreen.lend) and a small handle stays at the top of that display,
 * which brings them back. They also come back by themselves when the app is left with Back
 * (AppHost notices). After Home the handle is the way back.
 */
final class AppBeside {
    private static final String TAG = "EmuDEN";

    private static Activity main;
    private static Handle handle;
    private static AppHost host;

    private AppBeside() {
    }

    /** On the UI thread. The app opens on the display; the quick settings give it up. */
    static void open(Activity activity, Intent target, Display display) {
        close(false);
        main = activity;
        SecondScreen.lend(true);
        try {
            Handle made = new Handle(activity, display);
            made.show();
            handle = made;
            Intent intent = new Intent(activity, AppHost.class)
                    .putExtra(AppHost.EXTRA_TARGET, target)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_MULTIPLE_TASK
                            | Intent.FLAG_ACTIVITY_NO_ANIMATION);
            ActivityOptions options = ActivityOptions.makeBasic();
            options.setLaunchDisplayId(display.getDisplayId());
            activity.startActivity(intent, options.toBundle());
        } catch (RuntimeException e) {
            Log.w(TAG, "the app could not open beside the game", e);
            if (handle == null) {
                SecondScreen.lend(false);
            }
            close(false);
            Toast.makeText(activity, "The app could not be opened", Toast.LENGTH_LONG).show();
        }
    }

    static void hosted(AppHost activity) {
        if (handle == null) {
            // Nobody waits for it any more (the main activity was stopped in between).
            activity.finish();
            return;
        }
        host = activity;
    }

    static void unhosted(AppHost activity) {
        if (host == activity) {
            host = null;
        }
    }

    /** From AppHost: the app over it is gone, or could not be opened ("problem"). */
    static void left(AppHost activity, String problem) {
        if (host != activity) {
            activity.finish();
            return;
        }
        Activity owner = main;
        close(true);
        if (problem != null && owner != null) {
            Toast.makeText(owner, problem, Toast.LENGTH_LONG).show();
        }
    }

    /** The main activity is no longer shown: the quick settings are its own again. */
    static void onStop() {
        close(false);
    }

    // The quick settings back on their display; with "focus" the keys and the pad go back to
    // the main activity as well.
    private static void close(boolean focus) {
        Handle shown = handle;
        handle = null;
        if (shown != null) {
            try {
                shown.dismiss();
            } catch (RuntimeException e) {
                Log.w(TAG, "the handle was already gone", e);
            }
        }
        AppHost under = host;
        host = null;
        if (under != null) {
            under.finish();
            under.overridePendingTransition(0, 0);
        }
        Activity owner = main;
        main = null;
        if (shown != null) {
            SecondScreen.lend(false);
            if (focus && owner != null) {
                FocusHop.home(owner);
            }
        }
    }

    /** The one control of ours over the other app: brings the quick settings back. */
    private static final class Handle extends Presentation {
        Handle(Context context, Display display) {
            super(context, display, android.R.style.Theme_Translucent_NoTitleBar);
        }

        @Override
        protected void onCreate(Bundle savedInstanceState) {
            super.onCreate(savedInstanceState);
            setCancelable(false);
            Context context = getContext();
            float dp = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 1,
                    context.getResources().getDisplayMetrics());
            TextView label = new TextView(context);
            label.setText("Quick settings");
            label.setTextColor(Color.WHITE);
            label.setTextSize(TypedValue.COMPLEX_UNIT_DIP, 15);
            label.setGravity(Gravity.CENTER);
            label.setPadding((int) (18 * dp), (int) (7 * dp), (int) (18 * dp), (int) (8 * dp));
            GradientDrawable pill = new GradientDrawable();
            pill.setColor(Color.argb(215, 24, 24, 24));
            pill.setStroke(Math.max(1, (int) dp), Color.argb(140, 255, 255, 255));
            // Round at the bottom only: it hangs from the edge of the screen.
            float r = 14 * dp;
            pill.setCornerRadii(new float[] {0, 0, 0, 0, r, r, r, r});
            label.setBackground(pill);
            label.setOnClickListener(view -> close(true));
            setContentView(label, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT));
            Window window = getWindow();
            if (window != null) {
                window.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
                window.clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
                window.addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                        | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
                        | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS);
                window.setGravity(Gravity.TOP | Gravity.CENTER_HORIZONTAL);
                window.setLayout(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            }
        }
    }
}
