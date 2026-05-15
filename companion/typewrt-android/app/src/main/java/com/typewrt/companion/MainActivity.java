package com.typewrt.companion;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.BluetoothStatusCodes;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanRecord;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.database.Cursor;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.graphics.drawable.StateListDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;
import android.provider.MediaStore;
import android.provider.OpenableColumns;
import android.text.InputType;
import android.text.method.ScrollingMovementMethod;
import android.view.Gravity;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import android.util.Base64;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.UUID;

import org.json.JSONException;
import org.json.JSONObject;

public class MainActivity extends Activity {
    private static final int REQUEST_BLE_PERMISSIONS = 7;
    private static final int REQUEST_PICK_TEXT_FILE = 8;
    private static final long SCAN_TIMEOUT_MS = 15000;
    private static final long MAX_FILE_BYTES = 20L * 1024L * 1024L;
    private static final int HTTP_TIMEOUT_MS = 30000;
    private static final int DEFAULT_BLE_WRITE_CHUNK = 20;
    private static final int REQUESTED_BLE_MTU = 247;
    private static final int MAX_BLE_WRITE_CHUNK = REQUESTED_BLE_MTU - 3;
    private static final long SPLASH_DURATION_MS = 950;
    private static final int COLOR_BACKGROUND = 0xFF212121;
    private static final int COLOR_SURFACE = 0xFF2B2B2B;
    private static final int COLOR_SURFACE_HIGH = 0xFF33383D;
    private static final int COLOR_FIELD = 0xFF262A2E;
    private static final int COLOR_STROKE = 0xFF454A50;
    private static final int COLOR_TEXT_PRIMARY = 0xFFFFFFFF;
    private static final int COLOR_TEXT_SECONDARY = 0xFFC8D0D6;
    private static final int COLOR_TEXT_MUTED = 0xFF8E989F;
    private static final int COLOR_BLUE = 0xFF64B5F6;
    private static final int COLOR_BLUE_DARK = 0xFF1976D2;
    private static final int COLOR_RIPPLE = 0x3342A5F5;
    private static final String GITHUB_API_VERSION = "2026-03-10";

    private static final UUID SERVICE_UUID =
        UUID.fromString("0000ffe0-0000-1000-8000-00805f9b34fb");
    private static final UUID TX_UUID =
        UUID.fromString("0000ffe1-0000-1000-8000-00805f9b34fb");
    private static final UUID RX_UUID =
        UUID.fromString("0000ffe2-0000-1000-8000-00805f9b34fb");
    private static final UUID CCCD_UUID =
        UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
    private static final ParcelUuid SERVICE_PARCEL_UUID = new ParcelUuid(SERVICE_UUID);

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final Object receiveLock = new Object();
    private final Object latestFileLock = new Object();

    private TextView statusView;
    private TextView detailsView;
    private TextView activeFileView;
    private TextView preparedFileView;
    private TextView logView;
    private Button connectButton;
    private Button disconnectButton;
    private Button chooseTextButton;
    private Button sendTypewrtButton;
    private Button pandocExportButton;
    private Button githubUploadButton;
    private EditText pandocServerField;
    private EditText githubRepoField;
    private EditText githubPathField;
    private EditText githubBranchField;
    private EditText githubTokenField;
    private Spinner pandocFromSpinner;
    private Spinner pandocToSpinner;

    private BluetoothAdapter bluetoothAdapter;
    private BluetoothLeScanner scanner;
    private BluetoothGatt activeGatt;
    private BluetoothGattCharacteristic txCharacteristic;
    private BluetoothGattCharacteristic rxCharacteristic;
    private TransferMode transferMode = TransferMode.RECEIVE_FROM_TYPEWRT;
    private boolean scanning;
    private boolean receiverComplete;
    private boolean senderComplete = true;
    private int bleWriteChunkSize = DEFAULT_BLE_WRITE_CHUNK;
    private int scanGeneration;

    private ByteArrayOutputStream headerBuffer = new ByteArrayOutputStream();
    private ByteArrayOutputStream fileBuffer = new ByteArrayOutputStream();
    private long expectedSize = -1;
    private long receivedSize;
    private String currentFileName = "typewrt.txt";
    private byte[] latestFileData;
    private String latestFileName;
    private Uri latestFileUri;
    private FileSnapshot pendingSendFile;
    private byte[][] outgoingParts;
    private int outgoingPartIndex;
    private int outgoingPartOffset;
    private int outgoingPendingLength;
    private int outgoingBytesSent;

    private enum TransferMode {
        RECEIVE_FROM_TYPEWRT,
        SEND_TO_TYPEWRT
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        showSplashScreen();
        mainHandler.postDelayed(() -> {
            buildUi();
            initializeBluetooth();
        }, SPLASH_DURATION_MS);
    }

    @Override
    protected void onDestroy() {
        disconnect();
        super.onDestroy();
    }

    private void showSplashScreen() {
        LinearLayout splash = new LinearLayout(this);
        splash.setOrientation(LinearLayout.VERTICAL);
        splash.setGravity(Gravity.CENTER);
        splash.setBackgroundColor(COLOR_BACKGROUND);
        splash.setPadding(dp(24), dp(24), dp(24), dp(24));

        TextView mark = new TextView(this);
        mark.setText("wrt");
        mark.setGravity(Gravity.CENTER);
        mark.setTextColor(COLOR_TEXT_PRIMARY);
        mark.setTextSize(86);
        mark.setIncludeFontPadding(false);
        mark.setTypeface(getResources().getFont(R.font.momo_trust_display_regular));
        splash.addView(mark, new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.WRAP_CONTENT,
            LinearLayout.LayoutParams.WRAP_CONTENT));

        ImageView subtitle = new ImageView(this);
        subtitle.setImageResource(R.drawable.splash_companion_mark_ii);
        subtitle.setAdjustViewBounds(true);
        subtitle.setScaleType(ImageView.ScaleType.FIT_CENTER);
        LinearLayout.LayoutParams subtitleParams = new LinearLayout.LayoutParams(dp(220), dp(26));
        subtitleParams.topMargin = dp(16);
        splash.addView(subtitle, subtitleParams);

