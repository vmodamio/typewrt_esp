package com.typewrt.companion;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.List;
import java.util.Locale;

public class OutputContentProvider extends ContentProvider {
    private static final String OUTPUT_DIR_NAME = "output";

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public String getType(Uri uri) {
        try {
            return guessMimeType(fileForUri(uri).getName());
        } catch (FileNotFoundException e) {
            return "application/octet-stream";
        }
    }

    @Override
    public Cursor query(
        Uri uri,
        String[] projection,
        String selection,
        String[] selectionArgs,
        String sortOrder
    ) {
        try {
            File file = fileForUri(uri);
            String[] columns = projection == null ? new String[] {
                OpenableColumns.DISPLAY_NAME,
                OpenableColumns.SIZE
            } : projection;
            MatrixCursor cursor = new MatrixCursor(columns, 1);
            Object[] row = new Object[columns.length];

            for (int i = 0; i < columns.length; i++) {
                if (OpenableColumns.DISPLAY_NAME.equals(columns[i])) {
                    row[i] = file.getName();
                } else if (OpenableColumns.SIZE.equals(columns[i])) {
                    row[i] = file.length();
                } else {
                    row[i] = null;
                }
            }
            cursor.addRow(row);
            return cursor;
        } catch (FileNotFoundException e) {
            return null;
        }
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        if (mode != null && !"r".equals(mode)) {
            throw new FileNotFoundException("Output files are read-only.");
        }
        return ParcelFileDescriptor.open(fileForUri(uri), ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        return null;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        return 0;
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        return 0;
    }

    private File fileForUri(Uri uri) throws FileNotFoundException {
        List<String> segments = uri.getPathSegments();

        if (segments.size() < 2 || !OUTPUT_DIR_NAME.equals(segments.get(0))) {
            throw new FileNotFoundException("Output file not found.");
        }

        StringBuilder path = new StringBuilder();
        for (int i = 1; i < segments.size(); i++) {
            if (path.length() > 0) {
                path.append('/');
            }
            path.append(segments.get(i));
        }

        try {
            File root = outputDir().getCanonicalFile();
            File file = new File(root, sanitizePath(path.toString())).getCanonicalFile();
            String rootPath = root.getPath();
            String filePath = file.getPath();

            if (!filePath.equals(rootPath) && filePath.startsWith(rootPath + File.separator) &&
                    file.isFile()) {
                return file;
            }
        } catch (IOException | RuntimeException ignored) {
            // Surface a stable FileNotFoundException to callers.
        }
        throw new FileNotFoundException("Output file not found.");
    }

    private File outputDir() throws IOException {
        File root = getContext() == null ? null : getContext().getExternalFilesDir(null);

        if (root == null && getContext() != null) {
            root = getContext().getFilesDir();
        }
        if (root == null) {
            throw new IOException("App storage is not available.");
        }
        return new File(root, OUTPUT_DIR_NAME);
    }

    private String sanitizePath(String path) {
        String clean = path == null ? "" : path.trim().replace('\\', '/');
        StringBuilder out = new StringBuilder();
        String[] parts = clean.split("/");

        for (String part : parts) {
            String trimmed = part.trim();
            if (trimmed.isEmpty() || ".".equals(trimmed) || "..".equals(trimmed)) {
                continue;
            }
            if (out.length() > 0) {
                out.append('/');
            }
            out.append(trimmed);
        }
        return out.toString();
    }

    private String guessMimeType(String fileName) {
        String lower = fileName.toLowerCase(Locale.US);

        if (lower.endsWith(".md") || lower.endsWith(".markdown")) {
            return "text/markdown";
        }
        if (lower.endsWith(".csv")) {
            return "text/csv";
        }
        if (lower.endsWith(".json")) {
            return "application/json";
        }
        if (lower.endsWith(".epub")) {
            return "application/epub+zip";
        }
        if (lower.endsWith(".docx")) {
            return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
        }
        if (lower.endsWith(".html") || lower.endsWith(".htm")) {
            return "text/html";
        }
        if (lower.endsWith(".rst")) {
            return "text/x-rst";
        }
        if (lower.endsWith(".tex")) {
            return "application/x-tex";
        }
        if (lower.endsWith(".org")) {
            return "text/org";
        }
        if (lower.endsWith(".txt") || !lower.contains(".")) {
            return "text/plain";
        }
        return "application/octet-stream";
    }
}
