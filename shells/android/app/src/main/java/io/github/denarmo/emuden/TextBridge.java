// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.app.Activity;
import android.app.Dialog;
import android.app.Presentation;
import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.InputFilter;
import android.text.InputType;
import android.text.TextWatcher;
import android.util.Log;
import android.view.Display;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.FrameLayout;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLControllerManager;

/**
 * The system keyboard for a text the app asks for (platform/src/android/text_input_android.cpp).
 * A window that can take the focus is opened on the display the keyboard should be on (the
 * second one when asked and there, else the activity's own) with one invisible text field;
 * the keyboard is shown for it, and what is typed, the keyboard's done key and Back go to the
 * native side, which draws the text itself. The window is one pixel wide, so touches still
 * reach what is under it. Pad keys and sticks arriving here, because this window has the
 * focus, are handed on to SDL: the pad keeps driving the app.
 */
final class TextBridge {
    private static final String TAG = "EmuDEN";

    private static TextBridge instance;

    private final Activity activity;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private Dialog dialog;
    private EditText edit;
    private int session;
    private boolean quiet;   // the text is being set from the native side
    private boolean visible; // the keyboard was seen on screen in this session

    TextBridge(Activity activity) {
        this.activity = activity;
        instance = this;
    }

    // From the native side, on its own thread.
    static void begin(int session, String text, String hint, int maxLength, boolean second) {
        TextBridge self = instance;
        if (self != null) {
            self.ui.post(() -> self.open(session, text, hint, maxLength, second));
        }
    }

    static void setText(int session, String text) {
        TextBridge self = instance;
        if (self != null) {
            self.ui.post(() -> {
                if (self.edit != null && self.session == session) {
                    self.quiet = true;
                    self.edit.setText(text);
                    self.edit.setSelection(self.edit.length());
                    self.quiet = false;
                }
            });
        }
    }

    static void end(int session) {
        TextBridge self = instance;
        if (self != null) {
            self.ui.post(() -> {
                if (self.session == session) {
                    self.close();
                }
            });
        }
    }

    /** The activity is no longer shown: the keyboard goes with it. */
    void onStop() {
        if (dialog != null) {
            int ended = session;
            close();
            nativeHidden(ended);
        }
    }

    private void open(int id, String text, String hint, int maxLength, boolean second) {
        close();
        session = id;
        visible = false;
        Display target = second ? SecondScreen.otherDisplay(activity) : null;
        try {
            int theme = android.R.style.Theme_Translucent_NoTitleBar;
            Dialog made = target != null ? new Presentation(activity, target, theme) : new Dialog(activity, theme);
            EditText field = makeField(made.getContext(), id, text, hint, maxLength);
            FrameLayout root = new FrameLayout(made.getContext());
            root.addView(field, new FrameLayout.LayoutParams(1, 1));
            made.setContentView(root);
            made.setCancelable(false);
            made.setOnKeyListener((d, code, event) -> onKey(id, code, event));
            made.setOnDismissListener(d -> {
                // The display went away under the keyboard.
                if (dialog == made) {
                    dialog = null;
                    edit = null;
                    nativeHidden(id);
                }
            });
            Window window = made.getWindow();
            if (window != null) {
                window.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
                window.clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
                window.addFlags(WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL);
                window.setGravity(Gravity.START | Gravity.TOP);
                window.setLayout(1, ViewGroup.LayoutParams.MATCH_PARENT);
                window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE
                        | WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
                if (Build.VERSION.SDK_INT >= 30) {
                    window.setDecorFitsSystemWindows(false);
                }
                window.getDecorView().setOnApplyWindowInsetsListener((view, insets) -> {
                    onInsets(id, view, insets);
                    return insets;
                });
            }
            dialog = made;
            edit = field;
            made.show();
            field.requestFocus();
            // Once the window has the focus; some keyboards ignore the window's own request.
            field.postDelayed(() -> {
                if (edit == field) {
                    InputMethodManager manager =
                            (InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE);
                    if (manager != null) {
                        manager.showSoftInput(field, 0);
                    }
                }
            }, 120);
        } catch (RuntimeException e) {
            // The display does not take such a window: the native side sees no keyboard
            // appear and shows its own keys.
            Log.w(TAG, "the keyboard window could not be opened", e);
            dialog = null;
            edit = null;
        }
    }

