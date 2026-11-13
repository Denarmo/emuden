// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.content.Context;
import android.database.ContentObserver;
import android.media.AudioManager;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;

/**
 * The second screen's volume in the Retroid Pocket Duo's dual-screen panel. The device's
 * settings service turns that level into a gain for the audio stack and asks for it to be
 * applied by setting the music volume to the value it already has, which Android ignores:
 * the new level was only heard once the main volume changed. While this app runs, a change
 * of the level moves the music volume one step and back, which applies it.
 */
final class ScreenVolume {
    private static final String LEVEL = "secondary_screen_volume_level";
    private static final long SETTLE_MS = 300; // the settings service sets the gain first

    private static ContentObserver observer;

    private ScreenVolume() {
    }

    static void watch(Context context) {
        if (observer != null || Settings.System.getString(context.getContentResolver(), LEVEL) == null) {
            return; // not a device with that panel
        }
        Context app = context.getApplicationContext();
        Handler ui = new Handler(Looper.getMainLooper());
        Runnable apply = () -> nudge(app);
        observer = new ContentObserver(ui) {
            @Override
            public void onChange(boolean selfChange, Uri uri) {
                // A slider sends many changes: one nudge once it stops.
                ui.removeCallbacks(apply);
                ui.postDelayed(apply, SETTLE_MS);
            }
        };
        app.getContentResolver().registerContentObserver(Settings.System.getUriFor(LEVEL), false, observer);
    }

    static void unwatch(Context context) {
        if (observer != null) {
            context.getApplicationContext().getContentResolver().unregisterContentObserver(observer);
            observer = null;
        }
    }

    private static void nudge(Context context) {
        AudioManager audio = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
        if (audio == null || audio.isVolumeFixed()) {
            return;
        }
        int stream = AudioManager.STREAM_MUSIC;
        int now = audio.getStreamVolume(stream);
        // Down rather than up where it can: a moment quieter, never louder.
        int other = now > audio.getStreamMinVolume(stream) ? now - 1 : now + 1;
        if (other > audio.getStreamMaxVolume(stream)) {
            return;
        }
        try {
            audio.setStreamVolume(stream, other, 0);
            audio.setStreamVolume(stream, now, 0);
        } catch (SecurityException e) {
            // Do not disturb holds the volume: nothing to apply now.
        }
    }
}
