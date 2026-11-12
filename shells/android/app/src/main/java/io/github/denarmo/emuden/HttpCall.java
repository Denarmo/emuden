// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;

/** One GET for the native HTTP client: open, read the body in pieces, close. */
public final class HttpCall {
    private static final int MAX_REDIRECTS = 10;

    private HttpURLConnection connection;
    private InputStream body;
    private int status;
    private long length = -1;
    private String error = "";

    static HttpCall open(String url, String[] headers, int timeoutSeconds) {
        HttpCall call = new HttpCall();
        try {
            URL target = new URL(url);
            for (int hop = 0; ; ++hop) {
                HttpURLConnection c = (HttpURLConnection) target.openConnection();
                c.setInstanceFollowRedirects(false);
                c.setConnectTimeout(timeoutSeconds * 1000);
                c.setReadTimeout(timeoutSeconds * 1000);
                c.setRequestProperty("User-Agent", "EmuDEN");
                for (int i = 0; i + 1 < headers.length; i += 2) {
                    c.setRequestProperty(headers[i], headers[i + 1]);
                }
                int code = c.getResponseCode();
                String location = c.getHeaderField("Location");
                if (code >= 300 && code < 400 && location != null && hop < MAX_REDIRECTS) {
                    c.disconnect();
                    target = new URL(target, location);
                    continue;
                }
                call.connection = c;
                call.status = code;
                call.length = c.getContentLengthLong();
                call.body = code >= 400 ? c.getErrorStream() : c.getInputStream();
                break;
            }
        } catch (Exception e) {
            call.error = describe(e);
        }
        return call;
    }

    public int status() {
        return status;
    }

    /** The body's size, or -1 when the server did not say. */
    public long length() {
        return length;
    }

    public String error() {
        return error;
    }

    /** Bytes read into the buffer, 0 at the end, -1 on an error (see error()). */
    public int read(byte[] buffer) {
        if (body == null) {
            return 0;
        }
        try {
            int n = body.read(buffer);
            return n < 0 ? 0 : n;
        } catch (Exception e) {
            error = describe(e);
            return -1;
        }
    }

    public void close() {
        try {
            if (body != null) {
                body.close();
            }
        } catch (Exception ignored) {
        }
        if (connection != null) {
            connection.disconnect();
        }
    }

    private static String describe(Exception e) {
        String message = e.getMessage();
        return message == null || message.isEmpty() ? e.getClass().getSimpleName() : message;
    }
}
