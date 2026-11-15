// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.reflect.Field;
import java.net.HttpURLConnection;
import java.net.ProtocolException;
import java.net.URL;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.SSLContext;
import javax.net.ssl.TrustManager;
import javax.net.ssl.X509TrustManager;

/** One request for the native HTTP client: open (send the body), read the answer in pieces, close. */
public final class HttpCall {
    private static final int MAX_REDIRECTS = 10;

    private HttpURLConnection connection;
    private InputStream body;
    private int status;
    private long length = -1;
    private String error = "";
    private String[] headers = new String[0];

    static HttpCall open(String url, String method, String[] headers, int timeoutSeconds, byte[] data,
                         String dataFile, long fileOffset, long fileLength, boolean follow,
                         boolean anyCertificate) {
        HttpCall call = new HttpCall();
        try {
            URL target = new URL(url);
            for (int hop = 0; ; ++hop) {
                HttpURLConnection c = (HttpURLConnection) target.openConnection();
                if (anyCertificate && c instanceof HttpsURLConnection && isLoopback(target)) {
                    trustAnything((HttpsURLConnection) c);
                }
                c.setInstanceFollowRedirects(false);
                c.setConnectTimeout(timeoutSeconds * 1000);
                c.setReadTimeout(timeoutSeconds * 1000);
                setMethod(c, method);
                c.setRequestProperty("User-Agent", "EmuDEN");
                for (int i = 0; i + 1 < headers.length; i += 2) {
                    c.setRequestProperty(headers[i], headers[i + 1]);
                }
                boolean sends = data != null || dataFile != null
                        || method.equals("POST") || method.equals("PUT") || method.equals("PATCH");
                if (sends) {
                    long size = data != null ? data.length : dataFile != null ? fileLength : 0;
                    c.setDoOutput(true);
                    c.setFixedLengthStreamingMode(size);
                    try (OutputStream out = c.getOutputStream()) {
                        if (data != null) {
                            out.write(data);
                        } else if (dataFile != null) {
                            try (FileInputStream in = new FileInputStream(dataFile)) {
                                in.getChannel().position(fileOffset);
                                byte[] buffer = new byte[64 * 1024];
                                long left = fileLength;
                                int n;
                                while (left > 0
                                        && (n = in.read(buffer, 0, (int) Math.min(buffer.length, left))) > 0) {
                                    out.write(buffer, 0, n);
                                    left -= n;
                                }
                            }
                        }
                    }
                }
                int code = c.getResponseCode();
                String location = c.getHeaderField("Location");
                if (follow && code >= 300 && code < 400 && location != null && hop < MAX_REDIRECTS) {
                    c.disconnect();
                    target = new URL(target, location);
                    // 303, and 301/302 after a POST, go on as a GET without the body.
                    if (code == 303 || ((code == 301 || code == 302) && method.equals("POST"))) {
                        method = "GET";
                        data = null;
                        dataFile = null;
                    }
                    continue;
                }
                call.connection = c;
                call.status = code;
                call.length = c.getContentLengthLong();
                call.headers = headersOf(c);
                call.body = code >= 400 ? c.getErrorStream() : c.getInputStream();
                break;
            }
        } catch (Exception e) {
            call.error = describe(e);
        }
        return call;
    }

    // HttpURLConnection refuses PATCH on some versions; the field it checks is set directly then.
    private static void setMethod(HttpURLConnection c, String method) throws Exception {
        try {
            c.setRequestMethod(method);
        } catch (ProtocolException e) {
            Field field = HttpURLConnection.class.getDeclaredField("method");
            field.setAccessible(true);
            field.set(c, method);
        }
    }

    private static boolean isLoopback(URL url) {
        String host = url.getHost();
        return host.equals("127.0.0.1") || host.equalsIgnoreCase("localhost");
    }

    private static void trustAnything(HttpsURLConnection c) throws Exception {
        TrustManager[] any = {new X509TrustManager() {
            public void checkClientTrusted(X509Certificate[] chain, String type) {}
            public void checkServerTrusted(X509Certificate[] chain, String type) {}
            public X509Certificate[] getAcceptedIssuers() {
                return new X509Certificate[0];
            }
        }};
        SSLContext context = SSLContext.getInstance("TLS");
        context.init(null, any, null);
        c.setSSLSocketFactory(context.getSocketFactory());
        c.setHostnameVerifier((name, session) -> isLoopbackName(name));
    }

    private static boolean isLoopbackName(String name) {
        return name.equals("127.0.0.1") || name.equalsIgnoreCase("localhost");
    }

    private static String[] headersOf(HttpURLConnection c) {
        List<String> out = new ArrayList<>();
        for (Map.Entry<String, List<String>> entry : c.getHeaderFields().entrySet()) {
            if (entry.getKey() == null) {
                continue; // the status line
            }
            for (String value : entry.getValue()) {
                out.add(entry.getKey().toLowerCase(Locale.ROOT));
                out.add(value);
            }
        }
        return out.toArray(new String[0]);
    }

    /** Name and value pairs of the answer's headers, names in lower case. */
    public String[] headers() {
        return headers;
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
