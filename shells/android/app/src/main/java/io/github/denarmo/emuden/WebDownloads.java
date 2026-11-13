// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.content.Context;
import android.net.Uri;
import android.util.Base64;
import android.util.Log;
import android.webkit.CookieManager;
import android.webkit.JavascriptInterface;
import android.webkit.WebView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.HashMap;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * What a browser's download manager does for the page of WebPage. android.webkit.WebView
 * only says that the user started a download; saving it is the app's part.
 *
 * - A file on a server: once the native side has taken it, the address is requested again
 *   here and the answer written to the file it named. That one request carries the user
 *   agent of the view and the cookies the view holds for the address, as the view's own
 *   request did (a signed or signed-in download needs them). They go into the request's
 *   headers and nowhere else: not to the native side, a log or a file.
 * - A file the page made itself (a blob: or data: address): a data: address is decoded here.
 *   A blob lives inside the page, so a short script asks the page for the bytes of exactly
 *   that address and hands them over in pieces. It reads nothing else of the page, and the
 *   bridge it talks to only takes pieces for a download that is under way.
 */
final class WebDownloads {
    private static final String TAG = "EmuDEN";
    private static final String BRIDGE = "EmuDenDownload";
    private static final long MADE_LIMIT = 256L * 1024 * 1024;
    private static final int HOPS = 6;

    private final long handle;
    private final WebView view;
    private final File made; // where files the page made wait for the native side's verdict
    private final HashMap<Integer, AtomicBoolean> fetches = new HashMap<>(); // UI thread only
    private final HashMap<String, Made> pieces = new HashMap<>();            // guarded by itself
    private final SecureRandom random = new SecureRandom();
    private volatile boolean closed;

    private static final class Made {
        File file;
        OutputStream out;
        long size;
    }

    WebDownloads(long handle, Context context, WebView view) {
        this.handle = handle;
        this.view = view;
        made = new File(context.getCacheDir(), "web-downloads");
        view.addJavascriptInterface(new Bridge(), BRIDGE);
    }

    /** On the UI thread. */
    void fetch(int id, String url, String agent, String target) {
        AtomicBoolean cancelled = new AtomicBoolean();
        fetches.put(id, cancelled);
        // Read here, on the thread the view lives on; used for this request only.
        String cookies = CookieManager.getInstance().getCookie(url);
        Thread thread = new Thread(() -> transfer(id, url, agent, cookies, new File(target), cancelled),
                "web-download");
        thread.setDaemon(true);
        thread.start();
    }

    void cancel(int id) {
        AtomicBoolean cancelled = fetches.remove(id);
        if (cancelled != null) {
            cancelled.set(true);
        }
    }

    void close() {
        closed = true;
        for (AtomicBoolean cancelled : fetches.values()) {
            cancelled.set(true);
        }
        fetches.clear();
        synchronized (pieces) {
            for (Made item : pieces.values()) {
                drop(item);
            }
            pieces.clear();
        }
    }

    private void transfer(int id, String address, String agent, String cookies, File target,
            AtomicBoolean cancelled) {
        File part = new File(target.getPath() + ".part");
        HttpURLConnection connection = null;
        try {
            URL url = new URL(address);
            for (int hop = 0; ; ++hop) {
                connection = (HttpURLConnection) url.openConnection();
                connection.setInstanceFollowRedirects(false);
                connection.setConnectTimeout(20000);
                connection.setReadTimeout(30000);
                connection.setUseCaches(false);
                if (!agent.isEmpty()) {
                    connection.setRequestProperty("User-Agent", agent);
                }
                if (cookies != null && !cookies.isEmpty()) {
                    connection.setRequestProperty("Cookie", cookies);
                }
                int status = connection.getResponseCode();
                if (status < 300 || status >= 400 || status == 304) {
                    if (status != 200) {
                        fail(id, status == 401 || status == 403 ? "the site refused the download"
                                : "the site answered " + status);
                        return;
                    }
                    break;
                }
                String location = connection.getHeaderField("Location");
                connection.disconnect();
                connection = null;
                if (location == null || hop + 1 >= HOPS) {
                    fail(id, "the site sent the download in circles");
                    return;
                }
                URL next = new URL(url, location);
                if (!"https".equals(next.getProtocol()) && !"http".equals(next.getProtocol())) {
                    fail(id, "the download leads somewhere that is not a web address");
                    return;
                }
                // Another host gets its own cookies, as in the view; none are carried across.
                if (!next.getHost().equalsIgnoreCase(url.getHost())) {
                    cookies = CookieManager.getInstance().getCookie(next.toString());
                }
                url = next;
            }
            long total = connection.getContentLengthLong();
            long received = 0, told = 0;
            try (InputStream in = connection.getInputStream(); OutputStream out = new FileOutputStream(part)) {
                byte[] buffer = new byte[64 * 1024];
                for (int n; (n = in.read(buffer)) > 0; ) {
                    if (cancelled.get()) {
                        break;
                    }
                    out.write(buffer, 0, n);
                    received += n;
                    long now = System.currentTimeMillis();
                    if (now - told >= 100) {
                        told = now;
                        nativeFetch(handle, id, 0, received, total, null);
                    }
                }
            }
            if (cancelled.get()) {
                part.delete();
                fail(id, "cancelled");
                return;
            }
            if (total > 0 && received != total) {
                part.delete();
                fail(id, "the connection was lost");
                return;
            }
            if (!part.renameTo(target)) {
                part.delete();
                fail(id, "the file could not be written");
                return;
            }
            nativeFetch(handle, id, 0, received, received, null);
            nativeFetch(handle, id, 1, received, received, WebPage.bytes(target.getPath()));
        } catch (IOException | RuntimeException e) {
            part.delete();
            // The exception's text may hold the address: only its kind is noted.
            Log.w(TAG, "a download failed (" + e.getClass().getSimpleName() + ")");
            fail(id, "the connection was lost");
        } finally {
            if (connection != null) {
                connection.disconnect();
            }
        }
    }

