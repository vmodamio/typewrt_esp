# Typewrt Android Companion

Tiny Android companion for Typewrt BLE file transfers, GitHub sync, and Pandoc export.

The app keeps its own storage area with two folders. Tap **Repository** in the Transfer
tab to see the actual phone path.

- `remote/` is the phone mirror synchronized with GitHub and the typewriter.
- `output/` is for Pandoc exports and is not sent back to GitHub or Typewrt.
- `.typewrt-sync.json` tracks GitHub blob SHAs and local hashes so commits only send
  known local changes instead of comparing the whole repository.

## Use

### Receive sync files from Typewrt

1. In the Typewrt menu, mark files with `s`, then press `b` or run `:ble send`.
2. Open this app on the phone.
3. Tap **From Typewrt** and accept the Bluetooth permission prompt.
4. Received files are saved to the app's `remote/` folder, preserving subdirectories.

The app connects to the Typewrt BLE service `0xffe0`, subscribes to the TX characteristic
`0xffe1`, receives one or more `TYPEWRT-FILE` blocks, and stores the raw file bytes
locally. The Typewrt copy is treated as authoritative for received files, so matching
paths in `remote/` are updated directly.

### Send updates to Typewrt

1. Pull or restore from GitHub, or open a file in the Repository browser and queue delete.
2. From the Typewrt menu, enter `ble recv` to advertise receive mode in the current directory.
3. Tap **To Typewrt**.

The app writes the same `TYPEWRT-FILE <bytes> <name>\n` stream to the RX characteristic
`0xffe2`. Delete markers are sent as `TYPEWRT-DELETE <path>\n`; Typewrt keeps its local
copy and marks it with `x` in the menu. The Repository browser marks queued file updates
with `*` and queued deletes with `x`. Text files open read-only, with lightweight Markdown
highlighting for Markdown files.

## Pandoc export

After files are received or pulled into `remote/`, enter the root URL of a reachable
`pandoc-server`, choose the Pandoc inputs in export order, then tap **Export**. `.yaml`
and `.yml` selections are treated as Pandoc metadata blocks for the exported document.
The converted file is saved into the app's `output/` folder, separate from the
synchronized `remote/` mirror. Tap an item under **Pandoc outputs** to open it in another
Android app. The default conversion is Pandoc Markdown to EPUB.

`https://pandoc.org/app/` is a browser-based Pandoc WASM app, not an upload API. For direct
conversion from this native app, run a `pandoc-server` instance on a machine the phone can
reach, for example:

```sh
pandoc server --port 3030
```

Then use a URL such as `http://192.168.1.20:3030/` in the Android app.

## GitHub repository sync

Tap **GitHub repository** to show or hide the repository settings:

- Repository as `owner/repository`
- Repository root/path as an optional folder inside the repository, for example `notes`
- Branch, usually `main`
- A GitHub token with repository **Contents: read and write** permission

The app checks repository access whenever the configuration changes. Tap **Pull** to
download the whole repository, or the configured subfolder, into `remote/`. Files that
differ from the local phone mirror are queued for **To Typewrt**. Files that disappeared
from GitHub are removed from the phone mirror and queued as `TYPEWRT-DELETE` markers so
the typewriter can mark them with `x`.

Tap **Commit changes** to commit the files marked dirty in `.typewrt-sync.json` with the
message in the commit field. Tap **Restore** to choose a recent commit and rewrite
`remote/` to that point; the resulting file changes are queued for **To Typewrt**. Use
**File commits** to pick a file from the repository mirror and inspect its history.

## Build

Open `companion/typewrt-android` in Android Studio and run the `app` configuration on a phone.
The debug APK is generated at `app/build/outputs/apk/debug/app-debug.apk`.

The app is intentionally dependency-free apart from the Android Gradle plugin. It uses the
platform Bluetooth LE APIs and requires Android 10 or newer.
