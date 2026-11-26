// SPDX-License-Identifier: GPL-3.0-or-later
// ShaderImport.java - GTA 2 Recompiled for Android (agent L, 2026-11-26): BACK > Settings > Shaders.
// The player brings RetroArch slang shader packs into <userDir>/shader/ (the same folder as on Linux / Windows,
// maintainer 2026-11-26): a .zip (e.g. libretro's slang-shaders-master.zip) is copied as it is - the game unpacks it
// itself at start (winrecomp shaderpack: one root folder stripped, zip-slip guard, CRC-checked, into data/shaders/) -,
// or a whole folder is copied file by file (it is then a pack used in place). Copies, not links: the native code opens
// plain file paths, and the packs keep working when the SD card or the source is gone. The game reads shader/ when it
// starts, so an import or a removal ends with a restart prompt; the shader itself is chosen in the game
// (RECOMP OPTIONS > SHADERS).
package io.github.almostdrownedrat.gta2;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

final class ShaderImport {
    private ShaderImport() { }

    /** What the player picked: one zip, or a folder with its files (path below the folder, document, size). */
    static final class Plan {
        String name = "shaders";
        boolean zip;
        final List<String> rel = new ArrayList<>();
        final List<Uri> uris = new ArrayList<>();
        long bytes;
        int presets;                // .slangp files (a folder; a zip is only looked into by the game)
    }

    /** A picked .zip file. Null when it is not one. */
    static Plan zip(ContentResolver cr, Uri file) {
        Plan p = new Plan();
        p.zip = true;
        String name = null;
        try (Cursor c = cr.query(file, new String[] { OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE }, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                name = c.getString(0);
                p.bytes = c.isNull(1) ? 0 : c.getLong(1);
            }
        } catch (Exception ignored) { }
        if (name == null || !name.toLowerCase(Locale.ROOT).endsWith(".zip")) return null;
        p.name = MediaImport.safeName(name.substring(0, name.length() - 4));
        p.rel.add(p.name + ".zip");
        p.uris.add(file);
        return p;
    }

    /** A picked folder: every file below it (at most 16 levels, hidden ones left out). */
    static Plan folder(ContentResolver cr, Uri tree) {
        Plan p = new Plan();
        String rootId = DocumentsContract.getTreeDocumentId(tree);
        Uri root = DocumentsContract.buildDocumentUriUsingTree(tree, rootId);
        try (Cursor c = cr.query(root, new String[] { Document.COLUMN_DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst() && c.getString(0) != null) p.name = MediaImport.safeName(c.getString(0));
        } catch (Exception ignored) { }
        walk(cr, tree, rootId, p.name, 0, p);
        return p;
    }

    private static void walk(ContentResolver cr, Uri tree, String docId, String rel, int depth, Plan p) {
        Uri kids = DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId);
        String[] cols = { Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE,
                          Document.COLUMN_SIZE };
        try (Cursor c = cr.query(kids, cols, null, null, null)) {
            if (c == null) return;
            while (c.moveToNext()) {
                String id = c.getString(0), name = c.getString(1), mime = c.getString(2);
                if (name == null || name.startsWith(".")) continue;
                String r = rel + "/" + MediaImport.safeName(name);
                if (Document.MIME_TYPE_DIR.equals(mime)) {
                    if (depth < 16) walk(cr, tree, id, r, depth + 1, p);
                } else {
                    long size = c.isNull(3) ? 0 : c.getLong(3);
                    p.rel.add(r);
                    p.uris.add(DocumentsContract.buildDocumentUriUsingTree(tree, id));
                    p.bytes += size;
                    if (name.toLowerCase(Locale.ROOT).endsWith(".slangp")) p.presets++;
                }
            }
        } catch (Exception ignored) { }
    }

    /** The pack's place in shader/: <name>.zip or <name>/. */
    static File target(File shader, Plan p) { return new File(shader, p.zip ? p.name + ".zip" : p.name); }

    /** Copies the plan into shader/ through shader/.import/ (a cancelled or failed copy leaves nothing behind; a pack
     *  of the same name is replaced only at the end). "" = done, "cancel", else a sentence. */
    static String copy(ContentResolver cr, Plan p, File shader, MediaImport.Progress cb) {
        File tmp = new File(shader, ".import"), dst = target(shader, p);
        Gta2Activity.deleteTree(tmp);
        long done = 0;
        byte[] buf = new byte[1 << 20];
        try {
            for (int i = 0; i < p.rel.size(); i++) {
                File out = new File(tmp, p.rel.get(i));
                out.getParentFile().mkdirs();
                try (InputStream in = cr.openInputStream(p.uris.get(i)); OutputStream o = new FileOutputStream(out)) {
                    if (in == null) throw new IOException("cannot read " + p.rel.get(i));
                    int r;
                    while ((r = in.read(buf)) > 0) {
                        o.write(buf, 0, r);
                        done += r;
                        if (!cb.step(done, Math.max(p.bytes, done), p.rel.get(i))) { Gta2Activity.deleteTree(tmp); return "cancel"; }
                    }
                }
            }
        } catch (Exception ex) {
            Gta2Activity.deleteTree(tmp);
            return "The shaders could not be copied (" + ex.getMessage() + "). Is there enough free space?";
        }
        Gta2Activity.deleteTree(dst);
        File made = new File(tmp, p.rel.get(0).contains("/") ? p.name : p.rel.get(0));
        boolean ok = made.renameTo(dst);
        Gta2Activity.deleteTree(tmp);
        return ok ? "" : "The shaders could not be stored.";
    }

    /** A pack in shader/: a .zip or a folder, with its size. */
    static final class Pack {
        final String name; final boolean zip; final long bytes;
        Pack(String name, boolean zip, long bytes) { this.name = name; this.zip = zip; this.bytes = bytes; }
    }

    static List<Pack> packs(File shader) {
        List<Pack> l = new ArrayList<>();
        File[] kids = shader.listFiles();
        if (kids != null) for (File k : kids) {
            if (k.getName().startsWith(".")) continue;
            if (k.isDirectory()) l.add(new Pack(k.getName(), false, size(k, 0)));
            else if (k.getName().toLowerCase(Locale.ROOT).endsWith(".zip")) l.add(new Pack(k.getName(), true, k.length()));
        }
        Collections.sort(l, (a, b) -> a.name.compareToIgnoreCase(b.name));
        return l;
    }

    private static long size(File d, int depth) {
        long n = 0;
        File[] kids = d.listFiles();
        if (kids != null) for (File k : kids) n += k.isDirectory() ? (depth < 16 ? size(k, depth + 1) : 0) : k.length();
        return n;
    }

    /** Removes a pack, and for a zip its unpacked copy in data/shaders/ (a cache the game made from it). */
    static void remove(File shader, File data, Pack p) {
        Gta2Activity.deleteTree(new File(shader, p.name));
        if (p.zip) Gta2Activity.deleteTree(new File(new File(data, "shaders"), p.name.substring(0, p.name.length() - 4)));
    }
}
