// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.ActivityOptions;
import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.drawable.Drawable;
import android.os.Build;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * The other apps of the device for the native platform layer
 * (platform/src/android/apps_android.cpp): the launcher's activities with their names and
 * icons, and opening one, on the other display when asked. The manifest's queries element
 * makes them visible; nothing here needs a permission.
 */
final class AppList {
    private static final String TAG = "EmuDEN";
    private static final int ICON_SIZE = 192;
    // What the native side passes as the display.
    private static final int DISPLAY_MAIN = 1, DISPLAY_SECOND = 2;

    private static BroadcastReceiver receiver;

    private AppList() {
    }

    /** Tells the native side whenever an app is installed, removed or changed. */
    static void watch(Context context) {
        if (receiver != null) {
            return;
        }
        receiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context c, Intent intent) {
                try {
                    nativeChanged();
                } catch (UnsatisfiedLinkError e) {
                    // Not loaded yet: the first list is still to come.
                }
            }
        };
        IntentFilter filter = new IntentFilter();
        filter.addAction(Intent.ACTION_PACKAGE_ADDED);
        filter.addAction(Intent.ACTION_PACKAGE_REMOVED);
        filter.addAction(Intent.ACTION_PACKAGE_CHANGED);
        filter.addAction(Intent.ACTION_PACKAGE_REPLACED);
        filter.addDataScheme("package");
        Context app = context.getApplicationContext();
        if (Build.VERSION.SDK_INT >= 33) {
            app.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            app.registerReceiver(receiver, filter);
        }
    }

    static void unwatch(Context context) {
        if (receiver != null) {
            context.getApplicationContext().unregisterReceiver(receiver);
            receiver = null;
        }
    }

    /** id, label and icon file of every launcher activity but our own, three strings each. */
    static String[] list(Context context) {
        PackageManager packages = context.getPackageManager();
        Intent launcher = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER);
        List<ResolveInfo> found = packages.queryIntentActivities(launcher, 0);
        File folder = new File(context.getCacheDir(), "app_icons");
        if (!folder.isDirectory() && !folder.mkdirs()) {
            Log.w(TAG, "no folder for the app icons");
        }
        String own = context.getPackageName();
        ArrayList<String> out = new ArrayList<>(found.size() * 3);
        Set<String> kept = new HashSet<>();
        for (ResolveInfo info : found) {
            ActivityInfo activity = info.activityInfo;
            if (activity == null || own.equals(activity.packageName)) {
                continue;
            }
            String id = activity.packageName + "/" + activity.name;
            CharSequence label = info.loadLabel(packages);
            File icon = new File(folder, id.replaceAll("[^A-Za-z0-9._-]", "_") + ".png");
            if (!icon.isFile() || icon.lastModified() < updated(packages, activity.packageName)) {
                render(info, packages, icon);
            }
            kept.add(icon.getName());
            out.add(id);
            out.add(label != null ? label.toString() : activity.packageName);
            out.add(icon.isFile() ? icon.getAbsolutePath() : "");
        }
        // Icons of apps that are gone.
        File[] files = folder.listFiles();
        if (files != null) {
            for (File file : files) {
                if (!kept.contains(file.getName())) {
                    file.delete();
                }
            }
        }
        return out.toArray(new String[0]);
    }

    private static long updated(PackageManager packages, String name) {
        try {
            return packages.getPackageInfo(name, 0).lastUpdateTime;
        } catch (PackageManager.NameNotFoundException e) {
            return 0;
        }
    }

    // An adaptive icon draws itself with the system's mask, so one path serves every kind.
    private static void render(ResolveInfo info, PackageManager packages, File file) {
        try {
            Drawable drawable = info.loadIcon(packages);
            if (drawable == null) {
                return;
            }
            Bitmap bitmap = Bitmap.createBitmap(ICON_SIZE, ICON_SIZE, Bitmap.Config.ARGB_8888);
            Canvas canvas = new Canvas(bitmap);
            drawable.setBounds(0, 0, ICON_SIZE, ICON_SIZE);
            drawable.draw(canvas);
            File part = new File(file.getPath() + ".part");
            try (OutputStream stream = new FileOutputStream(part)) {
                bitmap.compress(Bitmap.CompressFormat.PNG, 100, stream);
            }
            bitmap.recycle();
            if (!part.renameTo(file)) {
                part.delete();
            }
        } catch (Exception | OutOfMemoryError e) {
            Log.w(TAG, "no icon for " + info.activityInfo.packageName, e);
        }
    }

    /**
     * Opens an app in a task of its own. Returns "" when it opened as asked, "note:..." when
     * it opened somewhere else than the display asked for, "error:..." when it did not open.
     */
    static String launch(Activity activity, String id, int display) {
        int slash = id.indexOf('/');
        if (slash <= 0) {
            return "error:unknown app";
        }
        Intent intent = new Intent(Intent.ACTION_MAIN)
                .addCategory(Intent.CATEGORY_LAUNCHER)
                .setComponent(new ComponentName(id.substring(0, slash), id.substring(slash + 1)))
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_RESET_TASK_IF_NEEDED);
        int target = -1;
        if (display == DISPLAY_SECOND) {
            target = SecondScreen.otherDisplayId(activity);
        } else if (display == DISPLAY_MAIN) {
            target = SecondScreen.ownDisplayId(activity);
        }
        String note = "";
        if (target >= 0 && display == DISPLAY_SECOND) {
            ActivityManager manager = (ActivityManager) activity.getSystemService(Context.ACTIVITY_SERVICE);
            boolean allowed = Build.VERSION.SDK_INT < 29
                    || (manager != null && manager.isActivityStartAllowedOnDisplay(activity, target, intent));
            if (allowed) {
                try {
                    ActivityOptions options = ActivityOptions.makeBasic();
                    options.setLaunchDisplayId(target);
                    activity.startActivity(intent, options.toBundle());
                    return "";
                } catch (RuntimeException e) {
                    Log.w(TAG, "the app could not open on display " + target, e);
                }
            }
            note = "note:This screen cannot show other apps, so it opened on the main one";
        } else if (display == DISPLAY_SECOND) {
            note = "note:There is no second screen, so it opened on this one";
        }
        try {
            if (target >= 0 && display == DISPLAY_MAIN) {
                ActivityOptions options = ActivityOptions.makeBasic();
                options.setLaunchDisplayId(target);
                activity.startActivity(intent, options.toBundle());
            } else {
                activity.startActivity(intent);
            }
            return note;
        } catch (RuntimeException e) {
            Log.w(TAG, "the app could not be opened", e);
            return "error:The app could not be opened";
        }
    }

    private static native void nativeChanged();

    /** The activity's window has the input focus, or lost it to another window. */
    static native void nativeFocus(boolean focused);
}