    private EditText makeField(Context context, int id, String text, String hint, int maxLength) {
        EditText field = new EditText(context);
        field.setSingleLine(true);
        field.setInputType(InputType.TYPE_CLASS_TEXT);
        field.setImeOptions(EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI
                | EditorInfo.IME_FLAG_NO_FULLSCREEN);
        field.setFilters(new InputFilter[] {new InputFilter.LengthFilter(Math.max(1, maxLength))});
        field.setHint(hint);
        field.setText(text);
        field.setSelection(field.length());
        field.setBackground(null);
        field.setTextColor(Color.TRANSPARENT);
        field.setHintTextColor(Color.TRANSPARENT);
        field.setCursorVisible(false);
        field.setPadding(0, 0, 0, 0);
        field.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {
            }

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {
            }

            @Override
            public void afterTextChanged(Editable s) {
                if (!quiet && session == id) {
                    nativeText(id, s.toString());
                }
            }
        });
        field.setOnEditorActionListener((view, action, event) -> {
            // A hardware Enter arrives as down and up: once is enough.
            if (event == null || event.getAction() == KeyEvent.ACTION_UP) {
                nativeDone(id);
            }
            return true;
        });
        field.setOnGenericMotionListener((view, event) -> onMotion(event));
        return field;
    }

    private boolean onKey(int id, int code, KeyEvent event) {
        if (code == KeyEvent.KEYCODE_BACK) {
            if (event.getAction() == KeyEvent.ACTION_UP && !event.isCanceled()) {
                nativeCancel(id);
            }
            return true;
        }
        int source = event.getSource();
        boolean pad = (source & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (source & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
                || KeyEvent.isGamepadButton(code);
        if (pad) {
            // This window has the focus, the pad belongs to the app.
            try {
                return SDLActivity.handleKeyEvent(edit, code, event, null);
            } catch (UnsatisfiedLinkError e) {
                return true;
            }
        }
        return false;
    }

    private static boolean onMotion(MotionEvent event) {
        if ((event.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) {
            try {
                return SDLControllerManager.handleJoystickMotionEvent(event);
            } catch (UnsatisfiedLinkError e) {
                return true;
            }
        }
        return false;
    }

    @SuppressWarnings("deprecation")
    private void onInsets(int id, View view, WindowInsets insets) {
        if (session != id || dialog == null) {
            return;
        }
        int height = view.getRootView().getHeight();
        int bottom;
        boolean shown;
        if (Build.VERSION.SDK_INT >= 30) {
            bottom = insets.getInsets(WindowInsets.Type.ime()).bottom;
            shown = insets.isVisible(WindowInsets.Type.ime()) && bottom > 0;
        } else {
            // Before Android 11 the keyboard is the only thing that takes this much.
            bottom = insets.getSystemWindowInsetBottom();
            shown = height > 0 && bottom > height * 0.15f;
        }
        float share = shown && height > 0 ? Math.min(1.0f, (float) bottom / height) : 0.0f;
        nativeCovered(id, share);
        if (shown && !visible) {
            visible = true;
            nativeShown(id);
        } else if (!shown && visible) {
            // Put away by the user (Back closes the keyboard first) or by a touch on the
            // other display.
            visible = false;
            nativeHidden(id);
        }
    }

    private void close() {
        Dialog open = dialog;
        dialog = null;
        edit = null;
        visible = false;
        if (open != null) {
            try {
                open.dismiss();
            } catch (RuntimeException e) {
                Log.w(TAG, "the keyboard window was already gone", e);
            }
        }
    }

    private static native void nativeShown(int session);

    private static native void nativeHidden(int session);

    private static native void nativeText(int session, String text);

    private static native void nativeDone(int session);

    private static native void nativeCancel(int session);

    private static native void nativeCovered(int session, float share);
}
