// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;
import android.util.Log;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Copies a folder the user granted in the system picker to "<target>/<folder name>", where
 * the app reads it by path. Files already there with the same size are kept.
 */
final class FolderImport extends Thread {
    private static final String TAG = "EmuDEN";

    private final Activity activity;
    private final Uri tree;
    private final File target;
    private volatile boolean cancelled;
    private AlertDialog progress;
    private int copied;
    private int kept;
    private int failed;

    FolderImport(Activity activity, Uri tree, File target) {
        super("folder-import");
        this.activity = activity;
        this.tree = tree;
        this.target = target;
    }

    @Override
    public void run() {
        String rootId = DocumentsContract.getTreeDocumentId(tree);
        File folder = new File(target, safeName(displayName(rootId)));
        activity.runOnUiThread(() -> progress = new AlertDialog.Builder(activity)
                .setTitle(R.string.import_title)
                .setMessage(folder.getName())
                .setCancelable(false)
                .setNegativeButton(android.R.string.cancel, (d, which) -> cancelled = true)
                .show());
        try {
            copyFolder(rootId, folder);
        } catch (Exception e) {
            Log.e(TAG, "folder copy stopped", e);
            failed++;
        }
        activity.runOnUiThread(() -> {
            if (progress != null) {
                progress.dismiss();
            }
            String text = activity.getString(R.string.import_done, copied + kept, folder.getAbsolutePath());
            if (failed > 0) {
                text += " " + activity.getString(R.string.import_failed, failed);
            }
            Toast.makeText(activity, text, Toast.LENGTH_LONG).show();
            Log.i(TAG, "folder copy: " + copied + " copied, " + kept + " kept, " + failed
                    + " failed, to " + folder);
        });
    }

    private String displayName(String documentId) {
        Uri uri = DocumentsContract.buildDocumentUriUsingTree(tree, documentId);
        try (Cursor cursor = activity.getContentResolver().query(uri,
                new String[] {Document.COLUMN_DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst() && cursor.getString(0) != null) {
                return cursor.getString(0);
            }
        } catch (Exception e) {
            Log.w(TAG, "no name for " + documentId, e);
        }
        return "Games";
    }

    private static String safeName(String name) {
        String clean = name.replaceAll("[\\\\/:*?\"<>|\\x00-\\x1f]", "_").trim();
        return clean.isEmpty() || clean.equals(".") || clean.equals("..") ? "Games" : clean;
    }

    private void copyFolder(String documentId, File folder) throws IOException {
        if (!folder.isDirectory() && !folder.mkdirs()) {
            throw new IOException("cannot create " + folder);
        }
        ContentResolver resolver = activity.getContentResolver();
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, documentId);
        try (Cursor cursor = resolver.query(children, new String[] {Document.COLUMN_DOCUMENT_ID,
                Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE, Document.COLUMN_SIZE},
                null, null, null)) {
            while (cursor != null && cursor.moveToNext() && !cancelled) {
                String id = cursor.getString(0);
                File out = new File(folder, safeName(cursor.getString(1)));
                if (Document.MIME_TYPE_DIR.equals(cursor.getString(2))) {
                    copyFolder(id, out);
                } else if (out.isFile() && !cursor.isNull(3) && out.length() == cursor.getLong(3)) {
                    kept++;
                } else {
                    copyFile(resolver, DocumentsContract.buildDocumentUriUsingTree(tree, id), out);
                }
            }
        }
    }

    private void copyFile(ContentResolver resolver, Uri from, File out) {
        activity.runOnUiThread(() -> {
            if (progress != null) {
                progress.setMessage(out.getName());
            }
        });
        File part = new File(out.getPath() + ".part");
        try (InputStream in = resolver.openInputStream(from); OutputStream to = new FileOutputStream(part)) {
            if (in == null) {
                throw new IOException("cannot open " + from);
            }
            byte[] buffer = new byte[256 * 1024];
            for (int n; (n = in.read(buffer)) > 0 && !cancelled; ) {
                to.write(buffer, 0, n);
            }
        } catch (Exception e) {
            Log.w(TAG, "not copied: " + out.getName(), e);
            part.delete();
            failed++;
            return;
        }
        if (cancelled || !part.renameTo(out)) {
            part.delete();
        } else {
            copied++;
        }
    }
}