    private void fail(int id, String reason) {
        if (!closed) {
            nativeFetch(handle, id, 2, 0, 0, WebPage.bytes(reason));
        }
    }

    /** On the UI thread: the user asked to save a blob: or data: address. */
    void made(String url) {
        byte[] token = new byte[16];
        random.nextBytes(token);
        StringBuilder name = new StringBuilder();
        for (byte b : token) {
            name.append(String.format("%02x", b & 0xff));
        }
        Made item = new Made();
        try {
            if (!made.isDirectory() && !made.mkdirs()) {
                throw new IOException("no folder");
            }
            item.file = new File(made, name.toString());
            item.out = new FileOutputStream(item.file);
            if (url.startsWith("data:")) {
                String type = decodeData(url, item);
                item.out.close();
                nativeBlob(handle, WebPage.bytes(item.file.getPath()), WebPage.bytes(type), item.size);
                return;
            }
        } catch (IOException | RuntimeException e) {
            drop(item);
            Log.w(TAG, "a file the page made could not be saved");
            return;
        }
        synchronized (pieces) {
            pieces.put(name.toString(), item);
        }
        // Only the blob the user asked to save is asked for, by its own address.
        String script = "(async()=>{const k='" + name + "',b=window." + BRIDGE + ";try{"
                + "const f=await(await fetch(" + quote(url) + ")).blob();"
                + "for(let o=0;o<f.size;o+=393216){"
                + "const u=new Uint8Array(await f.slice(o,o+393216).arrayBuffer());let s='';"
                + "for(let i=0;i<u.length;i+=32768)s+=String.fromCharCode.apply(null,u.subarray(i,i+32768));"
                + "b.piece(k,btoa(s));}"
                + "b.done(k,f.type||'');}catch(e){b.failed(k);}})()";
        view.evaluateJavascript(script, null);
    }

    private static String quote(String text) {
        StringBuilder out = new StringBuilder("'");
        for (int i = 0; i < text.length(); ++i) {
            char c = text.charAt(i);
            if (c == '\'' || c == '\\' || c < 0x20 || c == '<' || c == 0x2028 || c == 0x2029) {
                out.append(String.format("\\u%04x", (int) c));
            } else {
                out.append(c);
            }
        }
        return out.append('\'').toString();
    }

    // data:[<type>][;base64],<data>
    private static String decodeData(String url, Made item) throws IOException {
        int comma = url.indexOf(',');
        if (comma < 0) {
            throw new IOException("not a data address");
        }
        String head = url.substring(5, comma);
        String body = url.substring(comma + 1);
        byte[] data = head.endsWith(";base64") ? Base64.decode(Uri.decode(body), Base64.DEFAULT)
                : Uri.decode(body).getBytes(StandardCharsets.ISO_8859_1);
        item.out.write(data);
        item.size = data.length;
        int end = head.indexOf(';');
        return end < 0 ? head : head.substring(0, end);
    }

    private static void drop(Made item) {
        try {
            if (item.out != null) {
                item.out.close();
            }
        } catch (IOException e) {
            // Nothing to keep.
        }
        if (item.file != null) {
            item.file.delete();
        }
    }

    /**
     * What the script above talks to, on a thread of the engine. A page can call it too, so a
     * call counts only with the key of a download that is under way, which only that script
     * was given, and the result still goes through the native side's verdict like any file.
     */
    private final class Bridge {
        @JavascriptInterface
        public void piece(String key, String base64) {
            synchronized (pieces) {
                Made item = pieces.get(key);
                if (item == null || base64 == null) {
                    return;
                }
                try {
                    byte[] data = Base64.decode(base64, Base64.DEFAULT);
                    if (item.size + data.length > MADE_LIMIT) {
                        throw new IOException("too large");
                    }
                    item.out.write(data);
                    item.size += data.length;
                } catch (IOException | RuntimeException e) {
                    pieces.remove(key);
                    drop(item);
                }
            }
        }

        @JavascriptInterface
        public void done(String key, String type) {
            Made item;
            synchronized (pieces) {
                item = pieces.remove(key);
            }
            if (item == null) {
                return;
            }
            try {
                item.out.close();
            } catch (IOException e) {
                drop(item);
                return;
            }
            if (closed) {
                item.file.delete();
                return;
            }
            nativeBlob(handle, WebPage.bytes(item.file.getPath()), WebPage.bytes(type), item.size);
        }

        @JavascriptInterface
        public void failed(String key) {
            synchronized (pieces) {
                Made item = pieces.remove(key);
                if (item != null) {
                    drop(item);
                    Log.w(TAG, "the page did not hand over the file it made");
                }
            }
        }
    }

    private static native void nativeBlob(long handle, byte[] file, byte[] mime, long length);

    private static native void nativeFetch(long handle, int id, int kind, long received, long total, byte[] text);
}
