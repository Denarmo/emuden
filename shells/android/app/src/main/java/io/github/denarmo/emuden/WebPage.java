// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.ViewGroup;
import android.webkit.GeolocationPermissions;
import android.webkit.PermissionRequest;
import android.webkit.RenderProcessGoneDetail;
import android.webkit.ValueCallback;
import android.webkit.WebBackForwardList;
import android.webkit.WebChromeClient;
import android.webkit.WebHistoryItem;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.RelativeLayout;

import java.nio.charset.StandardCharsets;
import java.util.HashMap;

/**
 * The in-app browser's page: an android.webkit.WebView in the activity's layout, above the
 * app's surface, for platform/src/android/webview_android.cpp. The user browses; the native
 * side decides where the top-level page may go and whether a download the user started is
 * kept. Nothing here reads a page, and nothing here decides anything.
 *
 * Commands come from the app's thread and are carried out on the UI thread in their order.
 */
final class WebPage {
    private static final String TAG = "EmuDEN";
    // webview_android.cpp has the same numbers.
    private static final int CREATE = 0, DESTROY = 1, BOUNDS = 2, VISIBLE = 3, LOAD = 4, BACK = 5, FORWARD = 6,
            RELOAD = 7, STOP = 8, POINTER = 9, CLICK = 10, SCROLL = 11, TEXT = 12, KEY = 13, ZOOM = 14, FETCH = 15,
            CANCEL = 16;
    private static final int LINK = 0, REDIRECT = 1, PAGE_START = 2;

    private static final Handler ui = new Handler(Looper.getMainLooper());
    private static final HashMap<Long, WebPage> pages = new HashMap<>(); // UI thread only

    private final long handle;
    private final WebView view;
    private final Pointer pointer;
    private final WebDownloads downloads;
    private boolean loading;
    private boolean shown;
    private String refused; // the page that was just undone, so it is reported once
    private float zoom = 1.0f;
    private int left, top; // the view's place in the layout
    private int pointerX, pointerY; // inside the view

    static boolean available() {
        try {
            return WebView.getCurrentWebViewPackage() != null;
        } catch (Throwable e) {
            return false;
        }
    }

    static void command(long handle, int op, int a, int b, int c, int d, byte[] s, byte[] t) {
        ui.post(() -> {
            try {
                run(handle, op, a, b, c, d, text(s), text(t));
            } catch (RuntimeException e) {
                Log.w(TAG, "the browser view failed", e);
            }
        });
    }

    /** A page is showing: pad input goes to the app even when the page has the focus. */
    static boolean showing() {
        for (WebPage page : pages.values()) {
            if (page.shown) {
                return true;
            }
        }
        return false;
    }

    static void onActivityStart() {
        for (WebPage page : pages.values()) {
            page.view.onResume();
        }
    }

    static void onActivityStop() {
        for (WebPage page : pages.values()) {
            page.view.onPause();
        }
    }

    static void onActivityDestroy() {
        for (WebPage page : pages.values()) {
            page.close();
        }
        pages.clear();
    }

    private static void run(long handle, int op, int a, int b, int c, int d, String s, String t) {
        if (op == CREATE) {
            Activity activity = EmuDenActivity.current();
            ViewGroup layout = EmuDenActivity.webLayout();
            if (activity != null && layout != null) {
                pages.put(handle, new WebPage(handle, activity, layout, a, b, c, d));
            }
            return;
        }
        WebPage page = pages.get(handle);
        if (page == null) {
            return;
        }
        switch (op) {
        case DESTROY:
            pages.remove(handle);
            page.close();
            break;
        case BOUNDS:
            page.place(a, b, c, d);
            break;
        case VISIBLE:
            page.show(a != 0);
            break;
        case LOAD:
            page.view.loadUrl(s);
            break;
        case BACK:
            page.view.goBack();
            break;
        case FORWARD:
            page.view.goForward();
            break;
        case RELOAD:
            page.view.reload();
            break;
        case STOP:
            page.view.stopLoading();
            break;
        case POINTER:
            page.pointer.setVisibility(a != 0 && page.shown ? View.VISIBLE : View.INVISIBLE);
            page.pointer.setTag(a != 0);
            page.pointerX = b;
            page.pointerY = c;
            page.movePointer();
            break;
        case CLICK:
            page.click(a, b);
            break;
        case SCROLL:
            page.scroll(a, b, c, d);
            break;
        case TEXT:
            page.type(s);
            break;
        case KEY:
            page.key(a);
            break;
        case ZOOM:
            page.zoom(a / 100.0f);
            break;
        case FETCH: {
            int line = t.indexOf('\n');
            page.downloads.fetch(a, s, line < 0 ? "" : t.substring(line + 1), line < 0 ? t : t.substring(0, line));
            break;
        }
        case CANCEL:
            page.downloads.cancel(a);
            break;
        default:
            break;
        }
    }

