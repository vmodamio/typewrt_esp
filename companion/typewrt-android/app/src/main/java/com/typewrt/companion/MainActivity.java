package com.typewrt.companion;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.AlertDialog;
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
import android.content.ClipData;
import android.content.ContentUris;
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
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
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
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.UUID;

import org.json.JSONArray;
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
    private static final long GITHUB_ACCESS_CHECK_DELAY_MS = 900;
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
    private static final String TYPEWRT_DOWNLOAD_DIR = Environment.DIRECTORY_DOWNLOADS + "/Typewrt";

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
    private Button queueDeleteButton;
    private Button clearUpdatesButton;
    private Button pandocExportButton;
    private Button githubUploadButton;
    private Button githubFetchButton;
    private TextView githubAccessView;
    private EditText deletePathField;
    private EditText pandocServerField;
    private EditText githubRepoField;
    private EditText githubPathField;
    private EditText githubBranchField;
    private EditText githubTokenField;
    private Spinner pandocFromSpinner;
    private Spinner pandocToSpinner;
    private Button transferTabButton;
    private Button pandocTabButton;
    private Button githubTabButton;
    private LinearLayout transferTabContent;
    private LinearLayout pandocTabContent;
    private LinearLayout githubTabContent;

    private BluetoothAdapter bluetoothAdapter;
    private BluetoothLeScanner scanner;
    private BluetoothGatt activeGatt;
    private BluetoothGattCharacteristic txCharacteristic;
    private BluetoothGattCharacteristic rxCharacteristic;
    private TransferMode transferMode = TransferMode.RECEIVE_FROM_TYPEWRT;
    private boolean scanning;
    private boolean receiverComplete;
    private boolean receiverFailed;
    private boolean senderComplete = true;
    private int bleWriteChunkSize = DEFAULT_BLE_WRITE_CHUNK;
    private int scanGeneration;
    private int githubAccessGeneration;

    private ByteArrayOutputStream headerBuffer = new ByteArrayOutputStream();
    private ByteArrayOutputStream fileBuffer = new ByteArrayOutputStream();
    private long expectedSize = -1;
    private long receivedSize;
    private String currentFileName = "typewrt.txt";
    private final ArrayList<FileSnapshot> receivedFiles = new ArrayList<>();
    private byte[] latestFileData;
    private String latestFileName;
    private Uri latestFileUri;
    private final ArrayList<FileSnapshot> pendingSendFiles = new ArrayList<>();
    private FileSnapshot activeSendFile;
    private byte[][] outgoingParts;
    private int outgoingFileIndex;
    private int outgoingPartIndex;
    private int outgoingPartOffset;
    private int outgoingPendingLength;
    private int outgoingBytesSent;

    private enum TransferMode {
        RECEIVE_FROM_TYPEWRT,
        SEND_TO_TYPEWRT
    }

    private enum CompanionTab {
        TRANSFER,
        PANDOC,
        GITHUB
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
        root.setPadding(pad, dp(56), pad, pad);
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
        detailsView.setText("BLE sync idle.");
        detailsView.setTextSize(15);
        detailsView.setTextColor(COLOR_TEXT_SECONDARY);
        LinearLayout.LayoutParams detailsParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        detailsParams.topMargin = dp(6);
        root.addView(detailsView, detailsParams);

        LinearLayout tabs = new LinearLayout(this);
        tabs.setOrientation(LinearLayout.HORIZONTAL);
        tabs.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout.LayoutParams tabsParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        tabsParams.topMargin = dp(18);
        root.addView(tabs, tabsParams);

        transferTabButton = addTabButton(tabs, "Transfer", CompanionTab.TRANSFER);
        githubTabButton = addTabButton(tabs, "Git", CompanionTab.GITHUB);
        pandocTabButton = addTabButton(tabs, "Pandoc", CompanionTab.PANDOC);

        transferTabContent = addTabContent(root);
        pandocTabContent = addTabContent(root);
        githubTabContent = addTabContent(root);

        LinearLayout buttons = new LinearLayout(this);
        buttons.setOrientation(LinearLayout.HORIZONTAL);
        buttons.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout.LayoutParams buttonsParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        buttonsParams.topMargin = dp(18);
        transferTabContent.addView(buttons, buttonsParams);

        connectButton = new Button(this);
        connectButton.setText("From Typewrt");
        connectButton.setAllCaps(false);
        styleActionButton(connectButton, true, R.drawable.ic_transfer_receive);
        connectButton.setOnClickListener(v -> startReceiver());
        buttons.addView(connectButton, new LinearLayout.LayoutParams(
            0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        sendTypewrtButton = new Button(this);
        sendTypewrtButton.setText("To Typewrt");
        sendTypewrtButton.setAllCaps(false);
        styleActionButton(sendTypewrtButton, true, R.drawable.ic_transfer_send);
        sendTypewrtButton.setEnabled(false);
        sendTypewrtButton.setOnClickListener(v -> startSender());
        LinearLayout.LayoutParams sendTypewrtParams = new LinearLayout.LayoutParams(
            0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
        sendTypewrtParams.leftMargin = dp(10);
        buttons.addView(sendTypewrtButton, sendTypewrtParams);

        activeFileView = new TextView(this);
        activeFileView.setText("Phone mirror empty.");
        activeFileView.setTextSize(15);
        activeFileView.setTextColor(COLOR_TEXT_SECONDARY);
        LinearLayout.LayoutParams activeParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        activeParams.topMargin = dp(12);
        transferTabContent.addView(activeFileView, activeParams);

        TextView sendLabel = sectionLabel("Phone update queue");
        transferTabContent.addView(sendLabel);

        preparedFileView = new TextView(this);
        preparedFileView.setText("No updates queued.");
        preparedFileView.setTextSize(15);
        preparedFileView.setTextColor(COLOR_TEXT_SECONDARY);
        LinearLayout.LayoutParams preparedParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        preparedParams.topMargin = dp(6);
        transferTabContent.addView(preparedFileView, preparedParams);

        LinearLayout sendButtons = new LinearLayout(this);
        sendButtons.setOrientation(LinearLayout.HORIZONTAL);
        sendButtons.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout.LayoutParams sendButtonsParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        sendButtonsParams.topMargin = dp(8);
        transferTabContent.addView(sendButtons, sendButtonsParams);

        chooseTextButton = new Button(this);
        chooseTextButton.setText("Add files");
        chooseTextButton.setAllCaps(false);
        styleButton(chooseTextButton, false);
        chooseTextButton.setOnClickListener(v -> chooseTextFile());
        sendButtons.addView(chooseTextButton, new LinearLayout.LayoutParams(
            0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        clearUpdatesButton = new Button(this);
        clearUpdatesButton.setText("Clear queue");
        clearUpdatesButton.setAllCaps(false);
        styleButton(clearUpdatesButton, false);
        clearUpdatesButton.setEnabled(false);
        clearUpdatesButton.setOnClickListener(v -> clearQueuedUpdates());
        LinearLayout.LayoutParams clearParams = new LinearLayout.LayoutParams(
            0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
        clearParams.leftMargin = dp(10);
        sendButtons.addView(clearUpdatesButton, clearParams);

        deletePathField = addTextField(
            transferTabContent,
            "Deleted path, for example notes/draft.txt",
            "");

        LinearLayout utilityButtons = new LinearLayout(this);
        utilityButtons.setOrientation(LinearLayout.HORIZONTAL);
        utilityButtons.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout.LayoutParams utilityParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        utilityParams.topMargin = dp(8);
        transferTabContent.addView(utilityButtons, utilityParams);

        queueDeleteButton = new Button(this);
        queueDeleteButton.setText("Add delete");
        queueDeleteButton.setAllCaps(false);
        styleButton(queueDeleteButton, false);
        queueDeleteButton.setOnClickListener(v -> queueRemoteDelete());
        utilityButtons.addView(queueDeleteButton, new LinearLayout.LayoutParams(
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
        utilityButtons.addView(disconnectButton, disconnectParams);

        TextView pandocLabel = sectionLabel("Pandoc export");
        pandocTabContent.addView(pandocLabel);

        pandocServerField = addTextField(
            pandocTabContent,
            "Pandoc server URL (http://host:3030/)",
            "");
        pandocServerField.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);

        LinearLayout pandocFormats = new LinearLayout(this);
        pandocFormats.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams formatParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        formatParams.topMargin = dp(8);
        pandocTabContent.addView(pandocFormats, formatParams);

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
        pandocTabContent.addView(pandocExportButton, exportParams);

        TextView githubLabel = sectionLabel("GitHub repository");
        githubTabContent.addView(githubLabel);

        githubRepoField = addTextField(githubTabContent, "Repository owner/name", "");
        githubPathField = addTextField(githubTabContent, "Repository root/path (optional)", "");
        githubBranchField = addTextField(githubTabContent, "Branch", "main");
        githubTokenField = addTextField(githubTabContent, "GitHub token", "");
        githubTokenField.setInputType(
            InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        githubAccessView = new TextView(this);
        githubAccessView.setText("Repository access not checked.");
        githubAccessView.setTextSize(14);
        githubAccessView.setTextColor(COLOR_TEXT_SECONDARY);
        LinearLayout.LayoutParams accessParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        accessParams.topMargin = dp(8);
        githubTabContent.addView(githubAccessView, accessParams);
        installGithubConfigWatchers();

        githubFetchButton = new Button(this);
        githubFetchButton.setText("Fetch from GitHub");
        githubFetchButton.setAllCaps(false);
        styleButton(githubFetchButton, true);
        githubFetchButton.setEnabled(false);
        githubFetchButton.setOnClickListener(v -> fetchGithubRepository());
        LinearLayout.LayoutParams fetchParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        fetchParams.topMargin = dp(8);
        githubTabContent.addView(githubFetchButton, fetchParams);

        githubUploadButton = new Button(this);
        githubUploadButton.setText("Send latest file");
        githubUploadButton.setAllCaps(false);
        styleButton(githubUploadButton, true);
        githubUploadButton.setEnabled(false);
        githubUploadButton.setOnClickListener(v -> uploadLatestToGithub());
        LinearLayout.LayoutParams uploadParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        uploadParams.topMargin = dp(8);
        githubTabContent.addView(githubUploadButton, uploadParams);

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
        showTab(CompanionTab.TRANSFER);
        updateSendFileActions();
    }

    private Button addTabButton(LinearLayout root, String text, CompanionTab tab) {
        Button button = new Button(this);
        button.setText(text);
        button.setAllCaps(false);
        button.setTextSize(14);
        button.setMinHeight(dp(44));
        button.setOnClickListener(v -> showTab(tab));
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
            0,
            LinearLayout.LayoutParams.WRAP_CONTENT,
            1);
        if (root.getChildCount() > 0) {
            params.leftMargin = dp(8);
        }
        root.addView(button, params);
        return button;
    }

    private LinearLayout addTabContent(LinearLayout root) {
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        root.addView(content, params);
        return content;
    }

    private void showTab(CompanionTab tab) {
        if (transferTabContent != null) {
            transferTabContent.setVisibility(tab == CompanionTab.TRANSFER ? View.VISIBLE : View.GONE);
        }
        if (pandocTabContent != null) {
            pandocTabContent.setVisibility(tab == CompanionTab.PANDOC ? View.VISIBLE : View.GONE);
        }
        if (githubTabContent != null) {
            githubTabContent.setVisibility(tab == CompanionTab.GITHUB ? View.VISIBLE : View.GONE);
        }
        styleTabButton(transferTabButton, tab == CompanionTab.TRANSFER);
        styleTabButton(pandocTabButton, tab == CompanionTab.PANDOC);
        styleTabButton(githubTabButton, tab == CompanionTab.GITHUB);
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

    private void installGithubConfigWatchers() {
        TextWatcher watcher = new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {
            }

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {
            }

            @Override
            public void afterTextChanged(Editable s) {
                scheduleGithubAccessCheck();
            }
        };

        githubRepoField.addTextChangedListener(watcher);
        githubPathField.addTextChangedListener(watcher);
        githubBranchField.addTextChangedListener(watcher);
        githubTokenField.addTextChangedListener(watcher);
        scheduleGithubAccessCheck();
    }

    private void scheduleGithubAccessCheck() {
        int generation = ++githubAccessGeneration;

        if (githubAccessView == null) {
            return;
        }
        if (!githubConfigReady()) {
            githubAccessView.setText("Enter repository, branch, and token to check access.");
            githubAccessView.setTextColor(COLOR_TEXT_MUTED);
            updateGithubActionButtons();
            return;
        }
        githubAccessView.setText("Repository access check pending...");
        githubAccessView.setTextColor(COLOR_TEXT_SECONDARY);
        updateGithubActionButtons();
        mainHandler.postDelayed(() -> checkGithubAccess(generation),
            GITHUB_ACCESS_CHECK_DELAY_MS);
    }

    private boolean githubConfigReady() {
        if (githubRepoField == null || githubBranchField == null || githubTokenField == null) {
            return false;
        }
        String repo = githubRepoField.getText().toString().trim();
        String branch = githubBranchField.getText().toString().trim();
        String token = githubTokenField.getText().toString().trim();

        return repo.contains("/") && !branch.isEmpty() && !token.isEmpty();
    }

    private void updateGithubActionButtons() {
        boolean ready = githubConfigReady();

        if (githubFetchButton != null) {
            githubFetchButton.setEnabled(ready);
        }
        if (githubUploadButton != null) {
            githubUploadButton.setEnabled(ready && latestFileSnapshot() != null);
        }
    }

    private void checkGithubAccess(int generation) {
        String repoText = githubRepoField.getText().toString().trim();
        String branch = githubBranchField.getText().toString().trim();
        String token = githubTokenField.getText().toString().trim();
        String rootPath = githubPathField == null ? "" :
            githubPathField.getText().toString().trim();
        String[] repo = repoText.split("/", 2);

        if (generation != githubAccessGeneration || repo.length != 2 ||
                repo[0].trim().isEmpty() || repo[1].trim().isEmpty() ||
                branch.isEmpty() || token.isEmpty()) {
            return;
        }

        if (githubAccessView != null) {
            githubAccessView.setText("Checking repository access...");
        }
        Thread thread = new Thread(() -> {
            try {
                String result = performGithubAccessCheck(
                    repo[0].trim(),
                    repo[1].trim(),
                    branch,
                    rootPath,
                    token);
                mainHandler.post(() -> {
                    if (generation != githubAccessGeneration) {
                        return;
                    }
                    githubAccessView.setText(result);
                    githubAccessView.setTextColor(COLOR_BLUE);
                });
            } catch (IOException | JSONException e) {
                mainHandler.post(() -> {
                    if (generation != githubAccessGeneration) {
                        return;
                    }
                    githubAccessView.setText("Repository access failed: " + usefulMessage(e));
                    githubAccessView.setTextColor(COLOR_TEXT_SECONDARY);
                });
            }
        }, "typewrt-github-check");
        thread.start();
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

    private void styleActionButton(Button button, boolean primary, int iconResId) {
        styleButton(button, primary);
        button.setMinHeight(dp(86));
        button.setTextSize(17);
        button.setGravity(Gravity.CENTER);
        button.setLineSpacing(dp(2), 1.0f);
        setButtonTopIcon(button, iconResId, primary ? 0xFF0B1720 : COLOR_TEXT_PRIMARY);
    }

    private void styleTabButton(Button button, boolean selected) {
        if (button == null) {
            return;
        }
        int normalColor = selected ? COLOR_BLUE : COLOR_SURFACE_HIGH;
        int textColor = selected ? 0xFF0B1720 : COLOR_TEXT_PRIMARY;
        button.setTextColor(textColor);
        button.setBackground(rippleBackground(normalColor, 0xFF2A2D30, dp(22)));
    }

    private void setButtonTopIcon(Button button, int iconResId, int tint) {
        Drawable icon = getDrawable(iconResId);

        if (icon == null) {
            return;
        }
        icon = icon.mutate();
        icon.setTint(tint);
        icon.setBounds(0, 0, dp(32), dp(32));
        button.setCompoundDrawables(null, icon, null, null);
        button.setCompoundDrawablePadding(dp(6));
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
        if (pendingSendFiles.isEmpty()) {
            setStatus("No updates queued", "Choose text files or queue a delete marker first.");
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
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {
            "text/*",
            "application/json",
            "application/xml",
            "application/x-markdown",
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
        if (resultCode != RESULT_OK || data == null) {
            return;
        }
        ArrayList<Uri> uris = selectedUris(data);
        if (uris.isEmpty()) {
            return;
        }
        prepareSelectedTextFiles(uris);
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
            appendLog("Scanning to send updates to service 0xffe0...");
        } else {
            setStatus(
                "Scanning for Typewrt",
                "Run :ble send from the Typewrt menu if it is not already advertising.");
            appendLog("Scanning to receive sync files from service 0xffe0...");
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
            setStatus("Typewrt not found", "Run :ble send on Typewrt, then scan again.");
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
                        if (!receiverFailed && finishReceivedSession()) {
                            appendLog("Receive session closed cleanly.");
                        } else if (!receiverComplete) {
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

    private ArrayList<Uri> selectedUris(Intent data) {
        ArrayList<Uri> uris = new ArrayList<>();
        ClipData clipData = data.getClipData();

        if (clipData != null) {
            for (int i = 0; i < clipData.getItemCount(); i++) {
                Uri uri = clipData.getItemAt(i).getUri();
                if (uri != null) {
                    uris.add(uri);
                }
            }
        } else if (data.getData() != null) {
            uris.add(data.getData());
        }
        return uris;
    }

    private void prepareSelectedTextFiles(List<Uri> uris) {
        updateSendFileActions();
        setStatus("Preparing text files", "Reading " + uris.size() + " selected file" + plural(uris.size()) + "...");
        appendLog("Preparing " + uris.size() + " selected file" + plural(uris.size()) + ".");
        Thread thread = new Thread(() -> {
            try {
                ArrayList<FileSnapshot> snapshots = new ArrayList<>();
                long totalBytes = 0;

                for (Uri uri : uris) {
                    byte[] data = readSelectedBytes(uri);
                    String name = sanitizeTransferPath(displayNameForUri(uri), "typewrt.txt");
                    snapshots.add(FileSnapshot.file(name, data, uri));
                    totalBytes += data.length;
                }
                final long preparedBytes = totalBytes;
                mainHandler.post(() -> {
                    pendingSendFiles.addAll(snapshots);
                    setStatus("Queued " + queueSummary(), preparedBytes + " bytes added");
                    appendLog("Queued files: " + snapshots.size() + " (" + preparedBytes + " bytes)");
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
        return readUriBytes(uri, "Selected file");
    }

    private byte[] readUriBytes(Uri uri, String label) throws IOException {
        try (InputStream in = getContentResolver().openInputStream(uri);
             ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            if (in == null) {
                throw new IOException("Could not open file.");
            }
            byte[] buffer = new byte[4096];
            long total = 0;
            int n;
            while ((n = in.read(buffer)) != -1) {
                total += n;
                if (total > MAX_FILE_BYTES || total > Integer.MAX_VALUE) {
                    throw new IOException(label + " is larger than 20 MB.");
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

    private void queueRemoteDelete() {
        String path = sanitizeTransferPath(
            deletePathField == null ? "" : deletePathField.getText().toString(),
            "");

        if (path.isEmpty()) {
            setStatus("Delete path missing", "Enter a relative Typewrt path first.");
            return;
        }
        pendingSendFiles.add(FileSnapshot.deleteMarker(path));
        if (deletePathField != null) {
            deletePathField.setText("");
        }
        setStatus("Queued " + queueSummary(), path);
        appendLog("Queued delete: " + path);
        updateSendFileActions();
    }

    private void clearQueuedUpdates() {
        pendingSendFiles.clear();
        setStatus("Cleared updates", "No updates queued.");
        appendLog("Cleared queued updates.");
        updateSendFileActions();
    }

    private void updateSendFileActions() {
        boolean hasPreparedFile = !pendingSendFiles.isEmpty();
        if (preparedFileView != null) {
            if (hasPreparedFile) {
                preparedFileView.setText(
                    "Queued: " + queueSummary() + " (" + pendingSendBytes() + " bytes)");
            } else {
                preparedFileView.setText("No updates queued.");
            }
        }
        if (chooseTextButton != null) {
            chooseTextButton.setEnabled(activeGatt == null && !scanning);
        }
        if (sendTypewrtButton != null) {
            sendTypewrtButton.setEnabled(
                hasPreparedFile && bluetoothAdapter != null && activeGatt == null && !scanning);
        }
        if (queueDeleteButton != null) {
            queueDeleteButton.setEnabled(activeGatt == null && !scanning);
        }
        if (clearUpdatesButton != null) {
            clearUpdatesButton.setEnabled(hasPreparedFile && activeGatt == null && !scanning);
        }
    }

    private String sendSelectionLabel() {
        int count = pendingSendFiles.size();

        if (count == 1) {
            FileSnapshot snapshot = pendingSendFiles.get(0);
            return snapshot.deleteMarker ? "delete " + snapshot.name : snapshot.name;
        }
        return queueSummary();
    }

    private String queueSummary() {
        int files = 0;
        int deletes = 0;

        for (FileSnapshot snapshot : pendingSendFiles) {
            if (snapshot.deleteMarker) {
                deletes++;
            } else {
                files++;
            }
        }
        if (files == 0 && deletes == 0) {
            return "no updates";
        }
        if (files > 0 && deletes > 0) {
            return files + " file" + plural(files) + ", " + deletes + " delete" + plural(deletes);
        }
        if (files > 0) {
            return files + " file" + plural(files);
        }
        return deletes + " delete" + plural(deletes);
    }

    private long pendingSendBytes() {
        long total = 0;

        for (FileSnapshot snapshot : pendingSendFiles) {
            if (!snapshot.deleteMarker) {
                total += snapshot.data.length;
            }
        }
        return total;
    }

    private String plural(int count) {
        return count == 1 ? "" : "s";
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
        if (pendingSendFiles.isEmpty()) {
            failOutgoingTransfer(gatt, "No updates queued.");
            return;
        }

        outgoingFileIndex = 0;
        activeSendFile = null;
        senderComplete = false;
        beginNextOutgoingFile(gatt);
    }

    private void beginNextOutgoingFile(BluetoothGatt gatt) {
        if (outgoingFileIndex >= pendingSendFiles.size()) {
            finishOutgoingTransfer(gatt);
            return;
        }

        FileSnapshot snapshot = pendingSendFiles.get(outgoingFileIndex);
        String name = sanitizeTransferPath(snapshot.name, "typewrt.txt");
        activeSendFile = snapshot;
        if (snapshot.deleteMarker) {
            byte[] marker = ("TYPEWRT-DELETE " + name + "\n")
                .getBytes(StandardCharsets.UTF_8);
            outgoingParts = new byte[][] {marker};
        } else {
            byte[] header = ("TYPEWRT-FILE " + snapshot.data.length + " " + name + "\n")
                .getBytes(StandardCharsets.UTF_8);
            byte[] footer = ("\nTYPEWRT-END " + snapshot.data.length + " " + name + "\n")
                .getBytes(StandardCharsets.UTF_8);
            outgoingParts = new byte[][] {header, snapshot.data, footer};
        }
        outgoingPartIndex = 0;
        outgoingPartOffset = 0;
        outgoingPendingLength = 0;
        outgoingBytesSent = 0;

        mainHandler.post(() -> {
            if (snapshot.deleteMarker) {
                setStatus("Sending delete " + name, outgoingFileOrdinal());
                appendLog("Sending delete marker: " + name);
            } else {
                setStatus("Sending " + name, outgoingProgressText(0, snapshot.data.length));
                appendLog("Sending: " + name + " (" + snapshot.data.length + " bytes)");
            }
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
            outgoingFileIndex++;
            beginNextOutgoingFile(gatt);
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
        FileSnapshot snapshot = activeSendFile;
        if (snapshot == null || outgoingParts == null) {
            return;
        }
        if (snapshot.deleteMarker) {
            String ordinal = outgoingFileOrdinal();
            mainHandler.post(() -> setStatus("Sending delete " + snapshot.name, ordinal));
            return;
        }
        int headerLength = outgoingParts.length > 0 ? outgoingParts[0].length : 0;
        int fileBytesSent = Math.max(0, Math.min(snapshot.data.length, outgoingBytesSent - headerLength));
        String name = snapshot.name;
        mainHandler.post(() ->
            setStatus("Sending " + name, outgoingProgressText(fileBytesSent, snapshot.data.length)));
    }

    private String outgoingProgressText(int sent, int total) {
        String bytes = sent + " / " + total + " bytes";
        String ordinal = outgoingFileOrdinal();

        return ordinal.isEmpty() ? bytes : ordinal + "  " + bytes;
    }

    private String outgoingFileOrdinal() {
        int count = pendingSendFiles.size();

        if (count <= 1) {
            return "";
        }
        return (outgoingFileIndex + 1) + " / " + count;
    }

    @SuppressLint("MissingPermission")
    private void finishOutgoingTransfer(BluetoothGatt gatt) {
        String label = pendingSendFiles.isEmpty() ? "updates" : sendSelectionLabel();
        senderComplete = true;
        resetOutgoingTransfer();
        pendingSendFiles.clear();
        mainHandler.post(() -> {
            setStatus("Sent " + label, "Applied in the Typewrt menu directory.");
            appendLog("Sent: " + label);
            updateSendFileActions();
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
        activeSendFile = null;
        outgoingParts = null;
        outgoingFileIndex = 0;
        outgoingPartIndex = 0;
        outgoingPartOffset = 0;
        outgoingPendingLength = 0;
        outgoingBytesSent = 0;
    }

    @SuppressLint("MissingPermission")
    private void disconnect() {
        stopScan();
        if (transferMode == TransferMode.RECEIVE_FROM_TYPEWRT) {
            finishReceivedSession();
        }
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
            receivedFiles.clear();
            receiverComplete = false;
            receiverFailed = false;
        }
    }

    private void handleNotification(byte[] value) {
        if (value == null || value.length == 0) {
            return;
        }

        synchronized (receiveLock) {
            int offset = 0;
            while (offset < value.length && !receiverFailed) {
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
                    if (!parseIncomingLine(header)) {
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
                    }
                }
            }
        }
    }

    private boolean parseIncomingLine(String header) {
        if (header.endsWith("\r")) {
            header = header.substring(0, header.length() - 1);
        }
        if (header.isEmpty() || header.startsWith("TYPEWRT-END ")) {
            return true;
        }
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
        currentFileName = sanitizeTransferPath(rest.substring(space + 1).trim(), "typewrt.txt");
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
        byte[] data = fileBuffer.toByteArray();
        String name = currentFileName;
        receivedFiles.add(FileSnapshot.file(name, data, null));
        expectedSize = -1;
        receivedSize = 0;
        currentFileName = "typewrt.txt";
        fileBuffer = new ByteArrayOutputStream();
        mainHandler.post(() -> {
            setStatus("Received " + name, data.length + " bytes");
            appendLog("Received: " + name + " (" + data.length + " bytes)");
        });
    }

    private void failTransfer(String message) {
        receiverComplete = true;
        receiverFailed = true;
        mainHandler.post(() -> {
            setStatus("Transfer failed", message);
            appendLog(message);
        });
    }

    private boolean finishReceivedSession() {
        ArrayList<FileSnapshot> snapshots;

        synchronized (receiveLock) {
            if (receiverComplete || receiverFailed || expectedSize >= 0 || receivedFiles.isEmpty()) {
                return false;
            }
            snapshots = new ArrayList<>(receivedFiles);
            receivedFiles.clear();
            receiverComplete = true;
        }
        setStatus("Saving received files", snapshots.size() + " file" + plural(snapshots.size()));
        appendLog("Saving " + snapshots.size() + " received file" + plural(snapshots.size()) + ".");
        saveReceivedFiles(snapshots);
        return true;
    }

    private void saveReceivedFiles(List<FileSnapshot> snapshots) {
        Thread saveThread = new Thread(() -> {
            for (FileSnapshot snapshot : snapshots) {
                if (!snapshot.deleteMarker) {
                    saveFile(snapshot.name, snapshot.data);
                }
            }
        }, "typewrt-save-batch");
        saveThread.start();
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

    private String sanitizeTransferPath(String path, String fallback) {
        String clean = path == null ? "" : path.trim().replace('\\', '/');
        StringBuilder out = new StringBuilder();

        while (clean.startsWith("/")) {
            clean = clean.substring(1);
        }
        for (String rawPart : clean.split("/")) {
            String trimmed = rawPart.trim();
            StringBuilder safe = new StringBuilder();

            if (trimmed.isEmpty() || trimmed.equals(".") || trimmed.equals("..")) {
                continue;
            }
            for (int i = 0; i < trimmed.length(); i++) {
                char c = trimmed.charAt(i);
                if (c < 32 || ":*?\"<>|".indexOf(c) >= 0) {
                    safe.append('_');
                } else {
                    safe.append(c);
                }
            }
            String part = safe.toString().trim();
            if (part.isEmpty() || part.equals(".") || part.equals("..")) {
                continue;
            }
            if (out.length() > 0) {
                out.append('/');
            }
            out.append(part);
        }
        if (out.length() == 0) {
            return fallback == null ? "" : fallback;
        }
        return out.toString();
    }

    private String transferDisplayName(String path) {
        String safe = sanitizeTransferPath(path, "typewrt.txt");
        int slash = safe.lastIndexOf('/');

        return slash >= 0 ? safe.substring(slash + 1) : safe;
    }

    private String transferRelativeDir(String path) {
        String safe = sanitizeTransferPath(path, "typewrt.txt");
        int slash = safe.lastIndexOf('/');

        if (slash < 0) {
            return TYPEWRT_DOWNLOAD_DIR;
        }
        return TYPEWRT_DOWNLOAD_DIR + "/" + safe.substring(0, slash);
    }

    private void setLatestFile(String fileName, byte[] data, Uri uri) {
        synchronized (latestFileLock) {
            latestFileName = fileName;
            latestFileData = data;
            latestFileUri = uri;
        }
        updateFileActions();
    }

    private void updateFileActions() {
        FileSnapshot snapshot = latestFileSnapshot();
        boolean hasFile = snapshot != null;
        if (activeFileView != null) {
            if (hasFile) {
                activeFileView.setText(
                    "Latest from Typewrt: " + snapshot.name + " (" + snapshot.data.length + " bytes)");
            } else {
                activeFileView.setText("Phone mirror empty.");
            }
        }
        if (pandocExportButton != null) {
            pandocExportButton.setEnabled(hasFile);
        }
        updateGithubActionButtons();
    }

    private FileSnapshot latestFileSnapshot() {
        synchronized (latestFileLock) {
            if (latestFileData == null || latestFileName == null) {
                return null;
            }
            return FileSnapshot.file(latestFileName, latestFileData, latestFileUri);
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
        String rootPath = githubPathField.getText().toString().trim();
        String branch = githubBranchField.getText().toString().trim();
        String token = githubTokenField.getText().toString().trim();
        if (repoText.isEmpty() || !repoText.contains("/")) {
            setStatus("GitHub repository missing", "Use owner/repository.");
            return;
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

        String path = githubPathForSnapshot(rootPath, snapshot.name);
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

    private void fetchGithubRepository() {
        String repoText = githubRepoField.getText().toString().trim();
        String rootPath = githubPathField.getText().toString().trim();
        String branch = githubBranchField.getText().toString().trim();
        String token = githubTokenField.getText().toString().trim();

        if (repoText.isEmpty() || !repoText.contains("/")) {
            setStatus("GitHub repository missing", "Use owner/repository.");
            return;
        }
        if (branch.isEmpty()) {
            branch = "main";
            githubBranchField.setText(branch);
        }
        if (token.isEmpty()) {
            setStatus("GitHub token missing", "Use a token with Contents: read.");
            return;
        }

        String[] repo = repoText.split("/", 2);
        String owner = repo[0].trim();
        String name = repo[1].trim();
        if (owner.isEmpty() || name.isEmpty()) {
            setStatus("GitHub repository missing", "Use owner/repository.");
            return;
        }

        String finalBranch = branch;
        String finalRootPath = rootPath;
        if (githubFetchButton != null) {
            githubFetchButton.setEnabled(false);
        }
        setStatus("Fetching from GitHub", owner + "/" + name + "@" + branch);
        appendLog("GitHub fetch: " + owner + "/" + name + " " +
            sanitizeOptionalRepoPath(rootPath));

        Thread thread = new Thread(() -> {
            try {
                GitHubFetchResult result = performGithubFetch(
                    owner,
                    name,
                    finalBranch,
                    finalRootPath,
                    token);
                mainHandler.post(() -> {
                    pendingSendFiles.addAll(result.updates);
                    setStatus(
                        "Fetched from GitHub",
                        result.changed + " changed, " + result.unchanged +
                            " unchanged, " + result.deleted + " deleted");
                    appendLog("GitHub fetch queued " + result.updates.size() +
                        " update" + plural(result.updates.size()) +
                        (result.skipped > 0 ? "; skipped " + result.skipped : ""));
                    updateSendFileActions();
                    updateGithubActionButtons();
                });
            } catch (IOException | JSONException e) {
                mainHandler.post(() -> {
                    setStatus("GitHub fetch failed", usefulMessage(e));
                    appendLog("GitHub fetch failed: " + e);
                    updateGithubActionButtons();
                });
            }
        }, "typewrt-github-fetch");
        thread.start();
    }

    private GitHubFetchResult performGithubFetch(
        String owner,
        String repo,
        String branch,
        String rootPath,
        String token
    ) throws IOException, JSONException {
        String root = sanitizeOptionalRepoPath(rootPath);
        ArrayList<GitHubRemoteFile> remoteFiles = new ArrayList<>();
        Set<String> remotePaths = new HashSet<>();
        ArrayList<FileSnapshot> updates = new ArrayList<>();
        GitHubFetchResult result = new GitHubFetchResult(updates);

        collectGithubContents(owner, repo, branch, root, root, token, remoteFiles);
        for (GitHubRemoteFile remote : remoteFiles) {
            String localPath = remoteRelativePath(remote.path, root);

            if (remote.size > MAX_FILE_BYTES || remote.downloadUrl == null ||
                    remote.downloadUrl.isEmpty()) {
                result.skipped++;
                continue;
            }
            remotePaths.add(localPath);
            byte[] data = downloadGithubBytes(remote.downloadUrl, token);
            if (data.length > MAX_FILE_BYTES) {
                result.skipped++;
                continue;
            }
            if (saveMirrorFileSync(localPath, data)) {
                updates.add(FileSnapshot.file(localPath, data, null));
                result.changed++;
            } else {
                result.unchanged++;
            }
        }

        for (LocalMirrorFile local : listLocalMirrorFiles()) {
            if (remotePaths.contains(local.path)) {
                continue;
            }
            if (deleteLocalMirrorFile(local.uri)) {
                updates.add(FileSnapshot.deleteMarker(local.path));
                result.deleted++;
            }
        }
        return result;
    }

    private void collectGithubContents(
        String owner,
        String repo,
        String branch,
        String rootPath,
        String path,
        String token,
        List<GitHubRemoteFile> out
    ) throws IOException, JSONException {
        String endpoint = "https://api.github.com/repos/" + encodePathPart(owner)
            + "/" + encodePathPart(repo)
            + "/contents";

        if (!path.isEmpty()) {
            endpoint += "/" + encodeRepoPath(path);
        }
        endpoint += "?ref=" + encodePathPart(branch);

        HttpURLConnection connection = openGithubConnection(endpoint, "GET", token);
        int status = connection.getResponseCode();
        byte[] response = readResponse(connection);
        connection.disconnect();
        if (status < 200 || status >= 300) {
            throw new IOException("contents HTTP " + status + ": " + preview(response));
        }

        String json = new String(response, StandardCharsets.UTF_8).trim();
        if (json.startsWith("[")) {
            JSONArray array = new JSONArray(json);
            for (int i = 0; i < array.length(); i++) {
                collectGithubEntry(owner, repo, branch, rootPath,
                    array.getJSONObject(i), token, out);
            }
        } else {
            collectGithubEntry(owner, repo, branch, rootPath,
                new JSONObject(json), token, out);
        }
    }

    private void collectGithubEntry(
        String owner,
        String repo,
        String branch,
        String rootPath,
        JSONObject entry,
        String token,
        List<GitHubRemoteFile> out
    ) throws IOException, JSONException {
        String type = entry.optString("type", "");
        String path = entry.optString("path", "");

        if ("dir".equals(type)) {
            collectGithubContents(owner, repo, branch, rootPath, path, token, out);
            return;
        }
        if (!"file".equals(type)) {
            return;
        }
        out.add(new GitHubRemoteFile(
            path,
            entry.optLong("size", 0),
            entry.optString("download_url", "")));
    }

    private String remoteRelativePath(String remotePath, String rootPath) {
        String remote = sanitizeOptionalRepoPath(remotePath);
        String root = sanitizeOptionalRepoPath(rootPath);

        if (!root.isEmpty()) {
            if (remote.equals(root)) {
                return transferDisplayName(remote);
            }
            if (remote.startsWith(root + "/")) {
                remote = remote.substring(root.length() + 1);
            }
        }
        return sanitizeTransferPath(remote, "typewrt.txt");
    }

    private byte[] downloadGithubBytes(String downloadUrl, String token) throws IOException {
        HttpURLConnection connection = (HttpURLConnection) new URL(downloadUrl).openConnection();
        connection.setConnectTimeout(HTTP_TIMEOUT_MS);
        connection.setReadTimeout(HTTP_TIMEOUT_MS);
        connection.setRequestProperty("Accept", "application/octet-stream");
        connection.setRequestProperty("Authorization", "Bearer " + token);
        connection.setRequestProperty("X-GitHub-Api-Version", GITHUB_API_VERSION);
        int status = connection.getResponseCode();
        byte[] response = readResponse(connection);
        connection.disconnect();
        if (status < 200 || status >= 300) {
            throw new IOException("download HTTP " + status + ": " + preview(response));
        }
        return response;
    }

    private String performGithubAccessCheck(
        String owner,
        String repo,
        String branch,
        String rootPath,
        String token
    ) throws IOException, JSONException {
        String repoEndpoint = "https://api.github.com/repos/" + encodePathPart(owner)
            + "/" + encodePathPart(repo);
        HttpURLConnection repoConnection = openGithubConnection(repoEndpoint, "GET", token);
        int repoStatus = repoConnection.getResponseCode();
        byte[] repoResponse = readResponse(repoConnection);
        repoConnection.disconnect();
        if (repoStatus < 200 || repoStatus >= 300) {
            throw new IOException("repo HTTP " + repoStatus + ": " + preview(repoResponse));
        }

        JSONObject repoJson = new JSONObject(new String(repoResponse, StandardCharsets.UTF_8));
        JSONObject permissions = repoJson.optJSONObject("permissions");
        boolean canPush = permissions != null &&
            (permissions.optBoolean("push") ||
                permissions.optBoolean("maintain") ||
                permissions.optBoolean("admin"));

        String branchEndpoint = repoEndpoint + "/branches/" + encodePathPart(branch);
        HttpURLConnection branchConnection = openGithubConnection(branchEndpoint, "GET", token);
        int branchStatus = branchConnection.getResponseCode();
        byte[] branchResponse = readResponse(branchConnection);
        branchConnection.disconnect();
        if (branchStatus < 200 || branchStatus >= 300) {
            throw new IOException("branch HTTP " + branchStatus + ": " + preview(branchResponse));
        }

        String pathNote = "";
        String cleanRoot = sanitizeOptionalRepoPath(rootPath);
        if (!cleanRoot.isEmpty()) {
            String contentsEndpoint = repoEndpoint + "/contents/" + encodeRepoPath(cleanRoot)
                + "?ref=" + encodePathPart(branch);
            HttpURLConnection contentsConnection =
                openGithubConnection(contentsEndpoint, "GET", token);
            int contentsStatus = contentsConnection.getResponseCode();
            byte[] contentsResponse = readResponse(contentsConnection);
            contentsConnection.disconnect();
            if (contentsStatus == 404) {
                pathNote = "; path not found yet";
            } else if (contentsStatus < 200 || contentsStatus >= 300) {
                throw new IOException("path HTTP " + contentsStatus + ": " +
                    preview(contentsResponse));
            } else {
                pathNote = "; path readable";
            }
        }

        return "Repository access OK: " + owner + "/" + repo + "@" + branch +
            (canPush ? "; write likely OK" : "; write not confirmed") + pathNote;
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

    private String sanitizeOptionalRepoPath(String path) {
        String clean = path == null ? "" : path.trim().replace('\\', '/');

        while (clean.startsWith("/")) {
            clean = clean.substring(1);
        }
        while (clean.endsWith("/")) {
            clean = clean.substring(0, clean.length() - 1);
        }
        while (clean.contains("//")) {
            clean = clean.replace("//", "/");
        }
        if (clean.equals(".") || clean.equals("..")) {
            return "";
        }
        return clean;
    }

    private String githubPathForSnapshot(String rootPath, String fileName) {
        String root = sanitizeOptionalRepoPath(rootPath);
        String file = sanitizeRepoPath(fileName);

        if (root.isEmpty()) {
            return file;
        }
        return root + "/" + file;
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
        final boolean deleteMarker;

        FileSnapshot(String name, byte[] data, Uri uri, boolean deleteMarker) {
            this.name = name;
            this.data = data;
            this.uri = uri;
            this.deleteMarker = deleteMarker;
        }

        static FileSnapshot file(String name, byte[] data, Uri uri) {
            return new FileSnapshot(name, data, uri, false);
        }

        static FileSnapshot deleteMarker(String path) {
            return new FileSnapshot(path, new byte[0], null, true);
        }
    }

    private static final class GitHubRemoteFile {
        final String path;
        final long size;
        final String downloadUrl;

        GitHubRemoteFile(String path, long size, String downloadUrl) {
            this.path = path;
            this.size = size;
            this.downloadUrl = downloadUrl;
        }
    }

    private static final class LocalMirrorFile {
        final String path;
        final Uri uri;

        LocalMirrorFile(String path, Uri uri) {
            this.path = path;
            this.uri = uri;
        }
    }

    private static final class GitHubFetchResult {
        final ArrayList<FileSnapshot> updates;
        int changed;
        int unchanged;
        int deleted;
        int skipped;

        GitHubFetchResult(ArrayList<FileSnapshot> updates) {
            this.updates = updates;
        }
    }

    private boolean saveMirrorFileSync(String fileName, byte[] data) throws IOException {
        ContentResolver resolver = getContentResolver();
        String safePath = sanitizeTransferPath(fileName, "typewrt.txt");
        Uri existingUri = findExistingDownload(resolver, safePath);

        if (existingUri != null) {
            try {
                byte[] existingData = readUriBytes(existingUri, "Mirror file");
                if (bytesEqual(existingData, data)) {
                    mainHandler.post(() -> setLatestFile(safePath, data, existingUri));
                    return false;
                }
            } catch (IOException | RuntimeException ignored) {
                // If the local mirror is unreadable, overwrite it from GitHub.
            }
            overwriteMirrorFileSync(existingUri, data);
            mainHandler.post(() -> setLatestFile(safePath, data, existingUri));
            return true;
        }

        Uri uri = writeNewMirrorFileSync(safePath, data);
        mainHandler.post(() -> setLatestFile(safePath, data, uri));
        return true;
    }

    private Uri writeNewMirrorFileSync(String fileName, byte[] data) throws IOException {
        ContentResolver resolver = getContentResolver();
        String displayName = transferDisplayName(fileName);
        String relativeDir = transferRelativeDir(fileName);
        ContentValues values = new ContentValues();
        values.put(MediaStore.Downloads.DISPLAY_NAME, displayName);
        values.put(MediaStore.Downloads.MIME_TYPE, guessMimeType(displayName));
        values.put(MediaStore.Downloads.RELATIVE_PATH, relativeDir);
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
            return uri;
        } catch (IOException | RuntimeException e) {
            if (uri != null) {
                resolver.delete(uri, null, null);
            }
            if (e instanceof IOException) {
                throw (IOException) e;
            }
            throw new IOException("Could not save mirror file: " + usefulMessage(e), e);
        }
    }

    private void overwriteMirrorFileSync(Uri uri, byte[] data) throws IOException {
        try (OutputStream out = getContentResolver().openOutputStream(uri, "wt")) {
            if (out == null) {
                throw new IOException("Could not open output stream");
            }
            out.write(data);
        } catch (RuntimeException e) {
            throw new IOException("Could not overwrite mirror file: " + usefulMessage(e), e);
        }
    }

    private ArrayList<LocalMirrorFile> listLocalMirrorFiles() {
        ArrayList<LocalMirrorFile> files = new ArrayList<>();
        ContentResolver resolver = getContentResolver();
        String mirrorRoot = normalizeRelativePath(TYPEWRT_DOWNLOAD_DIR);
        String[] projection = {
            MediaStore.MediaColumns._ID,
            MediaStore.MediaColumns.DISPLAY_NAME,
            MediaStore.MediaColumns.RELATIVE_PATH,
        };

        try (Cursor cursor = resolver.query(
            MediaStore.Downloads.EXTERNAL_CONTENT_URI,
            projection,
            null,
            null,
            MediaStore.MediaColumns.DISPLAY_NAME + " ASC")) {
            if (cursor == null) {
                return files;
            }

            int idIndex = cursor.getColumnIndex(MediaStore.MediaColumns._ID);
            int nameIndex = cursor.getColumnIndex(MediaStore.MediaColumns.DISPLAY_NAME);
            int pathIndex = cursor.getColumnIndex(MediaStore.MediaColumns.RELATIVE_PATH);
            if (idIndex < 0 || nameIndex < 0 || pathIndex < 0) {
                return files;
            }

            while (cursor.moveToNext()) {
                String displayName = cursor.getString(nameIndex);
                String relativePath = normalizeRelativePath(cursor.getString(pathIndex));
                String subdir;

                if (relativePath.equals(mirrorRoot)) {
                    subdir = "";
                } else if (relativePath.startsWith(mirrorRoot + "/")) {
                    subdir = relativePath.substring(mirrorRoot.length() + 1);
                } else {
                    continue;
                }
                String localPath = subdir.isEmpty() ? displayName : subdir + "/" + displayName;
                String safePath = sanitizeTransferPath(localPath, "");
                if (safePath.isEmpty()) {
                    continue;
                }
                long id = cursor.getLong(idIndex);
                Uri uri = ContentUris.withAppendedId(MediaStore.Downloads.EXTERNAL_CONTENT_URI, id);
                files.add(new LocalMirrorFile(safePath, uri));
            }
        } catch (RuntimeException e) {
            mainHandler.post(() -> appendLog("Mirror listing failed: " + e));
        }
        return files;
    }

    private boolean deleteLocalMirrorFile(Uri uri) {
        try {
            return getContentResolver().delete(uri, null, null) > 0;
        } catch (RuntimeException e) {
            mainHandler.post(() -> appendLog("Mirror delete failed: " + e));
            return false;
        }
    }

    private void saveFile(String fileName, byte[] data) {
        ContentResolver resolver = getContentResolver();
        String safePath = sanitizeTransferPath(fileName, "typewrt.txt");
        Uri existingUri = findExistingDownload(resolver, safePath);

        if (existingUri != null) {
            try {
                byte[] existingData = readUriBytes(existingUri, "Existing file");
                if (bytesEqual(existingData, data)) {
                    mainHandler.post(() -> {
                        setLatestFile(safePath, data, existingUri);
                        setStatus("Already current " + safePath, "Downloads/Typewrt");
                        appendLog("Already current: " + existingUri);
                        setIdleButtons();
                    });
                    return;
                }
                mainHandler.post(() -> askOverwrite(safePath, data, existingUri, existingData));
                return;
            } catch (IOException | RuntimeException e) {
                mainHandler.post(() -> askOverwrite(safePath, data, existingUri, null));
                return;
            }
        }
        writeNewFile(safePath, data);
    }

    private void writeNewFile(String fileName, byte[] data) {
        ContentResolver resolver = getContentResolver();
        String displayName = transferDisplayName(fileName);
        String relativeDir = transferRelativeDir(fileName);
        ContentValues values = new ContentValues();
        values.put(MediaStore.Downloads.DISPLAY_NAME, displayName);
        values.put(MediaStore.Downloads.MIME_TYPE, guessMimeType(displayName));
        values.put(MediaStore.Downloads.RELATIVE_PATH, relativeDir);
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
                setStatus("Saved " + fileName, relativeDir);
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

    private void askOverwrite(
        String fileName,
        byte[] data,
        Uri existingUri,
        byte[] existingData
    ) {
        new AlertDialog.Builder(this)
            .setTitle("Overwrite " + fileName + "?")
            .setMessage("A different file already exists in Downloads/Typewrt.")
            .setNegativeButton("No", (dialog, which) -> {
                if (existingData != null) {
                    setLatestFile(fileName, existingData, existingUri);
                }
                setStatus("Kept existing " + fileName, "Received copy was not saved.");
                appendLog("Overwrite skipped: " + fileName);
                setIdleButtons();
            })
            .setPositiveButton("Yes", (dialog, which) -> {
                Thread thread = new Thread(
                    () -> overwriteFile(fileName, data, existingUri),
                    "typewrt-overwrite");
                thread.start();
            })
            .show();
    }

    private void overwriteFile(String fileName, byte[] data, Uri uri) {
        try (OutputStream out = getContentResolver().openOutputStream(uri, "wt")) {
            if (out == null) {
                throw new IOException("Could not open output stream");
            }
            out.write(data);
            mainHandler.post(() -> {
                setLatestFile(fileName, data, uri);
                setStatus("Overwrote " + fileName, "Downloads/Typewrt");
                appendLog("Overwrote: " + uri);
                setIdleButtons();
            });
        } catch (IOException | RuntimeException e) {
            mainHandler.post(() -> {
                setStatus("Overwrite failed", e.getMessage() == null ? e.toString() : e.getMessage());
                appendLog("Overwrite failed: " + e);
                setIdleButtons();
                updateFileActions();
            });
        }
    }

    private Uri findExistingDownload(ContentResolver resolver, String fileName) {
        String displayName = transferDisplayName(fileName);
        String relativeDir = transferRelativeDir(fileName);
        String[] projection = {
            MediaStore.MediaColumns._ID,
            MediaStore.MediaColumns.RELATIVE_PATH,
        };
        String selection = MediaStore.MediaColumns.DISPLAY_NAME + " = ?";

        try (Cursor cursor = resolver.query(
            MediaStore.Downloads.EXTERNAL_CONTENT_URI,
            projection,
            selection,
            new String[] {displayName},
            MediaStore.MediaColumns.DATE_MODIFIED + " DESC")) {
            if (cursor == null) {
                return null;
            }
            int idIndex = cursor.getColumnIndex(MediaStore.MediaColumns._ID);
            int pathIndex = cursor.getColumnIndex(MediaStore.MediaColumns.RELATIVE_PATH);
            if (idIndex < 0) {
                return null;
            }
            while (cursor.moveToNext()) {
                String relativePath = pathIndex >= 0 ? cursor.getString(pathIndex) : "";
                if (!sameRelativePath(relativePath, relativeDir)) {
                    continue;
                }
                long id = cursor.getLong(idIndex);
                return ContentUris.withAppendedId(MediaStore.Downloads.EXTERNAL_CONTENT_URI, id);
            }
        } catch (RuntimeException e) {
            mainHandler.post(() -> appendLog("Existing file lookup failed: " + e));
        }
        return null;
    }

    private boolean sameRelativePath(String left, String right) {
        return normalizeRelativePath(left).equals(normalizeRelativePath(right));
    }

    private String normalizeRelativePath(String path) {
        String clean = path == null ? "" : path.trim().replace('\\', '/');

        while (clean.startsWith("/")) {
            clean = clean.substring(1);
        }
        while (clean.endsWith("/")) {
            clean = clean.substring(0, clean.length() - 1);
        }
        return clean;
    }

    private boolean bytesEqual(byte[] left, byte[] right) {
        if (left == right) {
            return true;
        }
        if (left == null || right == null || left.length != right.length) {
            return false;
        }
        for (int i = 0; i < left.length; i++) {
            if (left[i] != right[i]) {
                return false;
            }
        }
        return true;
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
