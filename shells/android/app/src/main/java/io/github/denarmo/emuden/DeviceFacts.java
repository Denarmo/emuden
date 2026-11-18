// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Context;
import android.content.pm.PackageManager;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.util.DisplayMetrics;
import android.view.Display;
import android.view.InputDevice;

import java.util.Locale;

/**
 * What Android knows about the device, as "key=value" lines for the native platform layer
 * (platform/src/android/device_android.cpp). The default display comes first.
 */
public final class DeviceFacts {
    private DeviceFacts() {}

    public static String describe(Activity activity) {
        StringBuilder out = new StringBuilder();
        line(out, "manufacturer", Build.MANUFACTURER);
        line(out, "model", Build.MODEL);
        String soc = Build.VERSION.SDK_INT >= 31 ? Build.SOC_MODEL : "";
        line(out, "soc", soc == null || soc.isEmpty() || "unknown".equals(soc) ? Build.HARDWARE : soc);
        line(out, "release", Build.VERSION.RELEASE);
        line(out, "sdk", Integer.toString(Build.VERSION.SDK_INT));

        ActivityManager manager = (ActivityManager) activity.getSystemService(Context.ACTIVITY_SERVICE);
        if (manager != null) {
            ActivityManager.MemoryInfo memory = new ActivityManager.MemoryInfo();
            manager.getMemoryInfo(memory);
            line(out, "ram_mb", Long.toString(memory.totalMem / (1024 * 1024)));
        }
        boolean touch = activity.getPackageManager().hasSystemFeature(PackageManager.FEATURE_TOUCHSCREEN);
        line(out, "touch", touch ? "1" : "0");
        line(out, "pad", builtInPad() ? "1" : "0");

        DisplayManager displays = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
        if (displays != null) {
            Display[] all = displays.getDisplays();
            for (Display display : all) {
                if (display.getDisplayId() == Display.DEFAULT_DISPLAY) {
                    display(out, display);
                }
            }
            for (Display display : all) {
                if (display.getDisplayId() != Display.DEFAULT_DISPLAY) {
                    display(out, display);
                }
            }
        }
        return out.toString();
    }

    /** A gamepad that is part of the device, not one plugged in or paired. */
    private static boolean builtInPad() {
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (device == null || device.isVirtual()) {
                continue;
            }
            int sources = device.getSources();
            boolean pad = (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD;
            boolean external = Build.VERSION.SDK_INT >= 29 && device.isExternal();
            // Exclude virtual OEM daemons (e.g. uinput-xiaomi) that advertise SOURCE_GAMEPAD with
            // no axes and ControllerNumber 0.
            boolean realPad = !device.getMotionRanges().isEmpty() || device.getControllerNumber() > 0;
            if (pad && !external && realPad) {
                return true;
            }
        }
        return false;
    }

    @SuppressWarnings("deprecation")
    private static void display(StringBuilder out, Display display) {
        DisplayMetrics metrics = new DisplayMetrics();
        display.getRealMetrics(metrics);
        boolean turned = metrics.widthPixels < metrics.heightPixels;
        int width = Math.max(metrics.widthPixels, metrics.heightPixels);
        int height = Math.min(metrics.widthPixels, metrics.heightPixels);
        String name = display.getName() == null ? "" : display.getName().replace('|', ' ').replace('\n', ' ');
        line(out, "display", String.format(Locale.ROOT, "%s|%d|%d|%.2f|%.2f|%.2f|%.3f", name, width, height,
                display.getRefreshRate(), turned ? metrics.ydpi : metrics.xdpi,
                turned ? metrics.xdpi : metrics.ydpi, metrics.densityDpi / 160.0f));
    }

    private static void line(StringBuilder out, String key, String value) {
        out.append(key).append('=').append(value == null ? "" : value.replace('\n', ' ')).append('\n');
    }
}