    private WebPage(long handle, Activity activity, ViewGroup layout, int x, int y, int w, int h) {
        this.handle = handle;
        view = new WebView(activity);
        pointer = new Pointer(activity);
        downloads = new WebDownloads(handle, activity, view);

        WebSettings settings = view.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setAllowFileAccess(false);
        settings.setAllowContentAccess(false);
        settings.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        settings.setSafeBrowsingEnabled(true);
        settings.setGeolocationEnabled(false);
        settings.setSaveFormData(false);
        settings.setSupportMultipleWindows(false); // a link for a new window opens in this view
        settings.setJavaScriptCanOpenWindowsAutomatically(false);
        settings.setUseWideViewPort(true);
        settings.setLoadWithOverviewMode(true);
        settings.setBuiltInZoomControls(true);
        settings.setDisplayZoomControls(false);
        // The user agent stays the engine's own.

        view.setWebViewClient(new Client());
        view.setWebChromeClient(new Chrome());
        view.setDownloadListener((url, agent, disposition, mime, length) -> {
            if (url == null) {
                return;
            }
            if (url.startsWith("blob:") || url.startsWith("data:")) {
                downloads.made(url);
            } else {
                nativeDownload(handle, bytes(url), bytes(agent), bytes(disposition), bytes(mime), length);
            }
        });
        view.setFocusable(true);
        view.setFocusableInTouchMode(true);
        view.setVisibility(View.INVISIBLE);
        pointer.setVisibility(View.INVISIBLE);
        pointer.setTag(false);
        left = x;
        top = y;
        layout.addView(view, params(x, y, w, h));
        int size = Math.round(28 * activity.getResources().getDisplayMetrics().density);
        layout.addView(pointer, new RelativeLayout.LayoutParams(size, size));
        report();
    }

    private static RelativeLayout.LayoutParams params(int x, int y, int w, int h) {
        RelativeLayout.LayoutParams params = new RelativeLayout.LayoutParams(Math.max(w, 1), Math.max(h, 1));
        params.leftMargin = x;
        params.topMargin = y;
        return params;
    }

    private void place(int x, int y, int w, int h) {
        left = x;
        top = y;
        view.setLayoutParams(params(x, y, w, h));
        movePointer();
    }

    private void movePointer() {
        pointer.setTranslationX(left + pointerX);
        pointer.setTranslationY(top + pointerY);
    }

    private void show(boolean on) {
        shown = on;
        view.setVisibility(on ? View.VISIBLE : View.INVISIBLE);
        pointer.setVisibility(on && Boolean.TRUE.equals(pointer.getTag()) ? View.VISIBLE : View.INVISIBLE);
        if (!on && view.hasFocus()) {
            // The keyboard goes back to the app, whose own interface is what shows now.
            View surface = EmuDenActivity.webSurface();
            if (surface != null) {
                surface.requestFocus();
            }
        }
    }

