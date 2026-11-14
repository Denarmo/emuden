// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.app.Activity;
import android.app.ActivityOptions;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageInstaller;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.content.pm.Signature;
import android.content.pm.SigningInfo;
import android.net.Uri;
import android.os.Build;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.security.MessageDigest;
import java.util.List;

/**
 * Engine apps for the native platform layer (platform/src/android/engine_apps_android.cpp):
 * the Emulator Control Protocol's Android side. An engine is an app with an activity for
 * io.github.ecp.action.PLAY; it is started with an explicit intent once its signing
 * certificate is the one the plugin manifest pinned.
 */
final class EngineApps {
    private static final String TAG = "EmuDEN";
    static final String ACTION_PLAY = "io.github.ecp.action.PLAY";
    private static final String EXTRA = "io.github.ecp.extra.";
    private static final String ACTION_INSTALLED = "io.github.denarmo.emuden.ENGINE_INSTALLED";
    private static final int DISPLAY_MAIN = 1, DISPLAY_SECOND = 2;

    private static BroadcastReceiver installReceiver;

    private EngineApps() {
    }

    /** "cert|versionCode|plays|versionName", or "" when the package is not installed. */
    static String query(Context context, String pkg) {
        PackageManager packages = context.getPackageManager();
        try {
            PackageInfo info = packages.getPackageInfo(pkg, PackageManager.GET_SIGNING_CERTIFICATES);
            long code = Build.VERSION.SDK_INT >= 28 ? info.getLongVersionCode() : info.versionCode;
            String name = info.versionName != null ? info.versionName : "";
            boolean plays = playActivity(packages, pkg) != null;
            return certOf(info) + "|" + code + "|" + (plays ? "1" : "0") + "|" + name;
        } catch (PackageManager.NameNotFoundException e) {
            return "";
        }
    }

    // The current signer only: an app with several signers (or a rotated key with lineage)
    // has one current certificate, which is what the manifest pins.
    private static String certOf(PackageInfo info) {
        SigningInfo signing = info.signingInfo;
        if (signing == null) {
            return "";
        }
        Signature[] signers = signing.hasMultipleSigners() ? signing.getApkContentsSigners()
                : signing.getSigningCertificateHistory();
        if (signers == null || signers.length == 0) {
            return "";
        }
        if (signing.hasMultipleSigners() && signers.length != 1) {
            return ""; // never matched: several signers are not supported
        }
        Signature current = signers[signers.length - 1];
        try {
            byte[] digest = MessageDigest.getInstance("SHA-256").digest(current.toByteArray());
            StringBuilder hex = new StringBuilder(digest.length * 2);
            for (byte b : digest) {
                hex.append(String.format("%02x", b));
            }
            return hex.toString();
        } catch (Exception e) {
            return "";
        }
    }

    private static ComponentName playActivity(PackageManager packages, String pkg) {
        Intent intent = new Intent(ACTION_PLAY).addCategory(Intent.CATEGORY_DEFAULT).setPackage(pkg);
        List<ResolveInfo> found = packages.queryIntentActivities(intent, 0);
        for (ResolveInfo info : found) {
            ActivityInfo activity = info.activityInfo;
            if (activity != null && pkg.equals(activity.packageName)) {
                return new ComponentName(activity.packageName, activity.name);
            }
        }
        return null;
    }

    /** Hands an APK to the system installer through a session; the user confirms. */
    static String install(Activity activity, String path) {
        File apk = new File(path);
        if (!apk.isFile()) {
            return "error:The download is missing";
        }
        if (Build.VERSION.SDK_INT >= 26 && !activity.getPackageManager().canRequestPackageInstalls()) {
            // The user allows EmuDEN to install apps first; the next try goes on from there.
            try {
                activity.startActivity(new Intent(android.provider.Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                        Uri.parse("package:" + activity.getPackageName())));
            } catch (RuntimeException e) {
                Log.w(TAG, "no settings page for installing apps", e);
            }
            return "error:Allow EmuDEN to install apps, then install the engine again";
        }
        watchInstalls(activity);
        PackageInstaller installer = activity.getPackageManager().getPackageInstaller();
        PackageInstaller.SessionParams params =
                new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        params.setSize(apk.length());
        try {
            int id = installer.createSession(params);
            try (PackageInstaller.Session session = installer.openSession(id)) {
                try (InputStream in = new FileInputStream(apk);
                     OutputStream out = session.openWrite("engine.apk", 0, apk.length())) {
                    byte[] buffer = new byte[1 << 16];
                    int read;
                    while ((read = in.read(buffer)) > 0) {
                        out.write(buffer, 0, read);
                    }
                    session.fsync(out);
                }
                Intent done = new Intent(ACTION_INSTALLED).setPackage(activity.getPackageName());
                int flags = PendingIntent.FLAG_UPDATE_CURRENT
                        | (Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0);
                PendingIntent status = PendingIntent.getBroadcast(activity, id, done, flags);
                session.commit(status.getIntentSender());
            }
            return "";
        } catch (Exception e) {
            Log.w(TAG, "the engine could not be handed to the installer", e);
            return "error:The installer could not be started";
        }
    }

