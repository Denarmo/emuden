// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.app.Activity;
import android.view.Display;
import android.view.Window;
import android.view.WindowManager;

import java.util.ArrayList;
import java.util.List;

/**
 * The refresh rate of the display the main window is on, for black frame insertion
 * (platform/src/android/refresh_android.cpp): its modes at the current size, and the
 * window's preferred mode.
 */
final class DisplayRefresh {
    private DisplayRefresh() {}

    @SuppressWarnings("deprecation")
    private static Display display(Activity activity) {
        return activity.getWindowManager().getDefaultDisplay();
    }

    /** The rate it runs at, then every rate it offers at its current size. */
    static float[] rates(Activity activity) {
        Display display = display(activity);
        Display.Mode current = display.getMode();
        List<Float> rates = new ArrayList<>();
        rates.add(display.getRefreshRate());
        for (Display.Mode mode : display.getSupportedModes()) {
            if (mode.getPhysicalWidth() == current.getPhysicalWidth()
                    && mode.getPhysicalHeight() == current.getPhysicalHeight()) {
                rates.add(mode.getRefreshRate());
            }
        }
        float[] out = new float[rates.size()];
        for (int i = 0; i < out.length; i++) {
            out[i] = rates.get(i);
        }
        return out;
    }

    /** Prefers the mode at this rate (0: the system's own choice again). */
    static void request(Activity activity, float hz) {
        activity.runOnUiThread(() -> {
            Display display = display(activity);
            Display.Mode current = display.getMode();
            int id = 0;
            for (Display.Mode mode : display.getSupportedModes()) {
                if (hz > 0 && Math.abs(mode.getRefreshRate() - hz) < 0.5f
                        && mode.getPhysicalWidth() == current.getPhysicalWidth()
                        && mode.getPhysicalHeight() == current.getPhysicalHeight()) {
                    id = mode.getModeId();
                }
            }
            Window window = activity.getWindow();
            WindowManager.LayoutParams params = window.getAttributes();
            if (params.preferredDisplayModeId != id) {
                params.preferredDisplayModeId = id;
                window.setAttributes(params);
            }
        });
    }
}