    private void close() {
        downloads.close();
        ViewGroup parent = (ViewGroup) view.getParent();
        if (parent != null) {
            parent.removeView(view);
            parent.removeView(pointer);
        }
        View surface = EmuDenActivity.webSurface();
        if (surface != null) {
            surface.requestFocus();
        }
        view.stopLoading();
        view.setDownloadListener(null);
        view.setWebChromeClient(null);
        view.destroy();
    }

    private void report() {
        nativeState(handle, bytes(view.getUrl()), bytes(view.getTitle()), loading, view.canGoBack(),
                view.canGoForward());
    }

    // A page of a host that is not allowed has started (a redirect ended on it, or history
    // led to it): it is not shown.
    private boolean foreign(String url) {
        if (url == null) {
            return false;
        }
        if (!url.equals(refused)) {
            if (nativeNavigate(handle, bytes(url), PAGE_START, false)) {
                refused = null;
                return false;
            }
            refused = url;
        }
        view.stopLoading();
        WebBackForwardList history = view.copyBackForwardList();
        WebHistoryItem current = history.getCurrentItem();
        if (current != null && url.equals(current.getUrl())) {
            if (view.canGoBack()) {
                view.goBack();
            } else {
                view.loadUrl("about:blank");
            }
        }
        return true;
    }

    // Pad input: the events a finger, a wheel and a keyboard would send, given to the view.
    private void click(int x, int y) {
        if (!shown) {
            return;
        }
        if (!view.hasFocus()) {
            view.requestFocus();
        }
        long down = SystemClock.uptimeMillis();
        touch(down, down, MotionEvent.ACTION_DOWN, x, y);
        ui.postDelayed(() -> touch(down, SystemClock.uptimeMillis(), MotionEvent.ACTION_UP, x, y), 50);
    }

    private void touch(long down, long now, int action, int x, int y) {
        MotionEvent event = MotionEvent.obtain(down, now, action, x, y, 0);
        event.setSource(InputDevice.SOURCE_TOUCHSCREEN);
        view.dispatchTouchEvent(event);
        event.recycle();
    }

    private void scroll(int x, int y, int dx, int dy) {
        ViewConfiguration configuration = ViewConfiguration.get(view.getContext());
        MotionEvent.PointerProperties[] properties = {new MotionEvent.PointerProperties()};
        properties[0].id = 0;
        properties[0].toolType = MotionEvent.TOOL_TYPE_MOUSE;
        MotionEvent.PointerCoords[] coords = {new MotionEvent.PointerCoords()};
        coords[0].x = x;
        coords[0].y = y;
        // A wheel turned away from the user (positive) scrolls up.
        coords[0].setAxisValue(MotionEvent.AXIS_VSCROLL, -dy / configuration.getScaledVerticalScrollFactor());
        coords[0].setAxisValue(MotionEvent.AXIS_HSCROLL, dx / configuration.getScaledHorizontalScrollFactor());
        long now = SystemClock.uptimeMillis();
        MotionEvent event = MotionEvent.obtain(now, now, MotionEvent.ACTION_SCROLL, 1, properties, coords, 0, 0, 1,
                1, 0, 0, InputDevice.SOURCE_MOUSE, 0);
        if (!view.dispatchGenericMotionEvent(event)) {
            view.scrollBy(dx, dy);
        }
        event.recycle();
    }

    private void type(String text) {
        if (text.isEmpty()) {
            return;
        }
        KeyCharacterMap map = KeyCharacterMap.load(KeyCharacterMap.VIRTUAL_KEYBOARD);
        KeyEvent[] events = map.getEvents(text.toCharArray());
        if (events != null) {
            for (KeyEvent event : events) {
                view.dispatchKeyEvent(event);
            }
        } else {
            // Characters no key makes: the event a keyboard sends for composed text.
            view.dispatchKeyEvent(new KeyEvent(SystemClock.uptimeMillis(), text, KeyCharacterMap.VIRTUAL_KEYBOARD, 0));
        }
    }

    private void key(int code) {
        long now = SystemClock.uptimeMillis();
        view.dispatchKeyEvent(new KeyEvent(now, now, KeyEvent.ACTION_DOWN, code, 0));
        view.dispatchKeyEvent(new KeyEvent(now, now, KeyEvent.ACTION_UP, code, 0));
    }

