# Typewrt Android Companion

Tiny Android companion for Typewrt BLE file transfers.

## Use

### Receive sync files from Typewrt

1. In the Typewrt menu, mark files with `s`, then press `b` or run `:ble send`.
2. Open this app on the phone.
3. Tap **From Typewrt** and accept the Bluetooth permission prompt.
4. Received files are saved to `Downloads/Typewrt`, preserving relative subdirectories.

The app connects to the Typewrt BLE service `0xffe0`, subscribes to the TX characteristic
`0xffe1`, receives one or more `TYPEWRT-FILE` blocks, and stores the raw file bytes
locally. If a different file already exists in `Downloads/Typewrt`, the app asks before
overwriting it.

### Send updates to Typewrt

1. From the Typewrt menu, enter `ble recv` to advertise receive mode in the current directory.
2. In the Android app, tap **Add files** and pick one or more local text documents.
3. Optionally enter a relative path and tap **Add delete** to mark a file as deleted remotely.
4. Tap **To Typewrt**.

The app writes the same `TYPEWRT-FILE <bytes> <name>\n` stream to the RX characteristic
`0xffe2`. Delete markers are sent as `TYPEWRT-DELETE <path>\n`; Typewrt keeps its local
copy and marks it with `x` in the menu.

## Pandoc export

After a file is received, enter the root URL of a reachable `pandoc-server`, choose input
and output formats, then tap **Export**. The converted file is saved back into
`Downloads/Typewrt` and becomes the new latest file.

`https://pandoc.org/app/` is a browser-based Pandoc WASM app, not an upload API. For direct
conversion from this native app, run a `pandoc-server` instance on a machine the phone can
reach, for example:

```sh
pandoc server --port 3030
```

Then use a URL such as `http://192.168.1.20:3030/` in the Android app.

## GitHub repository sync

Fill in:

- Repository as `owner/repository`
- Repository root/path as an optional folder inside the repository, for example `notes`
- Branch, usually `main`
- A GitHub token with repository **Contents: read and write** permission

The app checks repository access whenever the configuration changes. Tap **Fetch from
GitHub** to download the whole repository, or the configured subfolder, into
`Downloads/Typewrt`. Files that differ from the local phone mirror are queued for
**To Typewrt**. Files that disappeared from GitHub are removed from the phone mirror and
queued as `TYPEWRT-DELETE` markers so the typewriter can mark them with `x`.

After receiving or exporting a file, tap **Send latest file** to create or update that one
file under the configured repository root through the GitHub repository contents API.

## Build

Open `companion/typewrt-android` in Android Studio and run the `app` configuration on a phone.
The debug APK is generated at `app/build/outputs/apk/debug/app-debug.apk`.

The app is intentionally dependency-free apart from the Android Gradle plugin. It uses the
platform Bluetooth LE APIs and requires Android 10 or newer.
