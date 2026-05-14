# Typewrt Android Companion

Tiny Android companion for Typewrt BLE file transfers.

## Use

### Receive from Typewrt

1. On Typewrt, run `:ble` to send the current buffer, or `:ble path` to send a file from the SD card.
2. Open this app on the phone.
3. Tap **Receive from Typewrt** and accept the Bluetooth permission prompt.
4. The received file is saved to `Downloads/Typewrt`.

The app connects to the Typewrt BLE service `0xffe0`, subscribes to the TX characteristic
`0xffe1`, receives the `TYPEWRT-FILE` stream, and stores the raw file bytes locally.

### Send to Typewrt

1. From the Typewrt menu, enter `ble recv` to advertise receive mode in the current directory.
2. In the Android app, tap **Choose text file** and pick a local text document.
3. Tap **Send to Typewrt**.

The app writes the same `TYPEWRT-FILE <bytes> <name>\n` stream to the RX characteristic
`0xffe2`. Typewrt saves the file in the directory that was open in the menu when `ble recv`
was started.

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

## GitHub upload

After receiving or exporting a file, fill in:

- Repository as `owner/repository`
- Repository path, for example `notes/typewrt.txt`
- Branch, usually `main`
- A GitHub token with repository **Contents: write** permission

Tap **Send to GitHub**. The app checks whether the target path already exists, then creates
or updates the file through the GitHub repository contents API.

## Build

Open `companion/typewrt-android` in Android Studio and run the `app` configuration on a phone.
The debug APK is generated at `app/build/outputs/apk/debug/app-debug.apk`.

The app is intentionally dependency-free apart from the Android Gradle plugin. It uses the
platform Bluetooth LE APIs and requires Android 10 or newer.