    private void zoom(float factor) {
        float step = factor / zoom;
        if (step > 0.01f && step < 100.0f && Math.abs(step - 1.0f) > 0.001f) {
            view.zoomBy(step);
        }
        zoom = factor;
    }

    private final class Client extends WebViewClient {
        @Override
        public boolean shouldOverrideUrlLoading(WebView web, WebResourceRequest request) {
            if (!request.isForMainFrame()) {
                return false; // frames are the site's own business
            }
            return !nativeNavigate(handle, bytes(request.getUrl().toString()), request.isRedirect() ? REDIRECT : LINK,
                    request.hasGesture());
        }

        @Override
        public void onPageStarted(WebView web, String url, Bitmap favicon) {
            if (foreign(url)) {
                return;
            }
            loading = true;
            report();
        }

        @Override
        public void onPageFinished(WebView web, String url) {
            loading = false;
            refused = null;
            report();
        }

        @Override
        public void doUpdateVisitedHistory(WebView web, String url, boolean isReload) {
            if (!foreign(url)) {
                report();
            }
        }

        @Override
        public boolean onRenderProcessGone(WebView web, RenderProcessGoneDetail detail) {
            Log.w(TAG, "the browser's page process ended");
            loading = false;
            return true; // the app goes on; the page stays blank until it is closed
        }
    }

    private final class Chrome extends WebChromeClient {
        @Override
        public void onPermissionRequest(PermissionRequest request) {
            request.deny();
        }

        @Override
        public void onGeolocationPermissionsShowPrompt(String origin, GeolocationPermissions.Callback callback) {
            callback.invoke(origin, false, false);
        }

        @Override
        public boolean onShowFileChooser(WebView web, ValueCallback<android.net.Uri[]> files,
                FileChooserParams params) {
            files.onReceiveValue(null); // the page gets no file of the user's
            return true;
        }

        @Override
        public void onReceivedTitle(WebView web, String title) {
            report();
        }
    }

    /** The pointer a pad moves: an arrow above the page that touches pass through. */
    private static final class Pointer extends View {
        private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint edge = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Path arrow = new Path();

        Pointer(Activity activity) {
            super(activity);
            fill.setColor(0xFFFFFFFF);
            fill.setStyle(Paint.Style.FILL);
            edge.setColor(0xFF000000);
            edge.setStyle(Paint.Style.STROKE);
            edge.setStrokeJoin(Paint.Join.ROUND);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            float s = getWidth();
            edge.setStrokeWidth(Math.max(2.0f, s * 0.08f));
            arrow.rewind();
            arrow.moveTo(s * 0.06f, s * 0.04f);
            arrow.lineTo(s * 0.06f, s * 0.82f);
            arrow.lineTo(s * 0.27f, s * 0.62f);
            arrow.lineTo(s * 0.42f, s * 0.94f);
            arrow.lineTo(s * 0.55f, s * 0.88f);
            arrow.lineTo(s * 0.40f, s * 0.57f);
            arrow.lineTo(s * 0.68f, s * 0.57f);
            arrow.close();
            canvas.drawPath(arrow, fill);
            canvas.drawPath(arrow, edge);
        }
    }

    static byte[] bytes(String text) {
        return text == null ? new byte[0] : text.getBytes(StandardCharsets.UTF_8);
    }

    private static String text(byte[] bytes) {
        return bytes == null ? "" : new String(bytes, StandardCharsets.UTF_8);
    }

    /** May the top-level page go to this address? Answered at once (webview_android.cpp). */
    private static native boolean nativeNavigate(long handle, byte[] url, int kind, boolean gesture);

    private static native void nativeState(long handle, byte[] url, byte[] title, boolean loading, boolean back,
            boolean forward);

    private static native void nativeDownload(long handle, byte[] url, byte[] agent, byte[] disposition, byte[] mime,
            long length);
}