    // The installer's answers: it needs the user (shown at once), or it is done.
    private static void watchInstalls(Context context) {
        if (installReceiver != null) {
            return;
        }
        installReceiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context c, Intent intent) {
                int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
                if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
                    Intent confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT);
                    if (confirm != null) {
                        confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                        try {
                            c.startActivity(confirm);
                        } catch (RuntimeException e) {
                            Log.w(TAG, "the installer's question could not be shown", e);
                        }
                    }
                } else if (status != PackageInstaller.STATUS_SUCCESS) {
                    Log.w(TAG, "engine install: " + intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE));
                }
            }
        };
        IntentFilter filter = new IntentFilter(ACTION_INSTALLED);
        Context app = context.getApplicationContext();
        if (Build.VERSION.SDK_INT >= 33) {
            app.registerReceiver(installReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            app.registerReceiver(installReceiver, filter);
        }
    }

    static String store(Activity activity, String url) {
        try {
            activity.startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            return "";
        } catch (RuntimeException e) {
            return "error:No app can open the store page";
        }
    }

    /** Starts a game in the engine: section 3 of the protocol. */
    static String play(Activity activity, String pkg, String cert, String path, int port, String token,
                       String session, boolean fullscreen, int display) {
        String installed = query(activity, pkg);
        if (installed.isEmpty()) {
            return "error:The engine app is not installed";
        }
        if (cert.isEmpty() || !installed.startsWith(cert + "|")) {
            return "error:The engine app is not the one the plugin names (its signature differs)";
        }
        ComponentName target = playActivity(activity.getPackageManager(), pkg);
        if (target == null) {
            return "error:The engine app cannot be started by EmuDEN";
        }
        Uri game = GameProvider.uriFor(activity, new File(path));
        Intent menu = new Intent(activity, EmuDenActivity.class)
                .setAction(Intent.ACTION_MAIN)
                .addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
        PendingIntent menuIntent = PendingIntent.getActivity(activity, 0, menu,
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        Intent intent = new Intent(ACTION_PLAY)
                .setComponent(target)
                .setDataAndType(game, "application/octet-stream")
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_GRANT_READ_URI_PERMISSION)
                .putExtra(EXTRA + "PORT", port)
                .putExtra(EXTRA + "TOKEN", token)
                .putExtra(EXTRA + "SESSION", session)
                .putExtra(EXTRA + "MENU_INTENT", menuIntent)
                .putExtra(EXTRA + "FULLSCREEN", fullscreen);
        return start(activity, intent, display);
    }

    /** After the menu: the engine's activity comes back to the front (section 6.2). */
    static String resume(Activity activity, String pkg, int display) {
        ComponentName target = playActivity(activity.getPackageManager(), pkg);
        if (target == null) {
            return "error:The engine app is gone";
        }
        Intent intent = new Intent(ACTION_PLAY)
                .setComponent(target)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
        return start(activity, intent, display);
    }

    private static String start(Activity activity, Intent intent, int display) {
        int target = display == DISPLAY_SECOND ? SecondScreen.otherDisplayId(activity)
                : display == DISPLAY_MAIN ? SecondScreen.ownDisplayId(activity) : -1;
        try {
            if (target >= 0) {
                ActivityOptions options = ActivityOptions.makeBasic();
                options.setLaunchDisplayId(target);
                activity.startActivity(intent, options.toBundle());
            } else {
                activity.startActivity(intent);
            }
            return "";
        } catch (RuntimeException e) {
            Log.w(TAG, "the engine could not be started", e);
            return "error:The engine app could not be started";
        }
    }
}
