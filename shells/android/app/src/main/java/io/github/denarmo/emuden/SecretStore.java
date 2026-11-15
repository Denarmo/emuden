// SPDX-FileCopyrightText: Copyright 2026 EmuDEN contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package io.github.denarmo.emuden;

import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;
import android.util.Log;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.KeyStore;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;
import org.json.JSONObject;

/** The native secrets: a JSON file of values encrypted with an AES key of the Android Keystore. */
final class SecretStore {
    private static final String TAG = "EmuDEN";
    private static final String ALIAS = "emuden.secrets";
    private static final String FILE = "secrets.json";

    private SecretStore() {}

    static synchronized byte[] get(File dir, String name) {
        try {
            JSONObject all = load(dir);
            if (!all.has(name)) {
                return null;
            }
            byte[] sealed = Base64.decode(all.getString(name), Base64.NO_WRAP);
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.DECRYPT_MODE, key(), new GCMParameterSpec(128, sealed, 0, 12));
            return cipher.doFinal(sealed, 12, sealed.length - 12);
        } catch (Exception e) {
            Log.w(TAG, "a secret could not be read", e);
            return null;
        }
    }

    static synchronized boolean set(File dir, String name, byte[] value) {
        try {
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.ENCRYPT_MODE, key());
            byte[] iv = cipher.getIV();
            byte[] body = cipher.doFinal(value);
            byte[] sealed = new byte[iv.length + body.length];
            System.arraycopy(iv, 0, sealed, 0, iv.length);
            System.arraycopy(body, 0, sealed, iv.length, body.length);
            JSONObject all = load(dir);
            all.put(name, Base64.encodeToString(sealed, Base64.NO_WRAP));
            save(dir, all);
            return true;
        } catch (Exception e) {
            Log.w(TAG, "a secret could not be stored", e);
            return false;
        }
    }

    static synchronized void remove(File dir, String name) {
        try {
            JSONObject all = load(dir);
            if (all.remove(name) != null) {
                save(dir, all);
            }
        } catch (Exception e) {
            Log.w(TAG, "a secret could not be removed", e);
        }
    }

    private static SecretKey key() throws Exception {
        KeyStore store = KeyStore.getInstance("AndroidKeyStore");
        store.load(null);
        if (store.containsAlias(ALIAS)) {
            return ((KeyStore.SecretKeyEntry) store.getEntry(ALIAS, null)).getSecretKey();
        }
        KeyGenerator generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
        generator.init(new KeyGenParameterSpec.Builder(ALIAS,
                KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .build());
        return generator.generateKey();
    }

    private static JSONObject load(File dir) throws Exception {
        File file = new File(dir, FILE);
        if (!file.isFile()) {
            return new JSONObject();
        }
        return new JSONObject(new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8));
    }

    private static void save(File dir, JSONObject all) throws Exception {
        File temp = new File(dir, FILE + ".tmp");
        Files.write(temp.toPath(), all.toString().getBytes(StandardCharsets.UTF_8));
        if (!temp.renameTo(new File(dir, FILE))) {
            throw new java.io.IOException("cannot replace " + FILE);
        }
    }
}
