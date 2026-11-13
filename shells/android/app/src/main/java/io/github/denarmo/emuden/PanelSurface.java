// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.content.Context;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;

/**
 * What the app draws on the second display: one surface, handed to the native platform layer
 * (platform/src/android/second_display_android.cpp) with its touches. PanelActivity holds it,
 * or the Presentation of SecondScreen where the display takes no activities.
 */
final class PanelSurface extends SurfaceView implements SurfaceHolder.Callback, View.OnTouchListener {
    private int pointer = -1;

    PanelSurface(Context context) {
        super(context);
        getHolder().addCallback(this);
        setOnTouchListener(this);
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        SecondScreen.surface(holder.getSurface(), width, height);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        // Returns once the native side has let go of the surface.
        SecondScreen.surface(null, 0, 0);
    }

    @Override
    public boolean onTouch(View view, MotionEvent event) {
        int index = event.getActionIndex();
        switch (event.getActionMasked()) {
        case MotionEvent.ACTION_DOWN:
            pointer = event.getPointerId(index);
            SecondScreen.touch(0, event.getX(index), event.getY(index));
            break;
        case MotionEvent.ACTION_MOVE: {
            int at = event.findPointerIndex(pointer);
            if (at >= 0) {
                SecondScreen.touch(1, event.getX(at), event.getY(at));
            }
            break;
        }
        case MotionEvent.ACTION_POINTER_UP:
            if (event.getPointerId(index) != pointer) {
                break;
            }
            // The first finger left: the press ends with it.
        case MotionEvent.ACTION_UP:
            if (pointer >= 0) {
                SecondScreen.touch(2, event.getX(index), event.getY(index));
                pointer = -1;
            }
            break;
        case MotionEvent.ACTION_CANCEL:
            if (pointer >= 0) {
                // Ends the press somewhere it cannot count as a tap.
                SecondScreen.touch(2, -100000.0f, -100000.0f);
                pointer = -1;
            }
            break;
        default:
            break;
        }
        return true;
    }
}
