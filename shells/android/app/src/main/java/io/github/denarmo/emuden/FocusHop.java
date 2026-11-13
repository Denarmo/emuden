// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.app.Activity;
import android.app.ActivityOptions;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;

/**
 * Moves the input focus to a display. Android gives keys, the pad and the keyboard to the
 * focused window of one display at a time, and only a touch or an activity started there
 * moves that: this activity is started on the display, shows nothing and ends at once, which
 * leaves the display as the focused one. The way back is the main activity brought to the
 * front on its own display.
 */
public class FocusHop extends Activity {
    private static final String TAG = "EmuDEN";

    private static Runnable arrived;

    /** Runs "then" once the display has the focus. False when the display takes no activity. */
    static boolean to(Activity from, int displayId, Runnable then) {
        try {
            Intent intent = new Intent(from, FocusHop.class)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_MULTIPLE_TASK
                            | Intent.FLAG_ACTIVITY_NO_ANIMATION);
            ActivityOptions options = ActivityOptions.makeBasic();
            options.setLaunchDisplayId(displayId);
            arrived = then;
            from.startActivity(intent, options.toBundle());
            return true;
        } catch (RuntimeException e) {
            Log.w(TAG, "the focus could not move to display " + displayId, e);
            arrived = null;
            return false;
        }
    }

    /** The keys and the pad back to the main activity, on the display it is on. */
    static void home(Activity main) {
        try {
            main.startActivity(new Intent(main, main.getClass())
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT
                            | Intent.FLAG_ACTIVITY_NO_ANIMATION));
        } catch (RuntimeException e) {
            Log.w(TAG, "the focus could not come back", e);
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Runnable then = arrived;
        arrived = null;
        finish();
        overridePendingTransition(0, 0);
        if (then != null) {
            then.run();
        }
    }
}