        setContentView(splash);
    }

    private void buildUi() {
        int pad = dp(20);
        int smallPad = dp(12);

        ScrollView page = new ScrollView(this);
        page.setFillViewport(true);
        page.setBackgroundColor(COLOR_BACKGROUND);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(pad, pad, pad, pad);
        root.setBackgroundColor(COLOR_BACKGROUND);
        page.addView(root, new ScrollView.LayoutParams(
            ScrollView.LayoutParams.MATCH_PARENT,
            ScrollView.LayoutParams.WRAP_CONTENT));

        TextView title = new TextView(this);
        title.setText("Typewrt Companion");
        title.setTextSize(26);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setTextColor(COLOR_TEXT_PRIMARY);
        root.addView(title, new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT));

        statusView = new TextView(this);
        statusView.setText("Ready");
        statusView.setTextSize(18);
        statusView.setTextColor(COLOR_TEXT_PRIMARY);
        LinearLayout.LayoutParams statusParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        statusParams.topMargin = smallPad;
        root.addView(statusView, statusParams);

        detailsView = new TextView(this);
        detailsView.setText("Receive from Typewrt, or choose a text file and send it from the menu.");
        detailsView.setTextSize(15);
        detailsView.setTextColor(COLOR_TEXT_SECONDARY);
        LinearLayout.LayoutParams detailsParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        detailsParams.topMargin = dp(6);
        root.addView(detailsView, detailsParams);

        LinearLayout buttons = new LinearLayout(this);
        buttons.setOrientation(LinearLayout.HORIZONTAL);
        buttons.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout.LayoutParams buttonsParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        buttonsParams.topMargin = dp(18);
        root.addView(buttons, buttonsParams);

        connectButton = new Button(this);
        connectButton.setText("Receive from Typewrt");
        connectButton.setAllCaps(false);
        styleButton(connectButton, true);
        connectButton.setOnClickListener(v -> startReceiver());
        buttons.addView(connectButton, new LinearLayout.LayoutParams(
            0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        disconnectButton = new Button(this);
        disconnectButton.setText("Disconnect");
        disconnectButton.setAllCaps(false);
        styleButton(disconnectButton, false);
        disconnectButton.setEnabled(false);
        disconnectButton.setOnClickListener(v -> disconnect());
        LinearLayout.LayoutParams disconnectParams = new LinearLayout.LayoutParams(
            0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
        disconnectParams.leftMargin = dp(10);
        buttons.addView(disconnectButton, disconnectParams);

        activeFileView = new TextView(this);
        activeFileView.setText("No file received yet.");
        activeFileView.setTextSize(15);
        activeFileView.setTextColor(COLOR_TEXT_SECONDARY);
        LinearLayout.LayoutParams activeParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        activeParams.topMargin = dp(12);
        root.addView(activeFileView, activeParams);

        TextView sendLabel = sectionLabel("Send to Typewrt");
        root.addView(sendLabel);

        preparedFileView = new TextView(this);
        preparedFileView.setText("No text file selected.");
        preparedFileView.setTextSize(15);
        preparedFileView.setTextColor(COLOR_TEXT_SECONDARY);
        LinearLayout.LayoutParams preparedParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        preparedParams.topMargin = dp(6);
        root.addView(preparedFileView, preparedParams);

        LinearLayout sendButtons = new LinearLayout(this);
        sendButtons.setOrientation(LinearLayout.HORIZONTAL);
        sendButtons.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout.LayoutParams sendButtonsParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        sendButtonsParams.topMargin = dp(8);
        root.addView(sendButtons, sendButtonsParams);

        chooseTextButton = new Button(this);
        chooseTextButton.setText("Choose text file");
        chooseTextButton.setAllCaps(false);
        styleButton(chooseTextButton, false);
        chooseTextButton.setOnClickListener(v -> chooseTextFile());
        sendButtons.addView(chooseTextButton, new LinearLayout.LayoutParams(
            0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        sendTypewrtButton = new Button(this);
        sendTypewrtButton.setText("Send to Typewrt");
        sendTypewrtButton.setAllCaps(false);
        styleButton(sendTypewrtButton, true);
        sendTypewrtButton.setEnabled(false);
        sendTypewrtButton.setOnClickListener(v -> startSender());
        LinearLayout.LayoutParams sendTypewrtParams = new LinearLayout.LayoutParams(
            0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
        sendTypewrtParams.leftMargin = dp(10);
        sendButtons.addView(sendTypewrtButton, sendTypewrtParams);

        TextView pandocLabel = sectionLabel("Pandoc export");
        root.addView(pandocLabel);

        pandocServerField = addTextField(
            root,
            "Pandoc server URL (http://host:3030/)",
            "");
        pandocServerField.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);

        LinearLayout pandocFormats = new LinearLayout(this);
        pandocFormats.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams formatParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        formatParams.topMargin = dp(8);
        root.addView(pandocFormats, formatParams);

        pandocFromSpinner = addSpinner(
            pandocFormats,
            new String[] {"markdown", "plain", "gfm", "html", "rst", "org", "latex"});
        pandocToSpinner = addSpinner(
            pandocFormats,
            new String[] {"html", "docx", "epub", "plain", "gfm", "markdown", "rst", "latex"});

        pandocExportButton = new Button(this);
        pandocExportButton.setText("Export");
        pandocExportButton.setAllCaps(false);
        styleButton(pandocExportButton, true);
        pandocExportButton.setEnabled(false);
        pandocExportButton.setOnClickListener(v -> exportLatestWithPandoc());
        LinearLayout.LayoutParams exportParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        exportParams.topMargin = dp(8);
        root.addView(pandocExportButton, exportParams);

        TextView githubLabel = sectionLabel("GitHub upload");
        root.addView(githubLabel);

        githubRepoField = addTextField(root, "Repository owner/name", "");
        githubPathField = addTextField(root, "Repository path", "");
        githubBranchField = addTextField(root, "Branch", "main");
        githubTokenField = addTextField(root, "GitHub token", "");
        githubTokenField.setInputType(
            InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);

        githubUploadButton = new Button(this);
        githubUploadButton.setText("Send to GitHub");
        githubUploadButton.setAllCaps(false);
        styleButton(githubUploadButton, true);
        githubUploadButton.setEnabled(false);
        githubUploadButton.setOnClickListener(v -> uploadLatestToGithub());
        LinearLayout.LayoutParams uploadParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        uploadParams.topMargin = dp(8);
        root.addView(githubUploadButton, uploadParams);

        TextView logLabel = new TextView(this);
        logLabel.setText("Transfer log");
        logLabel.setTextSize(14);
        logLabel.setTypeface(Typeface.DEFAULT_BOLD);
        logLabel.setTextColor(COLOR_BLUE);
        LinearLayout.LayoutParams labelParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        labelParams.topMargin = dp(24);
        root.addView(logLabel, labelParams);

        logView = new TextView(this);
        logView.setTextColor(COLOR_TEXT_PRIMARY);
        logView.setTextSize(14);
        logView.setTypeface(Typeface.MONOSPACE);
        logView.setMovementMethod(new ScrollingMovementMethod());
        logView.setText("");

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackground(roundedDrawable(COLOR_SURFACE, dp(10), COLOR_STROKE, 1));
        scroll.setPadding(smallPad, smallPad, smallPad, smallPad);
        scroll.addView(logView);
        LinearLayout.LayoutParams scrollParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, dp(180));
        scrollParams.topMargin = dp(8);
        root.addView(scroll, scrollParams);

        setContentView(page);
        updateSendFileActions();
    }

    private TextView sectionLabel(String text) {
        TextView label = new TextView(this);
        label.setText(text);
        label.setTextSize(14);
        label.setTypeface(Typeface.DEFAULT_BOLD);
        label.setTextColor(COLOR_BLUE);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        params.topMargin = dp(22);
        label.setLayoutParams(params);
        return label;
    }

    private EditText addTextField(LinearLayout root, String hint, String initial) {
        EditText field = new EditText(this);
        field.setHint(hint);
        field.setSingleLine(true);
        field.setTextSize(14);
        field.setTextColor(COLOR_TEXT_PRIMARY);
        field.setHintTextColor(COLOR_TEXT_MUTED);
        field.setText(initial);
        field.setPadding(dp(14), 0, dp(14), 0);
        field.setMinHeight(dp(48));
        field.setBackground(roundedDrawable(COLOR_FIELD, dp(8), COLOR_STROKE, 1));
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        params.topMargin = dp(8);
        root.addView(field, params);
        return field;
    }

    private Spinner addSpinner(LinearLayout root, String[] values) {
        Spinner spinner = new Spinner(this);
        ArrayAdapter<String> adapter = new ArrayAdapter<String>(
            this,
            android.R.layout.simple_spinner_item,
            values) {
            @Override
            public View getView(int position, View convertView, android.view.ViewGroup parent) {
                TextView view = (TextView) super.getView(position, convertView, parent);
                styleSpinnerText(view, false);
                return view;
            }

            @Override
            public View getDropDownView(int position, View convertView, android.view.ViewGroup parent) {
                TextView view = (TextView) super.getDropDownView(position, convertView, parent);
                styleSpinnerText(view, true);
                return view;
            }
        };
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinner.setAdapter(adapter);
        spinner.setMinimumHeight(dp(48));
        spinner.setPadding(dp(10), 0, dp(10), 0);
        spinner.setBackground(roundedDrawable(COLOR_FIELD, dp(8), COLOR_STROKE, 1));
        spinner.setPopupBackgroundDrawable(roundedDrawable(COLOR_SURFACE_HIGH, dp(8), COLOR_STROKE, 1));
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
            0,
            LinearLayout.LayoutParams.WRAP_CONTENT,
            1);
        params.leftMargin = root.getChildCount() == 0 ? 0 : dp(8);
        root.addView(spinner, params);
        return spinner;
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }

    private void styleButton(Button button, boolean primary) {
        int normalColor = primary ? COLOR_BLUE : COLOR_SURFACE_HIGH;
        int disabledColor = primary ? 0xFF3A4D5E : 0xFF2A2D30;
        int textColor = primary ? 0xFF0B1720 : COLOR_TEXT_PRIMARY;
        button.setMinHeight(dp(48));
        button.setPadding(dp(14), 0, dp(14), 0);
        button.setTextColor(new ColorStateList(
            new int[][] {
                new int[] {-android.R.attr.state_enabled},
                new int[] {}
            },
            new int[] {COLOR_TEXT_MUTED, textColor}));
        button.setBackground(rippleBackground(normalColor, disabledColor, dp(8)));
    }

    private void styleSpinnerText(TextView view, boolean dropdown) {
        view.setTextColor(COLOR_TEXT_PRIMARY);
        view.setTextSize(14);
        view.setPadding(dp(10), dp(10), dp(10), dp(10));
        if (dropdown) {
            view.setBackgroundColor(COLOR_SURFACE_HIGH);
        }
    }

    private Drawable rippleBackground(int normalColor, int disabledColor, int radius) {
        StateListDrawable states = new StateListDrawable();
        states.addState(
            new int[] {-android.R.attr.state_enabled},
            roundedDrawable(disabledColor, radius, COLOR_STROKE, 1));
        states.addState(new int[] {}, roundedDrawable(normalColor, radius, 0, 0));
        return new RippleDrawable(ColorStateList.valueOf(COLOR_RIPPLE), states, null);
    }

    private Drawable roundedDrawable(int color, int radius, int strokeColor, int strokeWidthDp) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(color);
        drawable.setCornerRadius(radius);
        if (strokeWidthDp > 0) {
            drawable.setStroke(dp(strokeWidthDp), strokeColor);
        }
        return drawable;
    }

    private void initializeBluetooth() {
        BluetoothManager manager = getSystemService(BluetoothManager.class);
        bluetoothAdapter = manager == null ? null : manager.getAdapter();
        if (bluetoothAdapter == null) {
            setStatus("Bluetooth is not available", "This phone does not expose a BLE adapter.");
            connectButton.setEnabled(false);
            updateSendFileActions();
        }
    }

    private void startReceiver() {
        transferMode = TransferMode.RECEIVE_FROM_TYPEWRT;
        if (!hasRequiredPermissions()) {
            requestPermissions(requiredPermissions(), REQUEST_BLE_PERMISSIONS);
            return;
        }
        startScan();
    }

    private void startSender() {
        if (pendingSendFile == null) {
            setStatus("No text file selected", "Choose a text file first.");
            return;
        }
        transferMode = TransferMode.SEND_TO_TYPEWRT;
        if (!hasRequiredPermissions()) {
            requestPermissions(requiredPermissions(), REQUEST_BLE_PERMISSIONS);
            return;
        }
        startScan();
    }

    private void chooseTextFile() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("text/*");
        intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
            "text/*",
            "application/json",
            "application/xml",
            "application/x-subrip"
        });
        startActivityForResult(intent, REQUEST_PICK_TEXT_FILE);
    }

    @Override
    public void onRequestPermissionsResult(
        int requestCode,
        String[] permissions,
        int[] grantResults
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != REQUEST_BLE_PERMISSIONS) {
            return;
        }
        if (hasRequiredPermissions()) {
            startScan();
        } else {
            setStatus("Bluetooth permission denied", "The app needs BLE permission to find Typewrt.");
            appendLog("Permission denied.");
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_PICK_TEXT_FILE) {
            return;
        }
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            return;
        }
        prepareSelectedTextFile(data.getData());
    }

    private String[] requiredPermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            return new String[] {
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_CONNECT
            };
        }
        return new String[] { Manifest.permission.ACCESS_FINE_LOCATION };
    }

    private boolean hasRequiredPermissions() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return true;
        }
        for (String permission : requiredPermissions()) {
            if (checkSelfPermission(permission) != PackageManager.PERMISSION_GRANTED) {
                return false;
            }
        }
        return true;
    }

    @SuppressLint("MissingPermission")
    private void startScan() {
        if (bluetoothAdapter == null) {
            setStatus("Bluetooth is not available", "This phone does not expose a BLE adapter.");
            return;
        }
        if (!bluetoothAdapter.isEnabled()) {
            setStatus("Bluetooth is off", "Enable Bluetooth and try again.");
            appendLog("Bluetooth adapter is disabled.");
            return;
        }
        scanner = bluetoothAdapter.getBluetoothLeScanner();
        if (scanner == null) {
            setStatus("BLE scanner unavailable", "Restart Bluetooth and try again.");
            appendLog("BluetoothLeScanner returned null.");
            return;
        }

        disconnect();
        if (transferMode == TransferMode.RECEIVE_FROM_TYPEWRT) {
            resetReceiver();
        } else {
            resetOutgoingTransfer();
            senderComplete = false;
        }
        scanning = true;
        connectButton.setEnabled(false);
        disconnectButton.setEnabled(true);
        updateSendFileActions();
        if (transferMode == TransferMode.SEND_TO_TYPEWRT) {
            setStatus(
                "Scanning for Typewrt",
                "Run ble recv from the Typewrt menu before sending.");
            appendLog("Scanning to send a text file to service 0xffe0...");
        } else {
            setStatus(
                "Scanning for Typewrt",
                "Run :ble on Typewrt if it is not already advertising.");
            appendLog("Scanning to receive from service 0xffe0...");
        }

        ScanSettings settings = new ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build();
        int generation = ++scanGeneration;
        scanner.startScan(Collections.emptyList(), settings, scanCallback);
        mainHandler.postDelayed(() -> onScanTimeout(generation), SCAN_TIMEOUT_MS);
    }

    private void onScanTimeout(int generation) {
        if (!scanning || generation != scanGeneration) {
            return;
        }
        stopScan();
        if (transferMode == TransferMode.SEND_TO_TYPEWRT) {
            senderComplete = true;
        }
        setIdleButtons();
        if (transferMode == TransferMode.SEND_TO_TYPEWRT) {
            setStatus("Typewrt not found", "Run ble recv from the Typewrt menu, then scan again.");
        } else {
            setStatus("Typewrt not found", "Run :ble on Typewrt, then scan again.");
        }
        appendLog("Scan timed out.");
    }

    @SuppressLint("MissingPermission")
    private void stopScan() {
        if (!scanning) {
            return;
        }
        scanning = false;
        if (scanner != null && hasRequiredPermissions()) {
            scanner.stopScan(scanCallback);
        }
    }

    private final ScanCallback scanCallback = new ScanCallback() {
        @Override
        public void onScanResult(int callbackType, ScanResult result) {
            if (!isTypewrtAdvertisement(result)) {
                return;
            }
            BluetoothDevice device = result.getDevice();
            if (device == null) {
                return;
            }
            stopScan();
            connectToDevice(device);
        }

        @Override
        public void onScanFailed(int errorCode) {
            scanning = false;
            if (transferMode == TransferMode.SEND_TO_TYPEWRT) {
                senderComplete = true;
            }
            setIdleButtons();
            setStatus("Scan failed", "Android BLE scan error " + errorCode + ".");
            appendLog("Scan failed: " + errorCode);
        }
    };

    @SuppressLint("MissingPermission")
    private boolean isTypewrtAdvertisement(ScanResult result) {
        ScanRecord record = result.getScanRecord();
        if (record != null) {
            List<ParcelUuid> serviceUuids = record.getServiceUuids();
            if (serviceUuids != null && serviceUuids.contains(SERVICE_PARCEL_UUID)) {
                return true;
            }
            String advertisedName = record.getDeviceName();
            if ("Typewrt".equalsIgnoreCase(advertisedName)) {
                return true;
            }
        }

        BluetoothDevice device = result.getDevice();
        if (device == null || !hasRequiredPermissions()) {
            return false;
        }
        String deviceName = device.getName();
        return "Typewrt".equalsIgnoreCase(deviceName);
    }

    @SuppressLint("MissingPermission")
    private void connectToDevice(BluetoothDevice device) {
        String name = safeDeviceName(device);
        if (transferMode == TransferMode.SEND_TO_TYPEWRT) {
            setStatus("Connecting to send", name);
        } else {
            setStatus("Connecting to receive", name);
        }
        appendLog("Found " + name + ". Connecting...");
        connectButton.setEnabled(false);
        disconnectButton.setEnabled(true);
        updateSendFileActions();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            activeGatt = device.connectGatt(this, false, gattCallback, BluetoothDevice.TRANSPORT_LE);
        } else {
            activeGatt = device.connectGatt(this, false, gattCallback);
        }
    }

    @SuppressLint("MissingPermission")
    private String safeDeviceName(BluetoothDevice device) {
        if (!hasRequiredPermissions()) {
            return "Typewrt";
        }
        String name = device.getName();
        return name == null || name.isEmpty() ? device.getAddress() : name;
    }

    private String phyName(int phy) {
        switch (phy) {
            case BluetoothDevice.PHY_LE_1M:
                return "1M";
            case BluetoothDevice.PHY_LE_2M:
                return "2M";
            case BluetoothDevice.PHY_LE_CODED:
                return "coded";
            default:
                return String.valueOf(phy);
        }
    }

    private final BluetoothGattCallback gattCallback = new BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        @Override
        public void onConnectionStateChange(BluetoothGatt gatt, int status, int newState) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                mainHandler.post(() -> {
                    if (transferMode == TransferMode.SEND_TO_TYPEWRT) {
                        setStatus("Connected", "Discovering Typewrt receive characteristic...");
                    } else {
                        setStatus("Connected", "Discovering Typewrt service...");
                    }
                    appendLog("Connected.");
                });
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    gatt.setPreferredPhy(
                        BluetoothDevice.PHY_LE_2M_MASK,
                        BluetoothDevice.PHY_LE_2M_MASK,
                        BluetoothDevice.PHY_OPTION_NO_PREFERRED);
                }
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                    gatt.requestMtu(REQUESTED_BLE_MTU);
                }
                gatt.discoverServices();
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                mainHandler.post(() -> {
                    if (transferMode == TransferMode.SEND_TO_TYPEWRT) {
                        if (!senderComplete) {
                            setStatus("Send interrupted", "The BLE link closed.");
                            appendLog("Disconnected while sending.");
                            senderComplete = true;
                        }
                    } else {
                        if (!receiverComplete) {
                            setStatus("Disconnected", "The BLE link closed.");
                            appendLog("Disconnected.");
                        }
                    }
                    closeGatt(gatt);
                    resetOutgoingTransfer();
                    setIdleButtons();
                });
            }
        }

        @Override
        public void onMtuChanged(BluetoothGatt gatt, int mtu, int status) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                bleWriteChunkSize = Math.max(
                    DEFAULT_BLE_WRITE_CHUNK,
                    Math.min(MAX_BLE_WRITE_CHUNK, mtu - 3));
            }
            mainHandler.post(() -> appendLog("MTU " + mtu + "."));
        }

        @Override
        public void onPhyUpdate(BluetoothGatt gatt, int txPhy, int rxPhy, int status) {
            mainHandler.post(() ->
                appendLog("PHY tx=" + phyName(txPhy) + " rx=" + phyName(rxPhy)
                    + " status=" + status + "."));
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt gatt, int status) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                mainHandler.post(() -> {
                    setStatus("Service discovery failed", "GATT status " + status + ".");
                    appendLog("Service discovery failed: " + status);
                });
                return;
            }
            BluetoothGattService service = gatt.getService(SERVICE_UUID);
            if (transferMode == TransferMode.SEND_TO_TYPEWRT) {
                rxCharacteristic = service == null ? null : service.getCharacteristic(RX_UUID);
                if (rxCharacteristic == null) {
                    failOutgoingTransfer(
                        gatt,
                        "Service 0xffe0 or RX characteristic 0xffe2 not found.");
                    return;
                }
                beginPreparedSend(gatt);
                return;
            }

            txCharacteristic = service == null ? null : service.getCharacteristic(TX_UUID);
            if (txCharacteristic == null) {
                mainHandler.post(() -> {
                    setStatus("Typewrt service missing", "The device does not expose 0xffe0/0xffe1.");
                    appendLog("Service 0xffe0 or TX characteristic 0xffe1 not found.");
                });
                return;
            }
            enableNotifications(gatt, txCharacteristic);
        }

        @Override
        public void onDescriptorWrite(
            BluetoothGatt gatt,
            BluetoothGattDescriptor descriptor,
            int status
        ) {
            if (CCCD_UUID.equals(descriptor.getUuid()) && status == BluetoothGatt.GATT_SUCCESS) {
                mainHandler.post(() -> {
                    setStatus("Receiving", "Waiting for the file header...");
                    appendLog("Notifications enabled.");
                });
            } else if (CCCD_UUID.equals(descriptor.getUuid())) {
                mainHandler.post(() -> {
                    setStatus("Notification setup failed", "GATT status " + status + ".");
                    appendLog("Descriptor write failed: " + status);
                });
            }
        }

        @Override
        public void onCharacteristicChanged(
            BluetoothGatt gatt,
            BluetoothGattCharacteristic characteristic
        ) {
            if (TX_UUID.equals(characteristic.getUuid())) {
                handleNotification(characteristic.getValue());
            }
        }

        @Override
        public void onCharacteristicChanged(
            BluetoothGatt gatt,
            BluetoothGattCharacteristic characteristic,
            byte[] value
        ) {
            if (TX_UUID.equals(characteristic.getUuid())) {
                handleNotification(value);
            }
        }

        @Override
        public void onCharacteristicWrite(
            BluetoothGatt gatt,
            BluetoothGattCharacteristic characteristic,
            int status
        ) {
            if (RX_UUID.equals(characteristic.getUuid())) {
                handleOutgoingWrite(gatt, status);
            }
        }
    };

    @SuppressLint("MissingPermission")
    private void enableNotifications(
        BluetoothGatt gatt,
        BluetoothGattCharacteristic characteristic
    ) {
        if (!hasRequiredPermissions()) {
            return;
        }
        boolean localEnabled = gatt.setCharacteristicNotification(characteristic, true);
        BluetoothGattDescriptor descriptor = characteristic.getDescriptor(CCCD_UUID);
        if (!localEnabled || descriptor == null) {
            mainHandler.post(() -> {
                setStatus("Notification setup failed", "The TX characteristic is not notifiable.");
                appendLog("Unable to enable local notifications.");
            });
            return;
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            int rc = gatt.writeDescriptor(
                descriptor,
                BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
            if (rc != BluetoothStatusCodes.SUCCESS) {
                mainHandler.post(() -> {
                    setStatus("Notification setup failed", "Descriptor write returned " + rc + ".");
                    appendLog("Descriptor write returned: " + rc);
                });
            }
        } else {
            descriptor.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
            if (!gatt.writeDescriptor(descriptor)) {
                mainHandler.post(() -> {
                    setStatus("Notification setup failed", "Descriptor write could not start.");
                    appendLog("Descriptor write could not start.");
                });
            }
        }
    }

    private void prepareSelectedTextFile(Uri uri) {
        pendingSendFile = null;
        updateSendFileActions();
        setStatus("Preparing text file", "Reading selected document...");
        appendLog("Preparing selected text file.");
        Thread thread = new Thread(() -> {
            try {
                byte[] data = readSelectedBytes(uri);
                String name = sanitizeFileName(displayNameForUri(uri));
                FileSnapshot snapshot = new FileSnapshot(name, data, uri);
                mainHandler.post(() -> {
                    pendingSendFile = snapshot;
                    setStatus("Prepared " + snapshot.name, snapshot.data.length + " bytes");
                    appendLog("Prepared: " + snapshot.name + " (" + snapshot.data.length + " bytes)");
                    updateSendFileActions();
                });
            } catch (IOException | RuntimeException e) {
                mainHandler.post(() -> {
                    setStatus("Could not prepare file", usefulMessage(e));
                    appendLog("Prepare failed: " + e);
                    updateSendFileActions();
                });
            }
        }, "typewrt-prepare-send");
        thread.start();
    }

    private byte[] readSelectedBytes(Uri uri) throws IOException {
        try (InputStream in = getContentResolver().openInputStream(uri);
             ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            if (in == null) {
                throw new IOException("Could not open selected file.");
            }
            byte[] buffer = new byte[4096];
            long total = 0;
            int n;
            while ((n = in.read(buffer)) != -1) {
                total += n;
                if (total > MAX_FILE_BYTES || total > Integer.MAX_VALUE) {
                    throw new IOException("Selected file is larger than 20 MB.");
                }
                out.write(buffer, 0, n);
            }
            return out.toByteArray();
        }
    }

    private String displayNameForUri(Uri uri) {
        String name = null;
        try (Cursor cursor = getContentResolver().query(
            uri,
            new String[] {OpenableColumns.DISPLAY_NAME},
            null,
            null,
            null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (index >= 0) {
                    name = cursor.getString(index);
                }
            }
        } catch (RuntimeException ignored) {
            name = null;
        }
        if (name == null || name.trim().isEmpty()) {
            name = uri.getLastPathSegment();
        }
        if (name == null || name.trim().isEmpty()) {
            name = "typewrt.txt";
        }
        return name;
    }

    private void updateSendFileActions() {
        FileSnapshot snapshot = pendingSendFile;
        boolean hasPreparedFile = snapshot != null;
        if (preparedFileView != null) {
            if (hasPreparedFile) {
                preparedFileView.setText(
                    "Prepared file: " + snapshot.name + " (" + snapshot.data.length + " bytes)");
            } else {
                preparedFileView.setText("No text file selected.");
            }
        }
        if (chooseTextButton != null) {
            chooseTextButton.setEnabled(activeGatt == null && !scanning);
        }
        if (sendTypewrtButton != null) {
            sendTypewrtButton.setEnabled(
                hasPreparedFile && bluetoothAdapter != null && activeGatt == null && !scanning);
        }
    }

    private void setIdleButtons() {
        if (connectButton != null) {
            connectButton.setEnabled(bluetoothAdapter != null);
        }
        if (disconnectButton != null) {
            disconnectButton.setEnabled(false);
        }
        updateSendFileActions();
    }

    private void beginPreparedSend(BluetoothGatt gatt) {
        FileSnapshot snapshot = pendingSendFile;
        if (snapshot == null) {
            failOutgoingTransfer(gatt, "No text file selected.");
            return;
        }

        String name = sanitizeFileName(snapshot.name);
        byte[] header = ("TYPEWRT-FILE " + snapshot.data.length + " " + name + "\n")
            .getBytes(StandardCharsets.UTF_8);
        byte[] footer = ("\nTYPEWRT-END " + snapshot.data.length + " " + name + "\n")
            .getBytes(StandardCharsets.UTF_8);
        outgoingParts = new byte[][] {header, snapshot.data, footer};
        outgoingPartIndex = 0;
        outgoingPartOffset = 0;
        outgoingPendingLength = 0;
        outgoingBytesSent = 0;
        senderComplete = false;

        mainHandler.post(() -> {
            setStatus("Sending " + name, "0 / " + snapshot.data.length + " bytes");
            appendLog("Sending: " + name + " (" + snapshot.data.length + " bytes)");
            updateSendFileActions();
        });
        writeNextOutgoingChunk(gatt);
    }

    @SuppressLint("MissingPermission")
    private void writeNextOutgoingChunk(BluetoothGatt gatt) {
        if (gatt == null || rxCharacteristic == null || outgoingParts == null) {
            failOutgoingTransfer(gatt, "BLE sender is not ready.");
            return;
        }
        while (outgoingPartIndex < outgoingParts.length
            && outgoingPartOffset >= outgoingParts[outgoingPartIndex].length) {
            outgoingPartIndex++;
            outgoingPartOffset = 0;
        }
        if (outgoingPartIndex >= outgoingParts.length) {
            finishOutgoingTransfer(gatt);
            return;
        }

        byte[] part = outgoingParts[outgoingPartIndex];
        int length = Math.min(bleWriteChunkSize, part.length - outgoingPartOffset);
        byte[] chunk = new byte[length];
        System.arraycopy(part, outgoingPartOffset, chunk, 0, length);
        outgoingPendingLength = length;
        rxCharacteristic.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);

        boolean started;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            int rc = gatt.writeCharacteristic(
                rxCharacteristic,
                chunk,
                BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
            started = rc == BluetoothStatusCodes.SUCCESS;
            if (!started) {
                failOutgoingTransfer(gatt, "BLE write could not start: " + rc);
                return;
            }
        } else {
            rxCharacteristic.setValue(chunk);
            started = gatt.writeCharacteristic(rxCharacteristic);
            if (!started) {
                failOutgoingTransfer(gatt, "BLE write could not start.");
                return;
            }
        }
    }

    private void handleOutgoingWrite(BluetoothGatt gatt, int status) {
        if (status != BluetoothGatt.GATT_SUCCESS) {
            failOutgoingTransfer(gatt, "BLE write failed: " + status);
            return;
        }
        int written = outgoingPendingLength;
        outgoingPartOffset += written;
        outgoingBytesSent += written;
        outgoingPendingLength = 0;
        publishOutgoingProgress();
        writeNextOutgoingChunk(gatt);
    }

    private void publishOutgoingProgress() {
        FileSnapshot snapshot = pendingSendFile;
        if (snapshot == null || outgoingParts == null) {
            return;
        }
        int headerLength = outgoingParts.length > 0 ? outgoingParts[0].length : 0;
        int fileBytesSent = Math.max(0, Math.min(snapshot.data.length, outgoingBytesSent - headerLength));
        String name = snapshot.name;
        mainHandler.post(() ->
            setStatus("Sending " + name, fileBytesSent + " / " + snapshot.data.length + " bytes"));
    }

    @SuppressLint("MissingPermission")
    private void finishOutgoingTransfer(BluetoothGatt gatt) {
        FileSnapshot snapshot = pendingSendFile;
        String name = snapshot == null ? "file" : snapshot.name;
        senderComplete = true;
        resetOutgoingTransfer();
        mainHandler.post(() -> {
            setStatus("Sent " + name, "Saved in the Typewrt menu directory.");
            appendLog("Sent: " + name);
        });
        if (gatt != null && hasRequiredPermissions()) {
            gatt.disconnect();
        }
    }

    @SuppressLint("MissingPermission")
    private void failOutgoingTransfer(BluetoothGatt gatt, String message) {
        senderComplete = true;
        resetOutgoingTransfer();
        mainHandler.post(() -> {
            setStatus("Send failed", message);
            appendLog(message);
            setIdleButtons();
        });
        if (gatt != null && hasRequiredPermissions()) {
            gatt.disconnect();
        }
    }

    private void resetOutgoingTransfer() {
        outgoingParts = null;
        outgoingPartIndex = 0;
        outgoingPartOffset = 0;
        outgoingPendingLength = 0;
        outgoingBytesSent = 0;
    }

    @SuppressLint("MissingPermission")
    private void disconnect() {
        stopScan();
        if (activeGatt != null && hasRequiredPermissions()) {
            activeGatt.disconnect();
            closeGatt(activeGatt);
        }
        activeGatt = null;
        txCharacteristic = null;
        rxCharacteristic = null;
        senderComplete = true;
        resetOutgoingTransfer();
        setIdleButtons();
    }

    @SuppressLint("MissingPermission")
    private void closeGatt(BluetoothGatt gatt) {
        if (gatt == null) {
            return;
        }
        if (activeGatt == gatt) {
            activeGatt = null;
            txCharacteristic = null;
            rxCharacteristic = null;
        }
        gatt.close();
    }

    private void resetReceiver() {
        synchronized (receiveLock) {
            headerBuffer = new ByteArrayOutputStream();
            fileBuffer = new ByteArrayOutputStream();
            expectedSize = -1;
            receivedSize = 0;
            currentFileName = "typewrt.txt";
            receiverComplete = false;
        }
    }

    private void handleNotification(byte[] value) {
        if (value == null || value.length == 0) {
            return;
        }

        synchronized (receiveLock) {
            int offset = 0;
            while (offset < value.length && !receiverComplete) {
                if (expectedSize < 0) {
                    int newline = indexOf(value, offset, value.length, (byte) '\n');
                    if (newline < 0) {
                        headerBuffer.write(value, offset, value.length - offset);
                        if (headerBuffer.size() > 256) {
                            failTransfer("Header is too long.");
                        }
                        return;
                    }

                    headerBuffer.write(value, offset, newline - offset);
                    String header = new String(headerBuffer.toByteArray(), StandardCharsets.UTF_8);
                    headerBuffer.reset();
                    if (!parseHeader(header)) {
                        failTransfer("Invalid header: " + header);
                        return;
                    }
                    offset = newline + 1;
                } else {
                    long remaining = expectedSize - receivedSize;
                    if (remaining <= 0) {
                        finishReceiveLocked();
                        return;
                    }
                    int take = (int) Math.min(remaining, value.length - offset);
                    fileBuffer.write(value, offset, take);
                    receivedSize += take;
                    offset += take;
                    publishProgress();
                    if (receivedSize == expectedSize) {
                        finishReceiveLocked();
                        return;
                    }
                }
            }
        }
    }

    private boolean parseHeader(String header) {
        if (!header.startsWith("TYPEWRT-FILE ")) {
            return false;
        }
        String rest = header.substring("TYPEWRT-FILE ".length());
        int space = rest.indexOf(' ');
        if (space <= 0 || space >= rest.length() - 1) {
            return false;
        }

        long size;
        try {
            size = Long.parseLong(rest.substring(0, space));
        } catch (NumberFormatException e) {
            return false;
        }
        if (size < 0 || size > MAX_FILE_BYTES || size > Integer.MAX_VALUE) {
            return false;
        }

        expectedSize = size;
        receivedSize = 0;
        currentFileName = sanitizeFileName(rest.substring(space + 1).trim());
        fileBuffer = new ByteArrayOutputStream((int) Math.min(size, 1024 * 1024));

        mainHandler.post(() -> {
            setStatus("Receiving " + currentFileName, "0 / " + expectedSize + " bytes");
            appendLog("Header: " + currentFileName + " (" + expectedSize + " bytes)");
        });

        if (expectedSize == 0) {
            finishReceiveLocked();
        }
        return true;
    }

    private int indexOf(byte[] data, int start, int end, byte needle) {
        for (int i = start; i < end; i++) {
            if (data[i] == needle) {
                return i;
            }
        }
        return -1;
    }

    private void publishProgress() {
        long nowReceived = receivedSize;
        long nowExpected = expectedSize;
        String name = currentFileName;
        mainHandler.post(() -> {
            setStatus("Receiving " + name, nowReceived + " / " + nowExpected + " bytes");
        });
    }

    private void finishReceiveLocked() {
        receiverComplete = true;
        byte[] data = fileBuffer.toByteArray();
        String name = currentFileName;
        mainHandler.post(() -> {
            setStatus("Saving " + name, data.length + " bytes received.");
            appendLog("Transfer complete. Saving...");
        });
        Thread saveThread = new Thread(() -> saveFile(name, data), "typewrt-save");
        saveThread.start();
    }

    private void failTransfer(String message) {
        receiverComplete = true;
        mainHandler.post(() -> {
            setStatus("Transfer failed", message);
            appendLog(message);
        });
    }

    private String sanitizeFileName(String name) {
        if (name == null || name.trim().isEmpty()) {
            return "typewrt.txt";
        }
        StringBuilder safe = new StringBuilder();
        for (int i = 0; i < name.length(); i++) {
            char c = name.charAt(i);
            if (c < 32 || "\\/:*?\"<>|".indexOf(c) >= 0) {
                safe.append('_');
            } else {
                safe.append(c);
            }
        }
        String result = safe.toString().trim();
        if (result.equals(".") || result.equals("..") || result.isEmpty()) {
            return "typewrt.txt";
        }
        return result;
    }

    private void setLatestFile(String fileName, byte[] data, Uri uri) {
        synchronized (latestFileLock) {
            latestFileName = fileName;
            latestFileData = data;
            latestFileUri = uri;
        }
        updateFileActions();
        if (githubPathField != null && githubPathField.getText().toString().trim().isEmpty()) {
            githubPathField.setText("typewrt/" + sanitizeRepoPath(fileName));
        }
    }

    private void updateFileActions() {
        FileSnapshot snapshot = latestFileSnapshot();
        boolean hasFile = snapshot != null;
        if (activeFileView != null) {
            if (hasFile) {
                activeFileView.setText(
                    "Latest file: " + snapshot.name + " (" + snapshot.data.length + " bytes)");
            } else {
                activeFileView.setText("No file received yet.");
            }
        }
        if (pandocExportButton != null) {
            pandocExportButton.setEnabled(hasFile);
        }
        if (githubUploadButton != null) {
            githubUploadButton.setEnabled(hasFile);
        }
    }

    private FileSnapshot latestFileSnapshot() {
        synchronized (latestFileLock) {
            if (latestFileData == null || latestFileName == null) {
                return null;
            }
            return new FileSnapshot(latestFileName, latestFileData, latestFileUri);
        }
    }

    private void exportLatestWithPandoc() {
        FileSnapshot snapshot = latestFileSnapshot();
        if (snapshot == null) {
            setStatus("Nothing to export", "Receive a Typewrt file first.");
            return;
        }

        String serverUrl = pandocServerField.getText().toString().trim();
        if (serverUrl.isEmpty()) {
            setStatus("Pandoc server missing", "Enter a pandoc-server root URL.");
            return;
        }
        if (serverUrl.contains("pandoc.org/app")) {
            setStatus("Pandoc app is browser-only", "Use a pandoc-server URL, not pandoc.org/app.");
            appendLog("pandoc.org/app runs in the browser and does not accept API uploads.");
            return;
        }

        String from = selectedString(pandocFromSpinner);
        String to = selectedString(pandocToSpinner);
        String outputName = convertedFileName(snapshot.name, to);
        pandocExportButton.setEnabled(false);
        setStatus("Exporting with Pandoc", outputName);
        appendLog("Pandoc export: " + from + " -> " + to);

        Thread thread = new Thread(() -> {
            try {
                String text = new String(snapshot.data, StandardCharsets.UTF_8);
                byte[] output = postPandoc(serverUrl, text, from, to);
                saveFile(outputName, output);
            } catch (IOException | JSONException e) {
                mainHandler.post(() -> {
                    setStatus("Pandoc export failed", usefulMessage(e));
                    appendLog("Pandoc failed: " + e);
                    updateFileActions();
                });
            }
        }, "typewrt-pandoc");
        thread.start();
    }

    private byte[] postPandoc(
        String serverUrl,
        String text,
        String from,
        String to
    ) throws IOException, JSONException {
        URL url = new URL(serverUrl);
        HttpURLConnection connection = (HttpURLConnection) url.openConnection();
        connection.setRequestMethod("POST");
        connection.setConnectTimeout(HTTP_TIMEOUT_MS);
        connection.setReadTimeout(HTTP_TIMEOUT_MS);
        connection.setDoOutput(true);
        connection.setRequestProperty("Accept", "application/octet-stream");
        connection.setRequestProperty("Content-Type", "application/json; charset=utf-8");

        JSONObject request = new JSONObject();
        request.put("text", text);
        request.put("from", from);
        request.put("to", to);
        request.put("standalone", true);

        byte[] body = request.toString().getBytes(StandardCharsets.UTF_8);
        try (OutputStream out = connection.getOutputStream()) {
            out.write(body);
        }

        int status = connection.getResponseCode();
        byte[] response = readResponse(connection);
        connection.disconnect();
        if (status < 200 || status >= 300) {
            throw new IOException("HTTP " + status + ": " + preview(response));
        }
        return response;
    }

    private void uploadLatestToGithub() {
        FileSnapshot snapshot = latestFileSnapshot();
        if (snapshot == null) {
            setStatus("Nothing to upload", "Receive or export a file first.");
            return;
        }

        String repoText = githubRepoField.getText().toString().trim();
        String path = githubPathField.getText().toString().trim();
        String branch = githubBranchField.getText().toString().trim();
        String token = githubTokenField.getText().toString().trim();
        if (repoText.isEmpty() || !repoText.contains("/")) {
            setStatus("GitHub repository missing", "Use owner/repository.");
            return;
        }
        if (path.isEmpty()) {
            path = "typewrt/" + sanitizeRepoPath(snapshot.name);
            githubPathField.setText(path);
        }
        if (branch.isEmpty()) {
            branch = "main";
            githubBranchField.setText(branch);
        }
        if (token.isEmpty()) {
            setStatus("GitHub token missing", "Use a token with Contents: write.");
            return;
        }

        String[] repo = repoText.split("/", 2);
        String owner = repo[0].trim();
        String name = repo[1].trim();
        if (owner.isEmpty() || name.isEmpty()) {
            setStatus("GitHub repository missing", "Use owner/repository.");
            return;
        }

        githubUploadButton.setEnabled(false);
        setStatus("Sending to GitHub", owner + "/" + name + "/" + path);
        appendLog("GitHub upload: " + owner + "/" + name + " " + path);

        String finalPath = path;
        String finalBranch = branch;
        Thread thread = new Thread(() -> {
            try {
                String sha = fetchGithubSha(owner, name, finalPath, finalBranch, token);
                String htmlUrl = putGithubFile(
                    owner,
                    name,
                    finalPath,
                    finalBranch,
                    token,
                    snapshot,
                    sha);
                mainHandler.post(() -> {
                    setStatus("Sent to GitHub", finalPath);
                    appendLog("GitHub saved: " + htmlUrl);
                    updateFileActions();
                });
            } catch (IOException | JSONException e) {
                mainHandler.post(() -> {
                    setStatus("GitHub upload failed", usefulMessage(e));
                    appendLog("GitHub failed: " + e);
                    updateFileActions();
                });
            }
        }, "typewrt-github");
        thread.start();
    }

    private String fetchGithubSha(
        String owner,
        String repo,
        String path,
        String branch,
        String token
    ) throws IOException, JSONException {
        String endpoint = "https://api.github.com/repos/" + encodePathPart(owner)
            + "/" + encodePathPart(repo)
            + "/contents/" + encodeRepoPath(path)
            + "?ref=" + encodePathPart(branch);
        HttpURLConnection connection = openGithubConnection(endpoint, "GET", token);
        int status = connection.getResponseCode();
        byte[] response = readResponse(connection);
        connection.disconnect();
        if (status == 404) {
            return null;
        }
        if (status < 200 || status >= 300) {
            throw new IOException("HTTP " + status + ": " + preview(response));
        }
        return new JSONObject(new String(response, StandardCharsets.UTF_8)).optString("sha", null);
    }

    private String putGithubFile(
        String owner,
        String repo,
        String path,
        String branch,
        String token,
        FileSnapshot snapshot,
        String sha
    ) throws IOException, JSONException {
        String endpoint = "https://api.github.com/repos/" + encodePathPart(owner)
            + "/" + encodePathPart(repo)
            + "/contents/" + encodeRepoPath(path);
        HttpURLConnection connection = openGithubConnection(endpoint, "PUT", token);
        connection.setDoOutput(true);
        connection.setRequestProperty("Content-Type", "application/json; charset=utf-8");

        JSONObject request = new JSONObject();
        request.put("message", "Add " + snapshot.name + " from Typewrt");
        request.put("content", Base64.encodeToString(snapshot.data, Base64.NO_WRAP));
        request.put("branch", branch);
        if (sha != null && !sha.isEmpty()) {
            request.put("sha", sha);
        }

        try (OutputStream out = connection.getOutputStream()) {
            out.write(request.toString().getBytes(StandardCharsets.UTF_8));
        }

        int status = connection.getResponseCode();
        byte[] response = readResponse(connection);
        connection.disconnect();
        if (status < 200 || status >= 300) {
            throw new IOException("HTTP " + status + ": " + preview(response));
        }
        JSONObject result = new JSONObject(new String(response, StandardCharsets.UTF_8));
        JSONObject content = result.optJSONObject("content");
        if (content != null) {
            String htmlUrl = content.optString("html_url", "");
            if (!htmlUrl.isEmpty()) {
                return htmlUrl;
            }
        }
        return owner + "/" + repo + "/" + path;
    }

    private HttpURLConnection openGithubConnection(
        String endpoint,
        String method,
        String token
    ) throws IOException {
        HttpURLConnection connection = (HttpURLConnection) new URL(endpoint).openConnection();
        connection.setRequestMethod(method);
        connection.setConnectTimeout(HTTP_TIMEOUT_MS);
        connection.setReadTimeout(HTTP_TIMEOUT_MS);
        connection.setRequestProperty("Accept", "application/vnd.github+json");
        connection.setRequestProperty("Authorization", "Bearer " + token);
        connection.setRequestProperty("X-GitHub-Api-Version", GITHUB_API_VERSION);
        return connection;
    }

    private byte[] readResponse(HttpURLConnection connection) throws IOException {
        InputStream in;
        try {
            in = connection.getInputStream();
        } catch (IOException e) {
            in = connection.getErrorStream();
            if (in == null) {
                throw e;
            }
        }
        try (InputStream stream = in; ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[4096];
            int n;
            while ((n = stream.read(buffer)) != -1) {
                out.write(buffer, 0, n);
            }
            return out.toByteArray();
        }
    }

    private String convertedFileName(String fileName, String format) {
        String base = fileName;
        int dot = base.lastIndexOf('.');
        if (dot > 0) {
            base = base.substring(0, dot);
        }
        return sanitizeFileName(base + extensionForFormat(format));
    }

    private String extensionForFormat(String format) {
        switch (format) {
            case "docx":
                return ".docx";
            case "epub":
                return ".epub";
            case "html":
                return ".html";
            case "gfm":
            case "markdown":
                return ".md";
            case "rst":
                return ".rst";
            case "latex":
                return ".tex";
            case "org":
                return ".org";
            case "plain":
            default:
                return ".txt";
        }
    }

    private String sanitizeRepoPath(String path) {
        String clean = path == null ? "" : path.trim().replace('\\', '/');
        while (clean.startsWith("/")) {
            clean = clean.substring(1);
        }
        while (clean.contains("//")) {
            clean = clean.replace("//", "/");
        }
        if (clean.isEmpty() || clean.equals(".") || clean.equals("..")) {
            return "typewrt.txt";
        }
        return clean;
    }

    private String encodeRepoPath(String path) {
        String clean = sanitizeRepoPath(path);
        StringBuilder encoded = new StringBuilder();
        String[] parts = clean.split("/");
        for (int i = 0; i < parts.length; i++) {
            if (i > 0) {
                encoded.append('/');
            }
            encoded.append(encodePathPart(parts[i]));
        }
        return encoded.toString();
    }

    private String encodePathPart(String value) {
        try {
            return URLEncoder.encode(value, "UTF-8").replace("+", "%20");
        } catch (IOException e) {
            return value;
        }
    }

    private String selectedString(Spinner spinner) {
        Object selected = spinner.getSelectedItem();
        return selected == null ? "" : selected.toString();
    }

    private String preview(byte[] data) {
        String text = new String(data, StandardCharsets.UTF_8).replace('\n', ' ').trim();
        if (text.length() > 160) {
            return text.substring(0, 160) + "...";
        }
        return text;
    }

    private String usefulMessage(Exception e) {
        String msg = e.getMessage();
        return msg == null || msg.isEmpty() ? e.toString() : msg;
    }

    private static final class FileSnapshot {
        final String name;
        final byte[] data;
        final Uri uri;

        FileSnapshot(String name, byte[] data, Uri uri) {
            this.name = name;
            this.data = data;
            this.uri = uri;
        }
    }

    private void saveFile(String fileName, byte[] data) {
        ContentResolver resolver = getContentResolver();
        ContentValues values = new ContentValues();
        values.put(MediaStore.Downloads.DISPLAY_NAME, fileName);
        values.put(MediaStore.Downloads.MIME_TYPE, guessMimeType(fileName));
        values.put(
            MediaStore.Downloads.RELATIVE_PATH,
            Environment.DIRECTORY_DOWNLOADS + "/Typewrt");
        values.put(MediaStore.Downloads.IS_PENDING, 1);

        Uri uri = null;
        try {
            uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
            if (uri == null) {
                throw new IOException("MediaStore insert returned null");
            }
            try (OutputStream out = resolver.openOutputStream(uri)) {
                if (out == null) {
                    throw new IOException("Could not open output stream");
                }
                out.write(data);
            }

            ContentValues done = new ContentValues();
            done.put(MediaStore.Downloads.IS_PENDING, 0);
            resolver.update(uri, done, null, null);

            Uri savedUri = uri;
            mainHandler.post(() -> {
                setLatestFile(fileName, data, savedUri);
                setStatus("Saved " + fileName, "Downloads/Typewrt");
                appendLog("Saved: " + savedUri);
                setIdleButtons();
            });
        } catch (IOException | RuntimeException e) {
            if (uri != null) {
                resolver.delete(uri, null, null);
            }
            mainHandler.post(() -> {
                setStatus("Save failed", e.getMessage() == null ? e.toString() : e.getMessage());
                appendLog("Save failed: " + e);
                setIdleButtons();
                updateFileActions();
            });
        }
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
        if (lower.endsWith(".txt") || !lower.contains(".")) {
            return "text/plain";
        }
        return "application/octet-stream";
    }

    private void setStatus(String status, String details) {
        statusView.setText(status);
        detailsView.setText(details);
    }

    private void appendLog(String message) {
        String old = logView.getText().toString();
        List<String> lines = new ArrayList<>();
        if (!old.isEmpty()) {
            Collections.addAll(lines, old.split("\\n"));
        }
        lines.add(message);
        while (lines.size() > 80) {
            lines.remove(0);
        }
        logView.setText(String.join("\n", lines));
        logView.post(() -> {
            View parent = (View) logView.getParent();
            if (parent instanceof ScrollView) {
                ((ScrollView) parent).fullScroll(View.FOCUS_DOWN);
            }
        });
    }
}
