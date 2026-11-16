// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.CancellationSignal;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract.Document;
import android.provider.DocumentsContract.Root;
import android.provider.DocumentsContract;
import android.provider.DocumentsProvider;
import android.webkit.MimeTypeMap;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

/**
 * The files and folders EmuDEN shares with an engine app for good (ECP configure): the player's
 * BIOS folder and game folders, read-only, as documents. It lists no roots, so no file picker
 * shows it; only an app a URI was granted to (EngineApps.share) can open one, and only what is
 * inside a path shared here. Document ids are canonical paths.
 */
public final class SharedDocuments extends DocumentsProvider {
    private static final String PREFS = "shared_documents";
    private static final String KEY_PATHS = "paths";
    private static final String[] ROOT_COLUMNS = {Root.COLUMN_ROOT_ID, Root.COLUMN_FLAGS, Root.COLUMN_TITLE,
            Root.COLUMN_DOCUMENT_ID};
    private static final String[] DOCUMENT_COLUMNS = {Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME,
            Document.COLUMN_MIME_TYPE, Document.COLUMN_SIZE, Document.COLUMN_LAST_MODIFIED, Document.COLUMN_FLAGS};

    static String authority(Context context) {
        return context.getPackageName() + ".documents";
    }

    /** Grants `pkg` lasting read access to a file or a folder and returns its URI. */
    static Uri share(Context context, String pkg, File file) throws IOException {
        File canonical = file.getCanonicalFile();
        if (!canonical.exists()) {
            throw new FileNotFoundException(file.getPath());
        }
        String id = canonical.getPath();
        SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        Set<String> paths = new HashSet<>(prefs.getStringSet(KEY_PATHS, new HashSet<>()));
        if (paths.add(id)) {
            prefs.edit().putStringSet(KEY_PATHS, paths).apply();
        }
        boolean folder = canonical.isDirectory();
        Uri uri = folder ? DocumentsContract.buildTreeDocumentUri(authority(context), id)
                : DocumentsContract.buildDocumentUri(authority(context), id);
        int flags = Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
                | (folder ? Intent.FLAG_GRANT_PREFIX_URI_PERMISSION : 0);
        context.grantUriPermission(pkg, uri, flags);
        return uri;
    }

    // The file of a document id, when it is a shared path or inside one.
    private File fileOf(String id) throws FileNotFoundException {
        File file;
        try {
            file = new File(id).getCanonicalFile();
        } catch (IOException e) {
            throw new FileNotFoundException(id);
        }
        Set<String> paths = getContext().getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .getStringSet(KEY_PATHS, new HashSet<>());
        for (String path : paths) {
            if (inside(file.getPath(), path)) {
                if (!file.exists()) break;
                return file;
            }
        }
        throw new FileNotFoundException(id);
    }

    private static boolean inside(String path, String folder) {
        return path.equals(folder) || path.startsWith(folder.endsWith("/") ? folder : folder + "/");
    }

    private static void addRow(MatrixCursor cursor, File file) {
        String mime = file.isDirectory() ? Document.MIME_TYPE_DIR : mimeOf(file.getName());
        cursor.newRow()
                .add(Document.COLUMN_DOCUMENT_ID, file.getPath())
                .add(Document.COLUMN_DISPLAY_NAME, file.getName())
                .add(Document.COLUMN_MIME_TYPE, mime)
                .add(Document.COLUMN_SIZE, file.isDirectory() ? null : file.length())
                .add(Document.COLUMN_LAST_MODIFIED, file.lastModified())
                .add(Document.COLUMN_FLAGS, 0);
    }

    private static String mimeOf(String name) {
        int dot = name.lastIndexOf('.');
        String type = dot < 0 ? null
                : MimeTypeMap.getSingleton().getMimeTypeFromExtension(name.substring(dot + 1).toLowerCase(Locale.ROOT));
        return type != null ? type : "application/octet-stream";
    }

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public Cursor queryRoots(String[] projection) {
        return new MatrixCursor(projection != null ? projection : ROOT_COLUMNS);
    }

    @Override
    public Cursor queryDocument(String documentId, String[] projection) throws FileNotFoundException {
        MatrixCursor cursor = new MatrixCursor(projection != null ? projection : DOCUMENT_COLUMNS);
        addRow(cursor, fileOf(documentId));
        return cursor;
    }

    @Override
    public Cursor queryChildDocuments(String parentDocumentId, String[] projection, String sortOrder)
            throws FileNotFoundException {
        MatrixCursor cursor = new MatrixCursor(projection != null ? projection : DOCUMENT_COLUMNS);
        File[] children = fileOf(parentDocumentId).listFiles();
        if (children != null) {
            for (File child : children) {
                if (!child.getName().startsWith(".")) addRow(cursor, child);
            }
        }
        return cursor;
    }

    @Override
    public boolean isChildDocument(String parentDocumentId, String documentId) {
        try {
            return inside(fileOf(documentId).getPath(), fileOf(parentDocumentId).getPath());
        } catch (FileNotFoundException e) {
            return false;
        }
    }

    @Override
    public ParcelFileDescriptor openDocument(String documentId, String mode, CancellationSignal signal)
            throws FileNotFoundException {
        if (!"r".equals(mode)) {
            throw new FileNotFoundException("read only");
        }
        File file = fileOf(documentId);
        if (!file.isFile()) {
            throw new FileNotFoundException(documentId);
        }
        return ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY);
    }
}
