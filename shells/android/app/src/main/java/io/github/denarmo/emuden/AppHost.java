// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.app.Activity;
import android.app.ActivityOptions;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.util.Log;
import android.view.View;

/**
 * Lies under an app opened beside the game (AppBeside), on the same display. Android tells
 * nobody when another app is left, but it shows what was under it: when this activity comes
 * up a second time the app is gone, and the quick settings take the display again.
 */
public class AppHost extends Activity {
    static final String EXTRA_TARGET = "target";
    private static final String TAG = "EmuDEN";
    private static final String STATE_OPENED = "opened";

    private boolean opened;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        opened = savedInstanceState != null && savedInstanceState.getBoolean(STATE_OPENED);
        View black = new View(this);
        black.setBackgroundColor(Color.BLACK);
        setContentView(black);
        AppBeside.hosted(this);
    }

    @Override
    protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        state.putBoolean(STATE_OPENED, opened);
    }

    @Override
    @SuppressWarnings("deprecation")
    protected void onResume() {
        super.onResume();
        if (isFinishing()) {
            return;
        }
        if (opened) {
            AppBeside.left(this, null);
            return;
        }
        opened = true;
        Intent target = getIntent() != null ? getIntent().getParcelableExtra(EXTRA_TARGET) : null;
        try {
            if (target == null) {
                throw new IllegalStateException("no app to open");
            }
            ActivityOptions options = ActivityOptions.makeBasic();
            options.setLaunchDisplayId(getWindowManager().getDefaultDisplay().getDisplayId());
            startActivity(target, options.toBundle());
        } catch (RuntimeException e) {
            Log.w(TAG, "the app could not be opened", e);
            AppBeside.left(this, "The app could not be opened");
        }
    }

    @Override
    protected void onDestroy() {
        AppBeside.unhosted(this);
        super.onDestroy();
    }
}
