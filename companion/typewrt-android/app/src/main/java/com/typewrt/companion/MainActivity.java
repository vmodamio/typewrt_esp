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
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.graphics.drawable.StateListDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.text.Editable;
import android.text.InputType;
import android.text.Spannable;
import android.text.SpannableString;
import android.text.TextWatcher;
import android.text.style.ForegroundColorSpan;
import android.text.style.StyleSpan;
import android.view.Gravity;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import android.util.Base64;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.ProtocolException;
import java.net.SocketTimeoutException;
import java.net.URL;
import java.net.URLEncoder;
import java.lang.reflect.Field;
import java.security.GeneralSecurityException;
import java.security.KeyStore;
import java.security.MessageDigest;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.UUID;

import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

public class MainActivity extends Activity {
    private static final int REQUEST_BLE_PERMISSIONS = 7;
    private static final int REQUEST_COPY_OUTPUT = 8;
    private static final long SCAN_TIMEOUT_MS = 15000;
    private static final long MAX_FILE_BYTES = 20L * 1024L * 1024L;
    private static final int HTTP_TIMEOUT_MS = 30000;
    private static final int PANDOC_SERVER_CHECK_TIMEOUT_MS = 8000;
    private static final int PANDOC_DEFAULT_PORT = 3030;
    private static final int DEFAULT_BLE_WRITE_CHUNK = 20;
    private static final int REQUESTED_BLE_MTU = 247;
    private static final int MAX_BLE_WRITE_CHUNK = REQUESTED_BLE_MTU - 3;
    private static final long GITHUB_ACCESS_CHECK_DELAY_MS = 900;
    private static final long PANDOC_SERVER_CHECK_DELAY_MS = 900;
    private static final int COLOR_BACKGROUND = 0xFF21211F;
    private static final int COLOR_SURFACE = 0xFF2B2B29;
    private static final int COLOR_SURFACE_HIGH = 0xFF33383D;
    private static final int COLOR_FIELD = 0xFF272A29;
    private static final int COLOR_STROKE = 0xFF454A50;
    private static final int COLOR_TEXT_PRIMARY = 0xFFFFFFFF;
    private static final int COLOR_TEXT_SECONDARY = 0xFFC8D0D6;
    private static final int COLOR_TEXT_MUTED = 0xFF8E989F;
    private static final int COLOR_BLUE = 0xFFF7C84F;
    private static final int COLOR_BLUE_DARK = 0xFFC79B2E;
    private static final int COLOR_RIPPLE = 0x33F7C84F;
    private static final String GITHUB_API_VERSION = "2026-03-10";
    private static final String REMOTE_DIR_NAME = "remote";
    private static final String OUTPUT_DIR_NAME = "output";
    private static final String SYNC_MANIFEST_NAME = ".typewrt-sync.json";
    private static final String SETTINGS_NAME = "typewrt_companion_settings";
    private static final String SETTING_PANDOC_SERVER_URL = "pandoc_server_url";
    private static final String SETTING_GITHUB_REPO = "github_repo";
    private static final String SETTING_GITHUB_PATH = "github_path";
    private static final String SETTING_GITHUB_BRANCH = "github_branch";
    private static final String SETTING_GITHUB_COMMIT_MESSAGE = "github_commit_message";
    private static final String SETTING_GITHUB_TOKEN_IV = "github_token_iv";
    private static final String SETTING_GITHUB_TOKEN_DATA = "github_token_data";
    private static final String KEYSTORE_PROVIDER = "AndroidKeyStore";
    private static final String GITHUB_TOKEN_KEY_ALIAS = "typewrt_companion_github_token";
    private static final String TOKEN_CIPHER = "AES/GCM/NoPadding";
    private static final int TOKEN_GCM_TAG_BITS = 128;
    private static final String PANDOC_EPUB_TEMPLATE_B64 =
        "PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0iVVRGLTgiPz4KPCFET0NUWVBFIGh0bWw+CjxodG1sIHhtbG5zPSJodHRw" +
        "Oi8vd3d3LnczLm9yZy8xOTk5L3hodG1sIiB4bWxuczplcHViPSJodHRwOi8vd3d3LmlkcGYub3JnLzIwMDcvb3BzIiRpZihs" +
        "YW5nKSQgbGFuZz0iJGxhbmckIiB4bWw6bGFuZz0iJGxhbmckIiRlbmRpZiQ+CjxoZWFkPgogIDxtZXRhIGNoYXJzZXQ9InV0" +
        "Zi04IiAvPgogIDxtZXRhIG5hbWU9ImdlbmVyYXRvciIgY29udGVudD0icGFuZG9jIiAvPgogIDx0aXRsZT4kcGFnZXRpdGxl" +
        "JDwvdGl0bGU+CiAgPHN0eWxlPgokaWYoY3NsLWNzcykkCiAgICAkc3R5bGVzLmNpdGF0aW9ucy5odG1sKCkkCiRlbmRpZiQK" +
        "JGlmKGhpZ2hsaWdodGluZy1jc3MpJAogICAgLyogQ1NTIGZvciBzeW50YXggaGlnaGxpZ2h0aW5nICovCiAgICAkaGlnaGxp" +
        "Z2h0aW5nLWNzcyQKJGVuZGlmJAogIDwvc3R5bGU+CiRmb3IoY3NzKSQKICA8bGluayByZWw9InN0eWxlc2hlZXQiIHR5cGU9" +
        "InRleHQvY3NzIiBocmVmPSIkY3NzJCIgLz4KJGVuZGZvciQKJGZvcihoZWFkZXItaW5jbHVkZXMpJAogICRoZWFkZXItaW5j" +
        "bHVkZXMkCiRlbmRmb3IkCjwvaGVhZD4KPGJvZHkkaWYoY292ZXJwYWdlKSQgaWQ9ImNvdmVyIiRlbmRpZiQkaWYoYm9keS10" +
        "eXBlKSQgZXB1Yjp0eXBlPSIkYm9keS10eXBlJCIkZW5kaWYkPgokaWYodGl0bGVwYWdlKSQKPHNlY3Rpb24gZXB1Yjp0eXBl" +
        "PSJ0aXRsZXBhZ2UiIGNsYXNzPSJ0aXRsZXBhZ2UiPgokZm9yKHRpdGxlKSQKJGlmKHRpdGxlLnR5cGUpJAogIDxoMSBjbGFz" +
        "cz0iJHRpdGxlLnR5cGUkIj4kdGl0bGUudGV4dCQ8L2gxPgokZWxzZSQKICA8aDEgY2xhc3M9InRpdGxlIj4kdGl0bGUkPC9o" +
        "MT4KJGVuZGlmJAokZW5kZm9yJAokaWYoc3VidGl0bGUpJAogIDxwIGNsYXNzPSJzdWJ0aXRsZSI+JHN1YnRpdGxlJDwvcD4K" +
        "JGVuZGlmJAokZm9yKGF1dGhvcikkCiAgPHAgY2xhc3M9ImF1dGhvciI+JGF1dGhvciQ8L3A+CiRlbmRmb3IkCiRmb3IoY3Jl" +
        "YXRvcikkCiAgPHAgY2xhc3M9IiRjcmVhdG9yLnJvbGUkIj4kY3JlYXRvci50ZXh0JDwvcD4KJGVuZGZvciQKJGlmKHB1Ymxp" +
        "c2hlcikkCiAgPHAgY2xhc3M9InB1Ymxpc2hlciI+JHB1Ymxpc2hlciQ8L3A+CiRlbmRpZiQKJGlmKGRhdGUpJAogIDxwIGNs" +
        "YXNzPSJkYXRlIj4kZGF0ZSQ8L3A+CiRlbmRpZiQKJGlmKHJpZ2h0cykkCiAgPGRpdiBjbGFzcz0icmlnaHRzIj4kcmlnaHRz" +
        "JDwvZGl2PgokZW5kaWYkCiRpZihhYnN0cmFjdCkkCjxkaXYgY2xhc3M9ImFic3RyYWN0Ij4KPGRpdiBjbGFzcz0iYWJzdHJh" +
        "Y3QtdGl0bGUiPiRhYnN0cmFjdC10aXRsZSQ8L2Rpdj4KJGFic3RyYWN0JAo8L2Rpdj4KJGVuZGlmJAo8L3NlY3Rpb24+CiRl" +
        "bHNlJAokaWYoY292ZXJwYWdlKSQKPGRpdiBpZD0iY292ZXItaW1hZ2UiPgo8c3ZnIHhtbG5zPSJodHRwOi8vd3d3LnczLm9y" +
        "Zy8yMDAwL3N2ZyIgeG1sbnM6eGxpbms9Imh0dHA6Ly93d3cudzMub3JnLzE5OTkveGxpbmsiIHZlcnNpb249IjEuMSIgd2lk" +
        "dGg9IjEwMCUiIGhlaWdodD0iMTAwJSIgdmlld0JveD0iMCAwICRjb3Zlci1pbWFnZS13aWR0aCQgJGNvdmVyLWltYWdlLWhl" +
        "aWdodCQiIHByZXNlcnZlQXNwZWN0UmF0aW89InhNaWRZTWlkIj4KPGltYWdlIHdpZHRoPSIkY292ZXItaW1hZ2Utd2lkdGgk" +
        "IiBoZWlnaHQ9IiRjb3Zlci1pbWFnZS1oZWlnaHQkIiB4bGluazpocmVmPSIuLi9tZWRpYS8kY292ZXItaW1hZ2UkIiAvPgo8" +
        "L3N2Zz4KPC9kaXY+CiRlbHNlJAokZm9yKGluY2x1ZGUtYmVmb3JlKSQKJGluY2x1ZGUtYmVmb3JlJAokZW5kZm9yJAokYm9k" +
        "eSQKJGZvcihpbmNsdWRlLWFmdGVyKSQKJGluY2x1ZGUtYWZ0ZXIkCiRlbmRmb3IkCiRlbmRpZiQKJGVuZGlmJAo8L2JvZHk+" +
        "CjwvaHRtbD4KCg==";
    private static final String PANDOC_STYLES_CITATIONS_HTML_B64 =
        "LyogQ1NTIGZvciBjaXRhdGlvbnMgKi8KZGl2LmNzbC1iaWItYm9keSB7IH0KZGl2LmNzbC1lbnRyeSB7CiAgY2xlYXI6IGJv" +
        "dGg7CiRpZihjc2wtZW50cnktc3BhY2luZykkCiAgbWFyZ2luLWJvdHRvbTogJGNzbC1lbnRyeS1zcGFjaW5nJDsKJGVuZGlm" +
        "JAp9Ci5oYW5naW5nLWluZGVudCBkaXYuY3NsLWVudHJ5IHsKICBtYXJnaW4tbGVmdDoyZW07CiAgdGV4dC1pbmRlbnQ6LTJl" +
        "bTsKfQpkaXYuY3NsLWxlZnQtbWFyZ2luIHsKICBtaW4td2lkdGg6MmVtOwogIGZsb2F0OmxlZnQ7Cn0KZGl2LmNzbC1yaWdo" +
        "dC1pbmxpbmUgewogIG1hcmdpbi1sZWZ0OjJlbTsKICBwYWRkaW5nLWxlZnQ6MWVtOwp9CmRpdi5jc2wtaW5kZW50IHsKICBt" +
        "YXJnaW4tbGVmdDogMmVtOwp9Cg==";
    private static final String PANDOC_ABBREVIATIONS_B64 =
        "YWV0LgphZXRhdC4KYWwuCkFwci4KQXVnLgpiay4KQnJvcy4KYy4KQ2FwdC4KY2YuCmNoLgpjaGFwLgpjaHMuCkNvLgpjb2wu" +
        "CkNvcnAuCmNwLgpkLgpEZWMuCkRyLgplLmcuCmVkLgplZHMuCmVzcC4KZi4KZmFzYy4KRmViLgpmZi4KZmlnLgpmbC4KZm9s" +
        "Lgpmb2xzLgpGci4KR2VuLgpHb3YuCkhvbi4KaS5lLgppbGwuCkluYy4KaW5jbC4KSmFuLgpKci4KSnVsLgpKdW4uCkx0ZC4K" +
        "TS5BLgpNLkQuCk1hci4KTXIuCk1ycy4KTXMuCm4uCm4uYi4Kbm4uCk5vLgpOb3YuCk9jdC4KcC4KUGguRC4KcHAuClByZXMu" +
        "ClByb2YuCnB0LgpxLnYuClJlcC4KUmV2LgpzLnYuCnMudnYuCnNhZWMuCnNlYy4KU2VuLgpTZXAuClNlcHQuClNndC4KU3Iu" +
        "ClN0Lgp1bml2Lgp2aXouCnZvbC4KdnMuCg==";
    private static final String PANDOC_EPUB_CSS_B64 =
        "LyogVGhpcyBkZWZpbmVzIHN0eWxlcyBhbmQgY2xhc3NlcyB1c2VkIGluIHRoZSBib29rICovCkBwYWdlIHsKICBtYXJnaW46" +
        "IDEwcHg7Cn0KaHRtbCwgYm9keSwgZGl2LCBzcGFuLCBhcHBsZXQsIG9iamVjdCwgaWZyYW1lLCBoMSwgaDIsIGgzLCBoNCwg" +
        "aDUsIGg2LCBwLApibG9ja3F1b3RlLCBwcmUsIGEsIGFiYnIsIGFjcm9ueW0sIGFkZHJlc3MsIGJpZywgY2l0ZSwgY29kZSwg" +
        "ZGVsLCBkZm4sIGVtLCBpbWcsCmlucywga2JkLCBxLCBzLCBzYW1wLCBzbWFsbCwgc3RyaWtlLCBzdHJvbmcsIHN1Yiwgc3Vw" +
        "LCB0dCwgdmFyLCBiLCB1LCBpLCBjZW50ZXIsCmZpZWxkc2V0LCBmb3JtLCBsYWJlbCwgbGVnZW5kLCB0YWJsZSwgY2FwdGlv" +
        "biwgdGJvZHksIHRmb290LCB0aGVhZCwgdHIsIHRoLCB0ZCwKYXJ0aWNsZSwgYXNpZGUsIGNhbnZhcywgZGV0YWlscywgZW1i" +
        "ZWQsIGZpZ3VyZSwgZmlnY2FwdGlvbiwgZm9vdGVyLCBoZWFkZXIsCmhncm91cCwgbWVudSwgbmF2LCBvdXRwdXQsIHJ1Ynks" +
        "IHNlY3Rpb24sIHN1bW1hcnksIHRpbWUsIG1hcmssIGF1ZGlvLCB2aWRlbywgb2wsCnVsLCBsaSwgZGwsIGR0LCBkZCB7CiAg" +
        "bWFyZ2luOiAwOwogIHBhZGRpbmc6IDA7CiAgYm9yZGVyOiAwOwogIGZvbnQtc2l6ZTogMTAwJTsKICB2ZXJ0aWNhbC1hbGln" +
        "bjogYmFzZWxpbmU7Cn0KaHRtbCB7CiAgbGluZS1oZWlnaHQ6IDEuMjsKICBmb250LWZhbWlseTogR2VvcmdpYSwgc2VyaWY7" +
        "CiAgY29sb3I6ICMxYTFhMWE7Cn0KcCB7CiAgdGV4dC1pbmRlbnQ6IDA7CiAgbWFyZ2luOiAxZW0gMDsKICB3aWRvd3M6IDI7" +
        "CiAgb3JwaGFuczogMjsKfQphLCBhOnZpc2l0ZWQgewogIGNvbG9yOiAjMWExYTFhOwp9CmltZyB7CiAgbWF4LXdpZHRoOiAx" +
        "MDAlOwp9CnN1cCB7CiAgdmVydGljYWwtYWxpZ246IHN1cGVyOwogIGZvbnQtc2l6ZTogc21hbGxlcjsKfQpzdWIgewogIHZl" +
        "cnRpY2FsLWFsaWduOiBzdWI7CiAgZm9udC1zaXplOiBzbWFsbGVyOwp9CmgxIHsKICBtYXJnaW46IDNlbSAwIDAgMDsKICBm" +
        "b250LXNpemU6IDJlbTsKICBwYWdlLWJyZWFrLWJlZm9yZTogYWx3YXlzOwogIGxpbmUtaGVpZ2h0OiAxNTAlOwp9CmgyIHsK" +
        "ICBtYXJnaW46IDEuNWVtIDAgMCAwOwogIGZvbnQtc2l6ZTogMS41ZW07CiAgbGluZS1oZWlnaHQ6IDEzNSU7Cn0KaDMgewog" +
        "IG1hcmdpbjogMS4zZW0gMCAwIDA7CiAgZm9udC1zaXplOiAxLjNlbTsKfQpoNCB7CiAgbWFyZ2luOiAxLjJlbSAwIDAgMDsK" +
        "ICBmb250LXNpemU6IDEuMmVtOwp9Cmg1IHsKICBtYXJnaW46IDEuMWVtIDAgMCAwOwogIGZvbnQtc2l6ZTogMS4xZW07Cn0K" +
        "aDYgewogIGZvbnQtc2l6ZTogMWVtOwp9CmgxLCBoMiwgaDMsIGg0LCBoNSwgaDYgewogIHRleHQtaW5kZW50OiAwOwogIHRl" +
        "eHQtYWxpZ246IGxlZnQ7CiAgZm9udC13ZWlnaHQ6IGJvbGQ7CiAgcGFnZS1icmVhay1hZnRlcjogYXZvaWQ7CiAgcGFnZS1i" +
        "cmVhay1pbnNpZGU6IGF2b2lkOwp9CgpvbCwgdWwgewogIG1hcmdpbjogMWVtIDAgMCAxLjdlbTsKfQpsaSA+IG9sLCBsaSA+" +
        "IHVsIHsKICBtYXJnaW4tdG9wOiAwOwp9CmJsb2NrcXVvdGUgewogIG1hcmdpbjogMWVtIDAgMWVtIDEuN2VtOwp9CmNvZGUg" +
        "ewogIGZvbnQtZmFtaWx5OiBNZW5sbywgTW9uYWNvLCAnTHVjaWRhIENvbnNvbGUnLCBDb25zb2xhcywgbW9ub3NwYWNlOwog" +
        "IGZvbnQtc2l6ZTogODUlOwogIG1hcmdpbjogMDsKICBoeXBoZW5zOiBtYW51YWw7Cn0KcHJlIHsKICBtYXJnaW46IDFlbSAw" +
        "OwogIG92ZXJmbG93OiBhdXRvOwp9CnByZSBjb2RlIHsKICBwYWRkaW5nOiAwOwogIG92ZXJmbG93OiB2aXNpYmxlOwogIG92" +
        "ZXJmbG93LXdyYXA6IG5vcm1hbDsKfQouc291cmNlQ29kZSB7CiAgYmFja2dyb3VuZC1jb2xvcjogdHJhbnNwYXJlbnQ7CiAg" +
        "b3ZlcmZsb3c6IHZpc2libGU7Cn0KaHIgewogIGJhY2tncm91bmQtY29sb3I6ICMxYTFhMWE7CiAgYm9yZGVyOiBub25lOwog" +
        "IGhlaWdodDogMXB4OwogIG1hcmdpbjogMWVtIDA7Cn0KdGFibGUgewogIG1hcmdpbjogMWVtIDA7CiAgYm9yZGVyLWNvbGxh" +
        "cHNlOiBjb2xsYXBzZTsKICB3aWR0aDogMTAwJTsKICBvdmVyZmxvdy14OiBhdXRvOwogIGRpc3BsYXk6IGJsb2NrOwp9CnRh" +
        "YmxlIGNhcHRpb24gewogIG1hcmdpbi1ib3R0b206IDAuNzVlbTsKfQp0Ym9keSB7CiAgbWFyZ2luLXRvcDogMC41ZW07CiAg" +
        "Ym9yZGVyLXRvcDogMXB4IHNvbGlkICMxYTFhMWE7CiAgYm9yZGVyLWJvdHRvbTogMXB4IHNvbGlkICMxYTFhMWE7Cn0KdGgs" +
        "IHRkIHsKICBwYWRkaW5nOiAwLjI1ZW0gMC41ZW0gMC4yNWVtIDAuNWVtOwp9CnRoIHsKICBib3JkZXItdG9wOiAxcHggc29s" +
        "aWQgIzFhMWExYTsKfQpoZWFkZXIgewogIG1hcmdpbi1ib3R0b206IDRlbTsKICB0ZXh0LWFsaWduOiBjZW50ZXI7Cn0KI1RP" +
        "QyBsaSB7CiAgbGlzdC1zdHlsZTogbm9uZTsKfQojVE9DIHVsIHsKICBwYWRkaW5nLWxlZnQ6IDEuM2VtOwp9CiNUT0MgPiB1" +
        "bCB7CiAgcGFkZGluZy1sZWZ0OiAwOwp9CiNUT0MgYTpub3QoOmhvdmVyKSB7CiAgdGV4dC1kZWNvcmF0aW9uOiBub25lOwp9" +
        "CmNvZGUgewogIHdoaXRlLXNwYWNlOiBwcmUtd3JhcDsKfQpzcGFuLnNtYWxsY2FwcyB7CiAgZm9udC12YXJpYW50OiBzbWFs" +
        "bC1jYXBzOwp9CgovKiBUaGlzIGlzIHRoZSBtb3N0IGNvbXBhdGlibGUgQ1NTLCBidXQgaXQgb25seSBhbGxvd3MgdHdvIGNv" +
        "bHVtbnM6ICovCmRpdi5jb2x1bW4gewogIGRpc3BsYXk6IGlubGluZS1ibG9jazsKICB2ZXJ0aWNhbC1hbGlnbjogdG9wOwog" +
        "IHdpZHRoOiA1MCU7Cn0KLyogSWYgeW91IGNhbiByZWx5IG9uIENTUzMgc3VwcG9ydCwgdXNlIHRoaXMgaW5zdGVhZDogKi8K" +
        "LyogZGl2LmNvbHVtbnMgewogIGRpc3BsYXk6IGZsZXg7CiAgZ2FwOiBtaW4oNHZ3LCAxLjVlbSk7Cn0KZGl2LmNvbHVtbiB7" +
        "CiAgZmxleDogYXV0bzsKICBvdmVyZmxvdy14OiBhdXRvOwp9ICovCgpkaXYuaGFuZ2luZy1pbmRlbnQgewogIG1hcmdpbi1s" +
        "ZWZ0OiAxLjVlbTsKICB0ZXh0LWluZGVudDogLTEuNWVtOwp9CnVsLnRhc2stbGlzdCB7CiAgbGlzdC1zdHlsZTogbm9uZTsK" +
        "fQp1bC50YXNrLWxpc3QgbGkgaW5wdXRbdHlwZT0iY2hlY2tib3giXSB7CiAgd2lkdGg6IDAuOGVtOwogIG1hcmdpbjogMCAw" +
        "LjhlbSAwLjJlbSAtMS42ZW07CiAgdmVydGljYWwtYWxpZ246IG1pZGRsZTsKfQouZGlzcGxheS5tYXRoIHsKICBkaXNwbGF5" +
        "OiBibG9jazsKICB0ZXh0LWFsaWduOiBjZW50ZXI7CiAgbWFyZ2luOiAwLjVyZW0gYXV0bzsKfQoKLyogRm9yIHRpdGxlLCBh" +
        "dXRob3IsIGFuZCBkYXRlIG9uIHRoZSBjb3ZlciBwYWdlICovCmgxLnRpdGxlIHsgfQpwLmF1dGhvciB7IH0KcC5kYXRlIHsg" +
        "fQoKbmF2I3RvYyBvbCwgbmF2I2xhbmRtYXJrcyBvbCB7CiAgcGFkZGluZzogMDsKICBtYXJnaW4tbGVmdDogMWVtOwp9Cm5h" +
        "diN0b2Mgb2wgbGksIG5hdiNsYW5kbWFya3Mgb2wgbGkgewogIGxpc3Qtc3R5bGUtdHlwZTogbm9uZTsKICBtYXJnaW46IDA7" +
        "CiAgcGFkZGluZzogMDsKfQphLmZvb3Rub3RlLXJlZiB7CiAgdmVydGljYWwtYWxpZ246IHN1cGVyOwp9CmVtLCBlbSBlbSBl" +
        "bSwgZW0gZW0gZW0gZW0gZW0gewogIGZvbnQtc3R5bGU6IGl0YWxpYzsKfQplbSBlbSwgZW0gZW0gZW0gZW0gewogIGZvbnQt" +
        "c3R5bGU6IG5vcm1hbDsKfQpxIHsKICBxdW90ZXM6ICLigJwiICLigJ0iICLigJgiICLigJkiOwp9CkBtZWRpYSBzY3JlZW4g" +
        "eyAvKiBXb3JrYXJvdW5kIGZvciBpQm9va3MgaXNzdWU7IHNlZSAjNjI0MiAqLwogIC5zb3VyY2VDb2RlIHsKICAgIG92ZXJm" +
        "bG93OiB2aXNpYmxlICFpbXBvcnRhbnQ7CiAgICB3aGl0ZS1zcGFjZTogcHJlLXdyYXAgIWltcG9ydGFudDsKICB9Cn0K";

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
    private TextView repositoryLabel;
    private LinearLayout remoteBrowserView;
    private LinearLayout pandocOutputView;
    private Button connectButton;
    private Button disconnectButton;
    private Button sendTypewrtButton;
    private Button clearUpdatesButton;
    private Button pandocExportButton;
    private Button pandocSelectButton;
    private Button pandocClearButton;
    private Button githubCommitButton;
    private Button githubFetchButton;
    private Button githubRestoreButton;
    private Button githubFileHistoryButton;
    private TextView githubLabel;
    private TextView githubAccessView;
    private LinearLayout githubConfigPanel;
    private EditText pandocServerField;
    private TextView pandocServerStatusView;
    private TextView pandocInputView;
    private EditText githubRepoField;
    private EditText githubPathField;
    private EditText githubBranchField;
    private EditText githubTokenField;
    private EditText githubCommitField;
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
    private int pandocServerCheckGeneration;
    private String remoteBrowserPath = "";
    private boolean githubConfigVisible;
    private boolean pandocServerReady;

    private ByteArrayOutputStream headerBuffer = new ByteArrayOutputStream();
    private ByteArrayOutputStream fileBuffer = new ByteArrayOutputStream();
    private long expectedSize = -1;
    private long receivedSize;
    private String currentFileName = "typewrt.txt";
    private final ArrayList<FileSnapshot> receivedFiles = new ArrayList<>();
    private final ArrayList<String> pandocInputPaths = new ArrayList<>();
    private String pendingOutputCopyPath;
    private byte[] latestFileData;
    private String latestFileName;
    private Uri latestFileUri;
    private final ArrayList<FileSnapshot> pendingSendFiles = new ArrayList<>();
    private boolean restorePending;
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
        buildUi();
        initializeBluetooth();
    }

    @Override
    protected void onDestroy() {
        disconnect();
        super.onDestroy();
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
        activeFileView.setText("Repository ready.");
        activeFileView.setTextSize(15);
        activeFileView.setTextColor(COLOR_TEXT_SECONDARY);
        LinearLayout.LayoutParams activeParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        activeParams.topMargin = dp(12);
        transferTabContent.addView(activeFileView, activeParams);

        repositoryLabel = sectionLabel("Repository");
        styleDisclosureLabel(repositoryLabel);
        repositoryLabel.setOnClickListener(v -> showRepositoryStoragePath());
        transferTabContent.addView(repositoryLabel);

        remoteBrowserView = new LinearLayout(this);
        remoteBrowserView.setOrientation(LinearLayout.VERTICAL);
        remoteBrowserView.setBackground(roundedDrawable(COLOR_SURFACE, dp(8), COLOR_STROKE, 1));
        remoteBrowserView.setPadding(dp(8), dp(8), dp(8), dp(8));
        LinearLayout.LayoutParams browserParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        browserParams.topMargin = dp(6);
        transferTabContent.addView(remoteBrowserView, browserParams);

        preparedFileView = new TextView(this);
        preparedFileView.setText("No updates queued.");
        preparedFileView.setTextSize(15);
        preparedFileView.setTextColor(COLOR_TEXT_SECONDARY);
        LinearLayout.LayoutParams preparedParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        preparedParams.topMargin = dp(6);
        transferTabContent.addView(preparedFileView, preparedParams);

        clearUpdatesButton = new Button(this);
        clearUpdatesButton.setText("Clear queue");
        clearUpdatesButton.setAllCaps(false);
        styleButton(clearUpdatesButton, false);
        clearUpdatesButton.setEnabled(false);
        clearUpdatesButton.setOnClickListener(v -> clearQueuedUpdates());
        LinearLayout.LayoutParams clearParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        clearParams.topMargin = dp(8);
        transferTabContent.addView(clearUpdatesButton, clearParams);

        disconnectButton = new Button(this);
        disconnectButton.setText("Disconnect");
        disconnectButton.setAllCaps(false);
        styleButton(disconnectButton, false);
        disconnectButton.setEnabled(false);
        disconnectButton.setOnClickListener(v -> disconnect());
        LinearLayout.LayoutParams disconnectParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        disconnectParams.topMargin = dp(8);
        transferTabContent.addView(disconnectButton, disconnectParams);

        TextView pandocLabel = sectionLabel("Pandoc export");
        pandocTabContent.addView(pandocLabel);

        pandocServerField = addTextField(
            pandocTabContent,
            "Pandoc server URL (http://host:3030/)",
            "");
        pandocServerField.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        pandocServerStatusView = new TextView(this);
        pandocServerStatusView.setText("Enter a pandoc-server URL to check server.");
        pandocServerStatusView.setTextSize(14);
        pandocServerStatusView.setTextColor(COLOR_TEXT_MUTED);
        LinearLayout.LayoutParams pandocStatusParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        pandocStatusParams.topMargin = dp(8);
        pandocTabContent.addView(pandocServerStatusView, pandocStatusParams);

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
            new String[] {"epub", "html", "docx", "plain", "gfm", "markdown", "rst", "latex"});

        pandocInputView = new TextView(this);
        pandocInputView.setTextSize(15);
        pandocInputView.setTextColor(COLOR_TEXT_SECONDARY);
        LinearLayout.LayoutParams pandocInputParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        pandocInputParams.topMargin = dp(10);
        pandocTabContent.addView(pandocInputView, pandocInputParams);

        LinearLayout pandocFileActions = new LinearLayout(this);
        pandocFileActions.setOrientation(LinearLayout.HORIZONTAL);
        pandocFileActions.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout.LayoutParams pandocFileActionParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        pandocFileActionParams.topMargin = dp(8);
        pandocTabContent.addView(pandocFileActions, pandocFileActionParams);

        pandocSelectButton = new Button(this);
        pandocSelectButton.setText("Select files");
        pandocSelectButton.setAllCaps(false);
        styleButton(pandocSelectButton, false);
        pandocSelectButton.setOnClickListener(v -> showPandocFilePicker());
        pandocFileActions.addView(pandocSelectButton, new LinearLayout.LayoutParams(
            0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        pandocClearButton = new Button(this);
        pandocClearButton.setText("Clear");
        pandocClearButton.setAllCaps(false);
        styleButton(pandocClearButton, false);
        pandocClearButton.setEnabled(false);
        pandocClearButton.setOnClickListener(v -> clearPandocSelection());
        LinearLayout.LayoutParams pandocClearParams = new LinearLayout.LayoutParams(
            0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
        pandocClearParams.leftMargin = dp(8);
        pandocFileActions.addView(pandocClearButton, pandocClearParams);

        pandocExportButton = new Button(this);
        pandocExportButton.setText("Export");
        pandocExportButton.setAllCaps(false);
        styleButton(pandocExportButton, true);
        pandocExportButton.setEnabled(false);
        pandocExportButton.setOnClickListener(v -> exportSelectedWithPandoc());
        LinearLayout.LayoutParams exportParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        exportParams.topMargin = dp(8);
        pandocTabContent.addView(pandocExportButton, exportParams);

        TextView pandocOutputLabel = sectionLabel("Pandoc outputs");
        styleDisclosureLabel(pandocOutputLabel);
        pandocOutputLabel.setOnClickListener(v -> refreshPandocOutputs());
        pandocTabContent.addView(pandocOutputLabel);

        pandocOutputView = new LinearLayout(this);
        pandocOutputView.setOrientation(LinearLayout.VERTICAL);
        pandocOutputView.setBackground(roundedDrawable(COLOR_SURFACE, dp(8), COLOR_STROKE, 1));
        pandocOutputView.setPadding(dp(8), dp(8), dp(8), dp(8));
        LinearLayout.LayoutParams outputParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        outputParams.topMargin = dp(6);
        pandocTabContent.addView(pandocOutputView, outputParams);
        updatePandocSelectionView();

        githubLabel = sectionLabel("GitHub repository");
        styleDisclosureLabel(githubLabel);
        githubLabel.setOnClickListener(v -> toggleGithubConfig());
        githubTabContent.addView(githubLabel);

        githubConfigPanel = new LinearLayout(this);
        githubConfigPanel.setOrientation(LinearLayout.VERTICAL);
        githubConfigPanel.setVisibility(View.GONE);
        githubTabContent.addView(githubConfigPanel, new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT));

        githubRepoField = addTextField(githubConfigPanel, "Repository owner/name", "");
        githubPathField = addTextField(githubConfigPanel, "Repository root/path (optional)", "");
        githubBranchField = addTextField(githubConfigPanel, "Branch", "main");
        githubTokenField = addTextField(githubConfigPanel, "GitHub token", "");
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
        githubConfigPanel.addView(githubAccessView, accessParams);

        githubFetchButton = new Button(this);
        githubFetchButton.setText("Pull");
        githubFetchButton.setAllCaps(false);
        styleButton(githubFetchButton, true);
        githubFetchButton.setEnabled(false);
        githubFetchButton.setOnClickListener(v -> fetchGithubRepository());
        LinearLayout.LayoutParams fetchParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        fetchParams.topMargin = dp(8);
        githubTabContent.addView(githubFetchButton, fetchParams);

        githubCommitField = addTextField(
            githubTabContent,
            "Commit message (optional)",
            "");

        githubCommitButton = new Button(this);
        githubCommitButton.setText("Commit changes");
        githubCommitButton.setAllCaps(false);
        styleButton(githubCommitButton, true);
        githubCommitButton.setEnabled(false);
        githubCommitButton.setOnClickListener(v -> commitGithubRepository());
        LinearLayout.LayoutParams commitParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        commitParams.topMargin = dp(8);
        githubTabContent.addView(githubCommitButton, commitParams);

        githubRestoreButton = new Button(this);
        githubRestoreButton.setText("Restore");
        githubRestoreButton.setAllCaps(false);
        styleButton(githubRestoreButton, false);
        githubRestoreButton.setEnabled(false);
        githubRestoreButton.setOnClickListener(v -> showGithubRestoreDialog());
        LinearLayout.LayoutParams restoreParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        restoreParams.topMargin = dp(8);
        githubTabContent.addView(githubRestoreButton, restoreParams);

        githubFileHistoryButton = new Button(this);
        githubFileHistoryButton.setText("File commits");
        githubFileHistoryButton.setAllCaps(false);
        styleButton(githubFileHistoryButton, false);
        githubFileHistoryButton.setEnabled(false);
        githubFileHistoryButton.setOnClickListener(v -> showGithubFileHistory());
        LinearLayout.LayoutParams historyParams = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT);
        historyParams.topMargin = dp(8);
        githubTabContent.addView(githubFileHistoryButton, historyParams);

        loadSavedSettings();
        installPandocServerWatcher();
        installGithubConfigWatchers();
        installGithubCommitWatcher();

        setContentView(page);
        showTab(CompanionTab.TRANSFER);
        refreshRemoteTree();
        refreshPandocOutputs();
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
        if (tab == CompanionTab.PANDOC) {
            refreshPandocOutputs();
        }
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

    private void styleDisclosureLabel(TextView label) {
        label.setMinHeight(dp(44));
        label.setGravity(Gravity.CENTER_VERTICAL);
        label.setPadding(dp(12), 0, dp(12), 0);
        label.setBackground(rippleBackground(COLOR_SURFACE_HIGH, COLOR_SURFACE_HIGH, dp(8)));
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
                saveGithubSettings();
                scheduleGithubAccessCheck();
            }
        };

        githubRepoField.addTextChangedListener(watcher);
        githubPathField.addTextChangedListener(watcher);
        githubBranchField.addTextChangedListener(watcher);
        githubTokenField.addTextChangedListener(watcher);
        scheduleGithubAccessCheck();
    }

    private void installGithubCommitWatcher() {
        githubCommitField.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {
            }

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {
            }

            @Override
            public void afterTextChanged(Editable s) {
                saveGithubCommitMessage();
            }
        });
    }

    private void installPandocServerWatcher() {
        TextWatcher watcher = new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {
            }

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {
            }

            @Override
            public void afterTextChanged(Editable s) {
                savePandocSettings();
                schedulePandocServerCheck();
            }
        };

        pandocServerField.addTextChangedListener(watcher);
        schedulePandocServerCheck();
    }

    private void loadSavedSettings() {
        SharedPreferences prefs = settings();

        pandocServerField.setText(prefs.getString(SETTING_PANDOC_SERVER_URL, ""));
        githubRepoField.setText(prefs.getString(SETTING_GITHUB_REPO, ""));
        githubPathField.setText(prefs.getString(SETTING_GITHUB_PATH, ""));
        githubBranchField.setText(prefs.getString(SETTING_GITHUB_BRANCH, "main"));
        githubCommitField.setText(prefs.getString(SETTING_GITHUB_COMMIT_MESSAGE, ""));

        try {
            githubTokenField.setText(decryptGithubToken(prefs));
        } catch (GeneralSecurityException | IOException | RuntimeException e) {
            clearSavedGithubToken(prefs);
            githubTokenField.setText("");
        }
    }

    private SharedPreferences settings() {
        return getSharedPreferences(SETTINGS_NAME, MODE_PRIVATE);
    }

    private void savePandocSettings() {
        settings().edit()
            .putString(SETTING_PANDOC_SERVER_URL, pandocServerField.getText().toString().trim())
            .apply();
    }

    private void saveGithubSettings() {
        SharedPreferences prefs = settings();
        SharedPreferences.Editor editor = prefs.edit()
            .putString(SETTING_GITHUB_REPO, githubRepoField.getText().toString().trim())
            .putString(SETTING_GITHUB_PATH, githubPathField.getText().toString().trim())
            .putString(SETTING_GITHUB_BRANCH, githubBranchField.getText().toString().trim());
        String token = githubTokenField.getText().toString().trim();

        if (token.isEmpty()) {
            editor.remove(SETTING_GITHUB_TOKEN_IV)
                .remove(SETTING_GITHUB_TOKEN_DATA)
                .apply();
            return;
        }

        try {
            EncryptedValue encrypted = encryptGithubToken(token);
            editor.putString(SETTING_GITHUB_TOKEN_IV, encrypted.iv)
                .putString(SETTING_GITHUB_TOKEN_DATA, encrypted.data)
                .apply();
        } catch (GeneralSecurityException | IOException | RuntimeException e) {
            editor.apply();
            setStatus("Token not saved", usefulMessage(e));
            appendLog("GitHub token save failed: " + e);
        }
    }

    private void saveGithubCommitMessage() {
        settings().edit()
            .putString(SETTING_GITHUB_COMMIT_MESSAGE, githubCommitField.getText().toString().trim())
            .apply();
    }

    private void clearSavedGithubToken(SharedPreferences prefs) {
        prefs.edit()
            .remove(SETTING_GITHUB_TOKEN_IV)
            .remove(SETTING_GITHUB_TOKEN_DATA)
            .apply();
    }

    private EncryptedValue encryptGithubToken(String token)
        throws GeneralSecurityException, IOException {
        Cipher cipher = Cipher.getInstance(TOKEN_CIPHER);

        cipher.init(Cipher.ENCRYPT_MODE, githubTokenKey());
        byte[] encrypted = cipher.doFinal(token.getBytes(StandardCharsets.UTF_8));
        return new EncryptedValue(
            Base64.encodeToString(cipher.getIV(), Base64.NO_WRAP),
            Base64.encodeToString(encrypted, Base64.NO_WRAP));
    }

    private String decryptGithubToken(SharedPreferences prefs)
        throws GeneralSecurityException, IOException {
        String iv = prefs.getString(SETTING_GITHUB_TOKEN_IV, "");
        String data = prefs.getString(SETTING_GITHUB_TOKEN_DATA, "");

        if (iv.isEmpty() || data.isEmpty()) {
            return "";
        }

        Cipher cipher = Cipher.getInstance(TOKEN_CIPHER);
        cipher.init(
            Cipher.DECRYPT_MODE,
            githubTokenKey(),
            new GCMParameterSpec(TOKEN_GCM_TAG_BITS, Base64.decode(iv, Base64.DEFAULT)));
        byte[] decrypted = cipher.doFinal(Base64.decode(data, Base64.DEFAULT));
        return new String(decrypted, StandardCharsets.UTF_8);
    }

    private SecretKey githubTokenKey() throws GeneralSecurityException, IOException {
        KeyStore keyStore = KeyStore.getInstance(KEYSTORE_PROVIDER);

        keyStore.load(null);
        KeyStore.Entry entry = keyStore.getEntry(GITHUB_TOKEN_KEY_ALIAS, null);
        if (entry instanceof KeyStore.SecretKeyEntry) {
            return ((KeyStore.SecretKeyEntry) entry).getSecretKey();
        }

        KeyGenerator generator = KeyGenerator.getInstance(
            KeyProperties.KEY_ALGORITHM_AES,
            KEYSTORE_PROVIDER);
        KeyGenParameterSpec spec = new KeyGenParameterSpec.Builder(
            GITHUB_TOKEN_KEY_ALIAS,
            KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
            .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
            .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
            .setRandomizedEncryptionRequired(true)
            .build();

        generator.init(spec);
        return generator.generateKey();
    }

    private void schedulePandocServerCheck() {
        int generation = ++pandocServerCheckGeneration;
        pandocServerReady = false;

        if (pandocServerStatusView == null || pandocServerField == null) {
            updatePandocSelectionView();
            return;
        }

        String serverUrl = pandocServerField.getText().toString().trim();
        if (serverUrl.isEmpty()) {
            pandocServerStatusView.setText("Enter a pandoc-server URL to check server.");
            pandocServerStatusView.setTextColor(COLOR_TEXT_MUTED);
            updatePandocSelectionView();
            return;
        }
        if (serverUrl.contains("pandoc.org/app")) {
            pandocServerStatusView.setText("Pandoc app is browser-only; use a pandoc-server URL.");
            pandocServerStatusView.setTextColor(COLOR_TEXT_SECONDARY);
            updatePandocSelectionView();
            return;
        }

        String normalizedServerUrl;
        try {
            normalizedServerUrl = normalizePandocServerUrl(serverUrl);
        } catch (IOException e) {
            String validationError = "Pandoc server URL is invalid: " + usefulMessage(e);
            pandocServerStatusView.setText(validationError);
            pandocServerStatusView.setTextColor(COLOR_TEXT_SECONDARY);
            updatePandocSelectionView();
            return;
        }

        pandocServerStatusView.setText("Pandoc server check pending...");
        pandocServerStatusView.setTextColor(COLOR_TEXT_SECONDARY);
        updatePandocSelectionView();
        mainHandler.postDelayed(() -> checkPandocServer(generation, normalizedServerUrl),
            PANDOC_SERVER_CHECK_DELAY_MS);
    }

    private String normalizePandocServerUrl(String serverUrl) throws IOException {
        String clean = serverUrl.trim();
        if (!clean.contains("://")) {
            clean = "http://" + clean;
        }
        URL url = new URL(clean);
        String protocol = url.getProtocol();

        if (!"http".equals(protocol) && !"https".equals(protocol)) {
            throw new IOException("Use an http:// or https:// pandoc-server URL.");
        }
        if (url.getHost() == null || url.getHost().isEmpty()) {
            throw new IOException("Pandoc server URL is missing a host.");
        }
        if (url.getPort() >= 0) {
            return url.toString();
        }

        return new URL(protocol, url.getHost(), PANDOC_DEFAULT_PORT, url.getFile())
            .toString();
    }

    private void checkPandocServer(int generation, String serverUrl) {
        if (generation != pandocServerCheckGeneration) {
            return;
        }
        if (pandocServerStatusView != null) {
            pandocServerStatusView.setText("Checking Pandoc server...");
        }

        Thread thread = new Thread(() -> {
            try {
                performPandocServerCheck(serverUrl);
                mainHandler.post(() -> {
                    if (generation != pandocServerCheckGeneration) {
                        return;
                    }
                    pandocServerReady = true;
                    pandocServerStatusView.setText("Pandoc server reachable.");
                    pandocServerStatusView.setTextColor(COLOR_BLUE);
                    updatePandocSelectionView();
                });
            } catch (IOException | JSONException | RuntimeException e) {
                mainHandler.post(() -> {
                    if (generation != pandocServerCheckGeneration) {
                        return;
                    }
                    pandocServerReady = false;
                    pandocServerStatusView.setText("Pandoc server check failed: " + usefulMessage(e));
                    pandocServerStatusView.setTextColor(COLOR_TEXT_SECONDARY);
                    updatePandocSelectionView();
                });
            }
        }, "typewrt-pandoc-check");
        thread.start();
    }

    private void performPandocServerCheck(String serverUrl) throws IOException, JSONException {
        HttpURLConnection connection = (HttpURLConnection)
            new URL(normalizePandocServerUrl(serverUrl)).openConnection();
        connection.setRequestMethod("POST");
        connection.setConnectTimeout(PANDOC_SERVER_CHECK_TIMEOUT_MS);
        connection.setReadTimeout(PANDOC_SERVER_CHECK_TIMEOUT_MS);
        connection.setDoOutput(true);
        connection.setRequestProperty("Accept", "application/octet-stream");
        connection.setRequestProperty("Content-Type", "application/json; charset=utf-8");
        connection.setRequestProperty("Connection", "close");
        connection.setUseCaches(false);

        JSONObject request = new JSONObject();
        request.put("text", "---\ntitle: Typewrt Pandoc Check\n---\n\n# Typewrt\n\nPandoc server check.\n");
        request.put("from", "markdown");
        request.put("to", "epub");
        request.put("standalone", true);
        addPandocBundledDefaults(request, "epub", true);

        byte[] body = request.toString().getBytes(StandardCharsets.UTF_8);
        connection.setFixedLengthStreamingMode(body.length);
        try (OutputStream out = connection.getOutputStream()) {
            out.write(body);
        }

        int status = connection.getResponseCode();
        byte[] response = readResponse(connection);
        if (status < 200 || status >= 300) {
            connection.disconnect();
            throw new IOException("HTTP " + status + ": " + preview(response));
        }
        connection.disconnect();
    }

    private static void addPandocBundledDefaults(
        JSONObject request,
        String to,
        boolean standalone
    ) throws JSONException {
        if (!standalone || !isPandocEpubOutput(to)) {
            return;
        }

        request.put("template", base64Utf8(PANDOC_EPUB_TEMPLATE_B64));

        JSONObject files = request.optJSONObject("files");
        if (files == null) {
            files = new JSONObject();
            request.put("files", files);
        }
        files.put("data/data/templates/styles.citations.html", PANDOC_STYLES_CITATIONS_HTML_B64);
        files.put("data/data/abbreviations", PANDOC_ABBREVIATIONS_B64);
        files.put("data/data/epub.css", PANDOC_EPUB_CSS_B64);
    }

    private static boolean isPandocEpubOutput(String to) {
        String format = to == null ? "" : to.toLowerCase(Locale.ROOT);
        return "epub".equals(format) || "epub2".equals(format) || "epub3".equals(format);
    }

    private static String base64Utf8(String text) {
        return new String(Base64.decode(text, Base64.NO_WRAP), StandardCharsets.UTF_8);
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
        if (githubCommitButton != null) {
            githubCommitButton.setEnabled(ready);
        }
        if (githubRestoreButton != null) {
            githubRestoreButton.setEnabled(ready);
        }
        if (githubFileHistoryButton != null) {
            githubFileHistoryButton.setEnabled(ready);
        }
    }

    private void toggleGithubConfig() {
        githubConfigVisible = !githubConfigVisible;
        if (githubConfigPanel != null) {
            githubConfigPanel.setVisibility(githubConfigVisible ? View.VISIBLE : View.GONE);
        }
    }

    private void showRepositoryStoragePath() {
        Thread thread = new Thread(() -> {
            try {
                String path = remoteDir().getAbsolutePath();
                mainHandler.post(() -> new AlertDialog.Builder(this)
                    .setTitle("Repository folder")
                    .setMessage(path)
                    .setPositiveButton("OK", null)
                    .show());
            } catch (IOException | RuntimeException e) {
                mainHandler.post(() ->
                    setStatus("Repository unavailable", usefulMessage(e)));
            }
        }, "typewrt-repository-path");
        thread.start();
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
        int normalColor = COLOR_BLUE;
        int disabledColor = primary ? 0xFF3A4D5E : 0xFF2A2D30;
        int textColor = 0xFF0B1720;
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
            setStatus("No updates queued", "Pull or restore from GitHub, or request a delete first.");
            return;
        }
        transferMode = TransferMode.SEND_TO_TYPEWRT;
        if (!hasRequiredPermissions()) {
            requestPermissions(requiredPermissions(), REQUEST_BLE_PERMISSIONS);
            return;
        }
        startScan();
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

        if (requestCode != REQUEST_COPY_OUTPUT) {
            return;
        }

        String sourcePath = pendingOutputCopyPath;
        pendingOutputCopyPath = null;
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            setStatus("Copy canceled", "No output was copied.");
            return;
        }
        if (sourcePath == null || sourcePath.isEmpty()) {
            setStatus("Copy failed", "Output selection was lost.");
            return;
        }
        copyPandocOutputToUri(sourcePath, data.getData());
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

    private void clearQueuedUpdates() {
        pendingSendFiles.clear();
        restorePending = false;
        setStatus("Cleared updates", "No updates queued.");
        appendLog("Cleared queued updates.");
        refreshRemoteTree();
        updateSendFileActions();
    }

    private void updateSendFileActions() {
        boolean hasPreparedFile = !pendingSendFiles.isEmpty();
        if (preparedFileView != null) {
            if (hasPreparedFile) {
                preparedFileView.setText(
                    (restorePending ? "Restore queued: " : "Queued: ") +
                        queueSummary() + " (" + pendingSendBytes() + " bytes)");
            } else {
                preparedFileView.setText("No updates queued.");
            }
        }
        if (sendTypewrtButton != null) {
            sendTypewrtButton.setEnabled(
                hasPreparedFile && bluetoothAdapter != null && activeGatt == null && !scanning);
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
        restorePending = false;
        mainHandler.post(() -> {
            setStatus("Sent " + label, "Applied in the Typewrt menu directory.");
            appendLog("Sent: " + label);
            refreshRemoteTree();
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
                    "Latest remote file: " + snapshot.name + " (" + snapshot.data.length + " bytes)");
            } else {
                activeFileView.setText("Repository ready.");
            }
        }
        updatePandocSelectionView();
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

    private void showPandocFilePicker() {
        setStatus("Loading repository files", "Choose Pandoc inputs in export order.");
        Thread thread = new Thread(() -> {
            try {
                File root = remoteDir().getCanonicalFile();
                mainHandler.post(() -> showPandocFilePickerDialog(root));
            } catch (IOException | RuntimeException e) {
                mainHandler.post(() -> {
                    setStatus("File picker failed", usefulMessage(e));
                    appendLog("Pandoc file picker failed: " + e);
                });
            }
        }, "typewrt-pandoc-file-picker");
        thread.start();
    }

    private void showPandocFilePickerDialog(File root) {
        ArrayList<String> selection = new ArrayList<>();
        String[] currentPath = new String[] {""};
        LinearLayout rows = new LinearLayout(this);
        rows.setOrientation(LinearLayout.VERTICAL);
        rows.setBackground(roundedDrawable(COLOR_SURFACE, dp(8), COLOR_STROKE, 1));
        rows.setPadding(dp(8), dp(8), dp(8), dp(8));

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(COLOR_BACKGROUND);
        scroll.addView(rows, new ScrollView.LayoutParams(
            ScrollView.LayoutParams.MATCH_PARENT,
            ScrollView.LayoutParams.WRAP_CONTENT));
        scroll.setLayoutParams(new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            dp(360)));

        try {
            HashSet<String> availablePaths = new HashSet<>();
            for (LocalMirrorFile file : listFolderFiles(root)) {
                availablePaths.add(file.path);
            }
            if (availablePaths.isEmpty()) {
                setStatus("No files found", "Pull or receive files into the repository first.");
                return;
            }
            for (String path : pandocInputPaths) {
                if (availablePaths.contains(path)) {
                    selection.add(path);
                }
            }
        } catch (IOException | RuntimeException e) {
            setStatus("File picker failed", usefulMessage(e));
            appendLog("Pandoc file picker failed: " + e);
            return;
        }

        Runnable[] render = new Runnable[1];
        render[0] = () -> renderPandocFilePickerRows(root, currentPath, selection, rows, render[0]);
        render[0].run();

        AlertDialog dialog = new AlertDialog.Builder(this)
            .setTitle("Pandoc inputs")
            .setView(scroll)
            .setPositiveButton("Use order", (d, which) -> {
                pandocInputPaths.clear();
                pandocInputPaths.addAll(selection);
                setStatus("Pandoc inputs selected", pandocSelectionSummary());
                updatePandocSelectionView();
            })
            .setNeutralButton("Clear", null)
            .setNegativeButton("Cancel", null)
            .create();
        dialog.setOnShowListener(d ->
            dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(v -> {
                selection.clear();
                render[0].run();
            }));
        dialog.show();
    }

    private void renderPandocFilePickerRows(
        File root,
        String[] currentPath,
        ArrayList<String> selection,
        LinearLayout rows,
        Runnable render
    ) {
        rows.removeAllViews();
        rows.addView(browserRow(
            "remote/" + (currentPath[0].isEmpty() ? "" : currentPath[0]),
            COLOR_BLUE,
            null));
        rows.addView(browserRow(
            "selected " + selection.size() + " input" + plural(selection.size()),
            selection.isEmpty() ? COLOR_TEXT_MUTED : COLOR_TEXT_SECONDARY,
            null));

        try {
            File current = currentPath[0].isEmpty() ?
                root : fileInside(root, currentPath[0]);

            if (!current.isDirectory()) {
                currentPath[0] = "";
                current = root;
            }
            if (!currentPath[0].isEmpty()) {
                rows.addView(browserRow("..", COLOR_TEXT_PRIMARY, () -> {
                    int slash = currentPath[0].lastIndexOf('/');
                    currentPath[0] = slash < 0 ? "" : currentPath[0].substring(0, slash);
                    render.run();
                }));
            }

            ArrayList<BrowserEntry> entries = listBrowserEntries(root, current);
            int visibleRows = 0;
            for (BrowserEntry entry : entries) {
                String label;
                int color = COLOR_TEXT_PRIMARY;

                if (entry.directory) {
                    label = "[+] " + entry.name;
                } else {
                    int selectedIndex = selection.indexOf(entry.path);
                    boolean selected = selectedIndex >= 0;
                    label = (selected ? "[x] " : "[ ] ") + entry.name +
                        "  " + entry.file.length() + " b";
                    if (selected) {
                        label += "  #" + (selectedIndex + 1);
                        color = COLOR_BLUE;
                    }
                }

                rows.addView(browserRow(label, color, () -> {
                    if (entry.directory) {
                        currentPath[0] = entry.path;
                    } else if (selection.contains(entry.path)) {
                        selection.remove(entry.path);
                    } else {
                        selection.add(entry.path);
                    }
                    render.run();
                }));
                visibleRows++;
            }
            if (visibleRows == 0) {
                rows.addView(browserRow("empty", COLOR_TEXT_MUTED, null));
            }
        } catch (IOException | RuntimeException e) {
            rows.addView(browserRow(usefulMessage(e), COLOR_TEXT_SECONDARY, null));
        }
    }

    private void clearPandocSelection() {
        pandocInputPaths.clear();
        setStatus("Pandoc inputs cleared", "Select files to export.");
        updatePandocSelectionView();
    }

    private void refreshPandocOutputs() {
        if (pandocOutputView == null) {
            return;
        }
        Thread thread = new Thread(() -> {
            ArrayList<LocalMirrorFile> files = new ArrayList<>();
            String error = null;

            try {
                files = listFolderFiles(outputDir());
            } catch (IOException | RuntimeException e) {
                error = usefulMessage(e);
            }

            ArrayList<LocalMirrorFile> finalFiles = files;
            String finalError = error;
            mainHandler.post(() -> renderPandocOutputs(finalFiles, finalError));
        }, "typewrt-pandoc-output-list");
        thread.start();
    }

    private void renderPandocOutputs(List<LocalMirrorFile> files, String error) {
        pandocOutputView.removeAllViews();
        pandocOutputView.addView(browserRow("output/", COLOR_BLUE, null));

        if (error != null) {
            pandocOutputView.addView(browserRow(error, COLOR_TEXT_SECONDARY, null));
            return;
        }
        if (files.isEmpty()) {
            pandocOutputView.addView(browserRow("empty", COLOR_TEXT_MUTED, null));
            return;
        }

        for (LocalMirrorFile output : files) {
            String label = output.path + "  " + output.file.length() + " b";
            pandocOutputView.addView(browserRow(label, COLOR_TEXT_PRIMARY, () ->
                showPandocOutputActions(output.path)));
        }
    }

    private void showPandocOutputActions(String path) {
        String safePath = sanitizeTransferPath(path, "");

        if (safePath.isEmpty()) {
            setStatus("Output path missing", "No Pandoc output selected.");
            return;
        }

        new AlertDialog.Builder(this)
            .setTitle(fileNameFromPath(safePath))
            .setItems(new String[] {"Open", "Share", "Copy", "Delete"}, (dialog, which) -> {
                if (which == 0) {
                    openPandocOutput(safePath);
                } else if (which == 1) {
                    sharePandocOutput(safePath);
                } else if (which == 2) {
                    choosePandocOutputCopyTarget(safePath);
                } else {
                    confirmDeletePandocOutput(safePath);
                }
            })
            .show();
    }

    private void openPandocOutput(String path) {
        String safePath = sanitizeTransferPath(path, "");

        if (safePath.isEmpty()) {
            setStatus("Output path missing", "No Pandoc output selected.");
            return;
        }

        Uri uri = outputContentUri(safePath);
        Intent intent = new Intent(Intent.ACTION_VIEW);
        intent.setDataAndType(uri, guessMimeType(safePath));
        intent.setClipData(ClipData.newUri(getContentResolver(), fileNameFromPath(safePath), uri));
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);

        try {
            startActivity(Intent.createChooser(intent, "Open " + fileNameFromPath(safePath)));
        } catch (ActivityNotFoundException e) {
            setStatus("No app found", "Install an app that can open " + extensionForPath(safePath) + " files.");
        }
    }

    private void sharePandocOutput(String path) {
        String safePath = sanitizeTransferPath(path, "");

        if (safePath.isEmpty()) {
            setStatus("Output path missing", "No Pandoc output selected.");
            return;
        }

        Uri uri = outputContentUri(safePath);
        Intent intent = new Intent(Intent.ACTION_SEND);
        intent.setType(guessMimeType(safePath));
        intent.putExtra(Intent.EXTRA_STREAM, uri);
        intent.putExtra(Intent.EXTRA_SUBJECT, fileNameFromPath(safePath));
        intent.setClipData(ClipData.newUri(getContentResolver(), fileNameFromPath(safePath), uri));
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);

        try {
            startActivity(Intent.createChooser(intent, "Share " + fileNameFromPath(safePath)));
        } catch (ActivityNotFoundException e) {
            setStatus("No share target", "No app can share " + extensionForPath(safePath) + " files.");
        }
    }

    private void choosePandocOutputCopyTarget(String path) {
        String safePath = sanitizeTransferPath(path, "");

        if (safePath.isEmpty()) {
            setStatus("Output path missing", "No Pandoc output selected.");
            return;
        }

        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType(guessMimeType(safePath));
        intent.putExtra(Intent.EXTRA_TITLE, fileNameFromPath(safePath));
        pendingOutputCopyPath = safePath;

        try {
            startActivityForResult(intent, REQUEST_COPY_OUTPUT);
        } catch (ActivityNotFoundException e) {
            pendingOutputCopyPath = null;
            setStatus("No file picker", "No app can choose where to copy this output.");
        }
    }

    private void confirmDeletePandocOutput(String path) {
        String safePath = sanitizeTransferPath(path, "");

        if (safePath.isEmpty()) {
            setStatus("Output path missing", "No Pandoc output selected.");
            return;
        }

        new AlertDialog.Builder(this)
            .setTitle("Delete output?")
            .setMessage(safePath)
            .setPositiveButton("Delete", (dialog, which) -> deletePandocOutput(safePath))
            .setNegativeButton("Cancel", null)
            .show();
    }

    private void deletePandocOutput(String path) {
        String safePath = sanitizeTransferPath(path, "");

        try {
            File file = outputFile(safePath);
            boolean deleted = deleteRecursive(file);
            pruneEmptyParents(file.getParentFile(), outputDir());
            if (deleted) {
                setStatus("Deleted output", safePath);
                appendLog("Output deleted: " + safePath);
            } else {
                setStatus("Delete failed", safePath + " was not found.");
            }
        } catch (IOException | RuntimeException e) {
            setStatus("Delete failed", usefulMessage(e));
            appendLog("Output delete failed: " + e);
        }
        refreshPandocOutputs();
    }

    private void copyPandocOutputToUri(String path, Uri destination) {
        String safePath = sanitizeTransferPath(path, "");

        try (InputStream in = new FileInputStream(outputFile(safePath));
             OutputStream out = getContentResolver().openOutputStream(destination)) {
            if (out == null) {
                throw new IOException("Copy destination is not writable.");
            }
            copyStream(in, out);
            setStatus("Copied output", fileNameFromPath(safePath));
            appendLog("Output copied: " + safePath);
        } catch (IOException | RuntimeException e) {
            setStatus("Copy failed", usefulMessage(e));
            appendLog("Output copy failed: " + e);
        }
    }

    private Uri outputContentUri(String path) {
        Uri.Builder builder = new Uri.Builder()
            .scheme("content")
            .authority(getPackageName() + ".output")
            .appendPath(OUTPUT_DIR_NAME);
        String[] parts = sanitizeTransferPath(path, "").split("/");

        for (String part : parts) {
            if (!part.isEmpty()) {
                builder.appendPath(part);
            }
        }
        return builder.build();
    }

    private void updatePandocSelectionView() {
        boolean hasSelection = !pandocInputPaths.isEmpty();

        if (pandocInputView != null) {
            pandocInputView.setText(hasSelection ?
                "Inputs:\n" + numberedPaths(pandocInputPaths) :
                "No Pandoc inputs selected.");
        }
        if (pandocExportButton != null) {
            pandocExportButton.setEnabled(hasSelection && pandocServerReady);
        }
        if (pandocClearButton != null) {
            pandocClearButton.setEnabled(hasSelection);
        }
    }

    private String numberedPaths(List<String> paths) {
        StringBuilder out = new StringBuilder();

        for (int i = 0; i < paths.size(); i++) {
            if (i > 0) {
                out.append('\n');
            }
            out.append(i + 1).append(". ").append(paths.get(i));
            if (isYamlFile(paths.get(i))) {
                out.append(" (metadata)");
            }
        }
        return out.toString();
    }

    private String pandocSelectionSummary() {
        int bodyFiles = 0;
        int metadataFiles = 0;

        for (String path : pandocInputPaths) {
            if (isYamlFile(path)) {
                metadataFiles++;
            } else {
                bodyFiles++;
            }
        }
        return bodyFiles + " input" + plural(bodyFiles) +
            (metadataFiles > 0 ? ", " + metadataFiles + " metadata" : "");
    }

    private void exportSelectedWithPandoc() {
        if (pandocInputPaths.isEmpty()) {
            setStatus("Nothing to export", "Select one or more repository files first.");
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
        try {
            serverUrl = normalizePandocServerUrl(serverUrl);
        } catch (IOException e) {
            setStatus("Pandoc server URL invalid", usefulMessage(e));
            return;
        }
        if (!pandocServerReady) {
            setStatus("Pandoc server not ready", "Wait for the server check to pass.");
            schedulePandocServerCheck();
            return;
        }

        String from = selectedString(pandocFromSpinner);
        String to = selectedString(pandocToSpinner);
        final String exportServerUrl = serverUrl;
        pandocExportButton.setEnabled(false);
        setStatus("Exporting with Pandoc", pandocSelectionSummary());
        appendLog("Pandoc export: " + from + " -> " + to);

        ArrayList<String> selectedPaths = new ArrayList<>(pandocInputPaths);
        Thread thread = new Thread(() -> {
            try {
                PandocInput input = buildPandocInput(selectedPaths);
                String outputName = convertedFileName(input.outputBasePath, to);
                byte[] output = postPandoc(exportServerUrl, input.text, from, to);
                saveOutputFile(outputName, output);
            } catch (IOException | JSONException e) {
                mainHandler.post(() -> {
                    setStatus("Pandoc export failed", usefulMessage(e));
                    appendLog("Pandoc failed: " + e);
                    updatePandocSelectionView();
                });
            }
        }, "typewrt-pandoc");
        thread.start();
    }

    private PandocInput buildPandocInput(List<String> paths) throws IOException {
        StringBuilder metadata = new StringBuilder();
        StringBuilder body = new StringBuilder();
        String outputBasePath = null;

        for (String path : paths) {
            String safePath = sanitizeTransferPath(path, "");
            if (safePath.isEmpty()) {
                continue;
            }
            File file = remoteFile(safePath);
            String text = new String(readFileBytes(file), StandardCharsets.UTF_8);

            if (isYamlFile(safePath)) {
                String yaml = normalizeYamlMetadata(text);
                if (!yaml.isEmpty()) {
                    metadata.append(yaml).append('\n');
                }
                continue;
            }

            if (outputBasePath == null) {
                outputBasePath = safePath;
            }
            if (body.length() > 0) {
                body.append("\n\n");
            }
            body.append(text);
        }

        if (outputBasePath == null || body.length() == 0) {
            throw new IOException("Select at least one non-YAML input file.");
        }

        if (metadata.length() > 0) {
            body.insert(0, "---\n" + metadata.toString().trim() + "\n---\n\n");
        }
        return new PandocInput(body.toString(), outputBasePath);
    }

    private String normalizeYamlMetadata(String text) {
        String clean = text.trim();

        if (clean.startsWith("---")) {
            clean = clean.substring(3).trim();
        }
        if (clean.endsWith("...")) {
            clean = clean.substring(0, clean.length() - 3).trim();
        }
        if (clean.endsWith("---")) {
            clean = clean.substring(0, clean.length() - 3).trim();
        }
        return clean;
    }

    private byte[] postPandoc(
        String serverUrl,
        String text,
        String from,
        String to
    ) throws IOException, JSONException {
        return postPandoc(serverUrl, text, from, to, HTTP_TIMEOUT_MS, true);
    }

    private byte[] postPandoc(
        String serverUrl,
        String text,
        String from,
        String to,
        int timeoutMs
    ) throws IOException, JSONException {
        return postPandoc(serverUrl, text, from, to, timeoutMs, true);
    }

    private byte[] postPandoc(
        String serverUrl,
        String text,
        String from,
        String to,
        int timeoutMs,
        boolean standalone
    ) throws IOException, JSONException {
        URL url = new URL(normalizePandocServerUrl(serverUrl));
        HttpURLConnection connection = (HttpURLConnection) url.openConnection();
        connection.setRequestMethod("POST");
        connection.setConnectTimeout(timeoutMs);
        connection.setReadTimeout(timeoutMs);
        connection.setDoOutput(true);
        connection.setRequestProperty("Accept", "application/octet-stream");
        connection.setRequestProperty("Content-Type", "application/json; charset=utf-8");
        connection.setRequestProperty("Connection", "close");

        JSONObject request = new JSONObject();
        request.put("text", text);
        request.put("from", from);
        request.put("to", to);
        request.put("standalone", standalone);
        addPandocBundledDefaults(request, to, standalone);

        byte[] body = request.toString().getBytes(StandardCharsets.UTF_8);
        mainHandler.post(() -> appendLog("Pandoc POST: " + url +
            " (" + body.length + " bytes)"));
        connection.setFixedLengthStreamingMode(body.length);
        try (OutputStream out = connection.getOutputStream()) {
            out.write(body);
        }

        int status = connection.getResponseCode();
        byte[] response = readResponse(connection);
        if (status < 200 || status >= 300) {
            connection.disconnect();
            throw new IOException("pandoc HTTP " + status + ": " + preview(response));
        }
        connection.disconnect();
        return response;
    }

    private void commitGithubRepository() {
        String repoText = githubRepoField.getText().toString().trim();
        String rootPath = githubPathField.getText().toString().trim();
        String branch = githubBranchField.getText().toString().trim();
        String token = githubTokenField.getText().toString().trim();
        String message = githubCommitField == null ? "" :
            githubCommitField.getText().toString().trim();

        if (repoText.isEmpty() || !repoText.contains("/")) {
            setStatus("GitHub repository missing", "Use owner/repository.");
            return;
        }
        if (branch.isEmpty()) {
            branch = "main";
            githubBranchField.setText(branch);
        }
        if (token.isEmpty()) {
            setStatus("GitHub token missing", "Use a token with Contents: read and write.");
            return;
        }
        if (message.isEmpty()) {
            message = "Sync repository";
        }

        String[] repo = repoText.split("/", 2);
        String owner = repo[0].trim();
        String name = repo[1].trim();
        if (owner.isEmpty() || name.isEmpty()) {
            setStatus("GitHub repository missing", "Use owner/repository.");
            return;
        }

        if (githubCommitButton != null) {
            githubCommitButton.setEnabled(false);
        }
        setStatus("Checking remote changes", owner + "/" + name + "@" + branch);
        appendLog("GitHub commit: " + owner + "/" + name + " " +
            sanitizeOptionalRepoPath(rootPath));

        String finalBranch = branch;
        String finalMessage = message;
        String finalRootPath = rootPath;
        Thread thread = new Thread(() -> {
            try {
                GitHubCommitResult result = performGithubCommit(
                    owner,
                    name,
                    finalBranch,
                    finalRootPath,
                    token,
                    finalMessage);
                mainHandler.post(() -> {
                    if (result.noChanges) {
                        setStatus("Nothing to commit", "remote/ already matches GitHub.");
                        appendLog("GitHub commit skipped: no changes.");
                    } else {
                        setStatus(
                            "Committed to GitHub",
                            result.changed + " changed, " + result.deleted + " deleted");
                        String shortSha = result.commitSha.length() <= 7 ?
                            result.commitSha : result.commitSha.substring(0, 7);
                        appendLog("GitHub commit: " + shortSha);
                    }
                    updateGithubActionButtons();
                });
            } catch (IOException | JSONException e) {
                mainHandler.post(() -> {
                    setStatus("GitHub commit failed", usefulMessage(e));
                    appendLog("GitHub commit failed: " + e);
                    updateGithubActionButtons();
                });
            }
        }, "typewrt-github-commit");
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
                    pendingSendFiles.clear();
                    pendingSendFiles.addAll(result.updates);
                    restorePending = !result.updates.isEmpty();
                    setStatus(
                        "Pulled from GitHub",
                        result.changed + " changed, " + result.unchanged +
                            " unchanged, " + result.deleted + " deleted");
                    appendLog("GitHub pull queued " + result.updates.size() +
                        " update" + plural(result.updates.size()) +
                        (result.skipped > 0 ? "; skipped " + result.skipped : ""));
                    refreshRemoteTree();
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
        SyncManifest manifest = loadSyncManifest();

        try {
            collectGithubContents(owner, repo, branch, root, root, token, remoteFiles);
        } catch (IOException e) {
            if (!usefulMessage(e).startsWith("contents HTTP 404:")) {
                throw e;
            }
        }
        for (GitHubRemoteFile remote : remoteFiles) {
            String localPath = remoteRelativePath(remote.path, root);

            if (remote.size > MAX_FILE_BYTES || remote.downloadUrl == null ||
                    remote.downloadUrl.isEmpty()) {
                result.skipped++;
                continue;
            }
            remotePaths.add(localPath);
            byte[] data = downloadGithubFile(owner, repo, token, remote);
            if (data.length > MAX_FILE_BYTES) {
                result.skipped++;
                continue;
            }
            if (saveMirrorFileSync(localPath, data, false)) {
                updates.add(FileSnapshot.file(localPath, data, null));
                result.changed++;
            } else {
                result.unchanged++;
            }
            updateManifestRemoteClean(manifest, localPath, data, remote.sha);
        }

        for (LocalMirrorFile local : listLocalMirrorFiles()) {
            if (remotePaths.contains(local.path)) {
                continue;
            }
            if (deleteLocalMirrorFile(local.file)) {
                updates.add(FileSnapshot.deleteMarker(local.path));
                manifest.entries.remove(local.path);
                result.deleted++;
            }
        }
        for (Iterator<Map.Entry<String, SyncEntry>> it = manifest.entries.entrySet().iterator();
                it.hasNext();) {
            if (!remotePaths.contains(it.next().getKey())) {
                it.remove();
            }
        }
        saveSyncManifest(manifest);
        return result;
    }

    private GitHubCommitResult performGithubCommit(
        String owner,
        String repo,
        String branch,
        String rootPath,
        String token,
        String message
    ) throws IOException, JSONException {
        String root = sanitizeOptionalRepoPath(rootPath);
        JSONArray treeItems = new JSONArray();
        GitHubCommitResult result = new GitHubCommitResult();
        SyncManifest manifest = loadSyncManifest();

        reconcileManifestWithLocalFiles(manifest);

        for (SyncEntry entry : new ArrayList<>(manifest.entries.values())) {
            if (!entry.dirty && !entry.deleted) {
                continue;
            }
            if (entry.deleted) {
                if (entry.githubSha.isEmpty() && entry.syncedHash.isEmpty()) {
                    manifest.entries.remove(entry.path);
                    continue;
                }
                JSONObject item = new JSONObject();
                item.put("path", githubPathForSnapshot(root, entry.path));
                item.put("sha", JSONObject.NULL);
                treeItems.put(item);
                result.deleted++;
                continue;
            }

            File localFile = remoteFile(entry.path);
            if (!localFile.isFile()) {
                entry.deleted = true;
                JSONObject item = new JSONObject();
                item.put("path", githubPathForSnapshot(root, entry.path));
                item.put("sha", JSONObject.NULL);
                treeItems.put(item);
                result.deleted++;
                continue;
            }

            byte[] data = readFileBytes(localFile);
            String blobSha = createGithubBlob(owner, repo, token, data);
            JSONObject item = new JSONObject();
            item.put("path", githubPathForSnapshot(root, entry.path));
            item.put("mode", "100644");
            item.put("type", "blob");
            item.put("sha", blobSha);
            treeItems.put(item);
            entry.githubSha = blobSha;
            entry.localHash = sha256Hex(data);
            entry.syncedHash = entry.localHash;
            entry.size = data.length;
            entry.dirty = false;
            entry.deleted = false;
            result.changed++;
        }

        if (treeItems.length() == 0) {
            result.noChanges = true;
            saveSyncManifest(manifest);
            return result;
        }

        String headSha = fetchGithubRefSha(owner, repo, branch, token);
        String baseTreeSha = fetchGithubCommitTreeSha(owner, repo, headSha, token);
        String newTreeSha = createGithubTree(owner, repo, token, baseTreeSha, treeItems);
        result.commitSha = createGithubCommit(owner, repo, token, message, newTreeSha, headSha);
        updateGithubRef(owner, repo, branch, token, result.commitSha);
        for (Iterator<Map.Entry<String, SyncEntry>> it = manifest.entries.entrySet().iterator();
                it.hasNext();) {
            SyncEntry entry = it.next().getValue();
            if (entry.deleted) {
                it.remove();
            }
        }
        saveSyncManifest(manifest);
        return result;
    }

    private void showGithubRestoreDialog() {
        String repoText = githubRepoField.getText().toString().trim();
        String rootPath = githubPathField.getText().toString().trim();
        String branch = githubBranchField.getText().toString().trim();
        String token = githubTokenField.getText().toString().trim();

        if (repoText.isEmpty() || !repoText.contains("/") || branch.isEmpty() || token.isEmpty()) {
            setStatus("GitHub configuration missing", "Enter repository, branch, and token first.");
            return;
        }

        String[] repo = repoText.split("/", 2);
        String owner = repo[0].trim();
        String name = repo[1].trim();
        if (owner.isEmpty() || name.isEmpty()) {
            setStatus("GitHub repository missing", "Use owner/repository.");
            return;
        }

        setStatus("Loading commits", owner + "/" + name + "@" + branch);
        Thread thread = new Thread(() -> {
            try {
                ArrayList<GitHubCommitItem> commits =
                    fetchGithubCommits(owner, name, branch, rootPath, token, "");
                mainHandler.post(() -> showRestoreChoices(owner, name, branch, rootPath, token, commits));
            } catch (IOException | JSONException e) {
                mainHandler.post(() -> {
                    setStatus("Commit list failed", usefulMessage(e));
                    appendLog("GitHub commits failed: " + e);
                });
            }
        }, "typewrt-github-commits");
        thread.start();
    }

    private void showRestoreChoices(
        String owner,
        String repo,
        String branch,
        String rootPath,
        String token,
        ArrayList<GitHubCommitItem> commits
    ) {
        if (commits.isEmpty()) {
            setStatus("No commits found", "GitHub returned an empty history.");
            return;
        }

        String[] labels = new String[commits.size()];
        for (int i = 0; i < commits.size(); i++) {
            labels[i] = commits.get(i).label();
        }

        new AlertDialog.Builder(this)
            .setTitle("Restore remote")
            .setItems(labels, (dialog, which) -> restoreGithubCommit(
                owner,
                repo,
                rootPath,
                token,
                commits.get(which)))
            .show();
    }

    private void restoreGithubCommit(
        String owner,
        String repo,
        String rootPath,
        String token,
        GitHubCommitItem commit
    ) {
        setStatus("Restoring " + commit.shortSha(), commit.title);
        Thread thread = new Thread(() -> {
            try {
                GitHubFetchResult result = performGithubFetch(
                    owner,
                    repo,
                    commit.sha,
                    rootPath,
                    token);
                mainHandler.post(() -> {
                    pendingSendFiles.clear();
                    pendingSendFiles.addAll(result.updates);
                    restorePending = !result.updates.isEmpty();
                    setStatus(
                        "Restored " + commit.shortSha(),
                        result.changed + " changed, " + result.deleted + " deleted");
                    appendLog("Restore queued " + result.updates.size() + " update" +
                        plural(result.updates.size()));
                    refreshRemoteTree();
                    updateSendFileActions();
                    updateGithubActionButtons();
                });
            } catch (IOException | JSONException e) {
                mainHandler.post(() -> {
                    setStatus("Restore failed", usefulMessage(e));
                    appendLog("Restore failed: " + e);
                    updateGithubActionButtons();
                });
            }
        }, "typewrt-github-restore");
        thread.start();
    }

    private void showGithubFileHistory() {
        String repoText = githubRepoField.getText().toString().trim();
        String rootPath = githubPathField.getText().toString().trim();
        String branch = githubBranchField.getText().toString().trim();
        String token = githubTokenField.getText().toString().trim();

        if (repoText.isEmpty() || !repoText.contains("/") || branch.isEmpty() || token.isEmpty()) {
            setStatus("GitHub configuration missing", "Enter repository, branch, and token first.");
            return;
        }

        String[] repo = repoText.split("/", 2);
        String owner = repo[0].trim();
        String name = repo[1].trim();
        if (owner.isEmpty() || name.isEmpty()) {
            setStatus("GitHub repository missing", "Use owner/repository.");
            return;
        }

        setStatus("Loading repository files", "Choose a file for history.");
        Thread thread = new Thread(() -> {
            try {
                ArrayList<LocalMirrorFile> files = listFolderFiles(remoteDir());
                mainHandler.post(() -> showGithubFileHistoryPicker(
                    owner,
                    name,
                    branch,
                    rootPath,
                    token,
                    files));
            } catch (IOException | RuntimeException e) {
                mainHandler.post(() -> {
                    setStatus("File picker failed", usefulMessage(e));
                    appendLog("File picker failed: " + e);
                });
            }
        }, "typewrt-github-file-picker");
        thread.start();
    }

    private void showGithubFileHistoryPicker(
        String owner,
        String repo,
        String branch,
        String rootPath,
        String token,
        ArrayList<LocalMirrorFile> files
    ) {
        if (files.isEmpty()) {
            setStatus("No files found", "Pull or receive files into the repository first.");
            return;
        }

        String[] labels = new String[files.size()];
        for (int i = 0; i < files.size(); i++) {
            labels[i] = files.get(i).path;
        }
        new AlertDialog.Builder(this)
            .setTitle("File commits")
            .setItems(labels, (dialog, which) -> loadGithubFileHistory(
                owner,
                repo,
                branch,
                rootPath,
                token,
                files.get(which).path))
            .show();
    }

    private void loadGithubFileHistory(
        String owner,
        String repo,
        String branch,
        String rootPath,
        String token,
        String filePath
    ) {
        setStatus("Loading file commits", filePath);
        Thread thread = new Thread(() -> {
            try {
                ArrayList<GitHubCommitItem> commits =
                    fetchGithubCommits(owner, repo, branch, rootPath, token, filePath);
                mainHandler.post(() -> showCommitList("File commits: " + filePath, commits));
            } catch (IOException | JSONException e) {
                mainHandler.post(() -> {
                    setStatus("File history failed", usefulMessage(e));
                    appendLog("File history failed: " + e);
                });
            }
        }, "typewrt-github-file-history");
        thread.start();
    }

    private void showCommitList(String title, ArrayList<GitHubCommitItem> commits) {
        if (commits.isEmpty()) {
            setStatus("No commits found", "GitHub returned an empty history.");
            return;
        }

        String[] labels = new String[commits.size()];
        for (int i = 0; i < commits.size(); i++) {
            labels[i] = commits.get(i).label();
        }
        new AlertDialog.Builder(this)
            .setTitle(title)
            .setItems(labels, null)
            .show();
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
            entry.optString("download_url", ""),
            entry.optString("sha", "")));
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

    private byte[] downloadGithubFile(
        String owner,
        String repo,
        String token,
        GitHubRemoteFile file
    ) throws IOException, JSONException {
        if (file.sha != null && !file.sha.isEmpty()) {
            String endpoint = "https://api.github.com/repos/" + encodePathPart(owner)
                + "/" + encodePathPart(repo)
                + "/git/blobs/" + encodePathPart(file.sha);
            JSONObject blob = sendGithubJson(endpoint, "GET", token, null);
            String encoding = blob.optString("encoding", "");
            String content = blob.optString("content", "").replace("\n", "");

            if ("base64".equals(encoding) && !content.isEmpty()) {
                return Base64.decode(content, Base64.DEFAULT);
            }
        }
        return downloadGithubBytes(file.downloadUrl, token);
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

    private ArrayList<GitHubCommitItem> fetchGithubCommits(
        String owner,
        String repo,
        String branch,
        String rootPath,
        String token,
        String filePath
    ) throws IOException, JSONException {
        String endpoint = "https://api.github.com/repos/" + encodePathPart(owner)
            + "/" + encodePathPart(repo)
            + "/commits?sha=" + encodePathPart(branch)
            + "&per_page=30";
        String path = sanitizeOptionalRepoPath(filePath);
        String root = sanitizeOptionalRepoPath(rootPath);

        if (!path.isEmpty()) {
            endpoint += "&path=" + encodePathPart(githubPathForSnapshot(root, path));
        } else if (!root.isEmpty()) {
            endpoint += "&path=" + encodePathPart(root);
        }

        HttpURLConnection connection = openGithubConnection(endpoint, "GET", token);
        int status = connection.getResponseCode();
        byte[] response = readResponse(connection);
        connection.disconnect();
        if (status < 200 || status >= 300) {
            throw new IOException("commits HTTP " + status + ": " + preview(response));
        }

        JSONArray json = new JSONArray(new String(response, StandardCharsets.UTF_8));
        ArrayList<GitHubCommitItem> commits = new ArrayList<>();
        for (int i = 0; i < json.length(); i++) {
            JSONObject item = json.getJSONObject(i);
            JSONObject commit = item.optJSONObject("commit");
            JSONObject author = commit == null ? null : commit.optJSONObject("author");
            String message = commit == null ? "" : commit.optString("message", "");
            String title = message.split("\\n", 2)[0].trim();
            commits.add(new GitHubCommitItem(
                item.optString("sha", ""),
                title.isEmpty() ? "(no message)" : title,
                author == null ? "" : author.optString("date", "")));
        }
        return commits;
    }

    private String createGithubBlob(
        String owner,
        String repo,
        String token,
        byte[] data
    ) throws IOException, JSONException {
        String endpoint = "https://api.github.com/repos/" + encodePathPart(owner)
            + "/" + encodePathPart(repo)
            + "/git/blobs";
        JSONObject request = new JSONObject();
        request.put("content", Base64.encodeToString(data, Base64.NO_WRAP));
        request.put("encoding", "base64");

        return sendGithubJson(endpoint, "POST", token, request).getString("sha");
    }

    private String fetchGithubRefSha(
        String owner,
        String repo,
        String branch,
        String token
    ) throws IOException, JSONException {
        String endpoint = "https://api.github.com/repos/" + encodePathPart(owner)
            + "/" + encodePathPart(repo)
            + "/git/ref/heads/" + encodeRepoPath(branch);
        JSONObject response = sendGithubJson(endpoint, "GET", token, null);
        JSONObject object = response.getJSONObject("object");

        return object.getString("sha");
    }

    private String fetchGithubCommitTreeSha(
        String owner,
        String repo,
        String commitSha,
        String token
    ) throws IOException, JSONException {
        String endpoint = "https://api.github.com/repos/" + encodePathPart(owner)
            + "/" + encodePathPart(repo)
            + "/git/commits/" + encodePathPart(commitSha);
        JSONObject response = sendGithubJson(endpoint, "GET", token, null);
        JSONObject tree = response.getJSONObject("tree");

        return tree.getString("sha");
    }

    private String createGithubTree(
        String owner,
        String repo,
        String token,
        String baseTreeSha,
        JSONArray treeItems
    ) throws IOException, JSONException {
        String endpoint = "https://api.github.com/repos/" + encodePathPart(owner)
            + "/" + encodePathPart(repo)
            + "/git/trees";
        JSONObject request = new JSONObject();
        request.put("base_tree", baseTreeSha);
        request.put("tree", treeItems);

        return sendGithubJson(endpoint, "POST", token, request).getString("sha");
    }

    private String createGithubCommit(
        String owner,
        String repo,
        String token,
        String message,
        String treeSha,
        String parentSha
    ) throws IOException, JSONException {
        String endpoint = "https://api.github.com/repos/" + encodePathPart(owner)
            + "/" + encodePathPart(repo)
            + "/git/commits";
        JSONObject request = new JSONObject();
        JSONArray parents = new JSONArray();
        parents.put(parentSha);
        request.put("message", message);
        request.put("tree", treeSha);
        request.put("parents", parents);

        return sendGithubJson(endpoint, "POST", token, request).getString("sha");
    }

    private void updateGithubRef(
        String owner,
        String repo,
        String branch,
        String token,
        String commitSha
    ) throws IOException, JSONException {
        String endpoint = "https://api.github.com/repos/" + encodePathPart(owner)
            + "/" + encodePathPart(repo)
            + "/git/refs/heads/" + encodeRepoPath(branch);
        JSONObject request = new JSONObject();
        request.put("sha", commitSha);
        request.put("force", false);
        sendGithubJson(endpoint, "PATCH", token, request);
    }

    private JSONObject sendGithubJson(
        String endpoint,
        String method,
        String token,
        JSONObject body
    ) throws IOException, JSONException {
        HttpURLConnection connection = openGithubConnection(endpoint, method, token);
        if (body != null) {
            connection.setDoOutput(true);
            connection.setRequestProperty("Content-Type", "application/json; charset=utf-8");
            try (OutputStream out = connection.getOutputStream()) {
                out.write(body.toString().getBytes(StandardCharsets.UTF_8));
            }
        }

        int status = connection.getResponseCode();
        byte[] response = readResponse(connection);
        connection.disconnect();
        if (status < 200 || status >= 300) {
            throw new IOException(method + " HTTP " + status + ": " + preview(response));
        }
        return new JSONObject(new String(response, StandardCharsets.UTF_8));
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
        setHttpMethod(connection, method);
        connection.setConnectTimeout(HTTP_TIMEOUT_MS);
        connection.setReadTimeout(HTTP_TIMEOUT_MS);
        connection.setRequestProperty("Accept", "application/vnd.github+json");
        connection.setRequestProperty("Authorization", "Bearer " + token);
        connection.setRequestProperty("X-GitHub-Api-Version", GITHUB_API_VERSION);
        return connection;
    }

    private void setHttpMethod(HttpURLConnection connection, String method) throws IOException {
        try {
            connection.setRequestMethod(method);
        } catch (ProtocolException e) {
            if (!"PATCH".equals(method)) {
                throw e;
            }
            try {
                Field field = HttpURLConnection.class.getDeclaredField("method");
                field.setAccessible(true);
                field.set(connection, method);
            } catch (ReflectiveOperationException | RuntimeException reflectionError) {
                throw e;
            }
        }
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

    private String extensionForPath(String path) {
        String name = fileNameFromPath(path);
        int dot = name.lastIndexOf('.');

        return dot >= 0 && dot < name.length() - 1 ? name.substring(dot) : "this";
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
        if (e instanceof SocketTimeoutException) {
            return "Timed out waiting for server response.";
        }
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

    private static final class PandocInput {
        final String text;
        final String outputBasePath;

        PandocInput(String text, String outputBasePath) {
            this.text = text;
            this.outputBasePath = outputBasePath;
        }
    }

    private static final class GitHubRemoteFile {
        final String path;
        final long size;
        final String downloadUrl;
        final String sha;

        GitHubRemoteFile(String path, long size, String downloadUrl, String sha) {
            this.path = path;
            this.size = size;
            this.downloadUrl = downloadUrl;
            this.sha = sha;
        }
    }

    private static final class EncryptedValue {
        final String iv;
        final String data;

        EncryptedValue(String iv, String data) {
            this.iv = iv;
            this.data = data;
        }
    }

    private static final class LocalMirrorFile {
        final String path;
        final File file;

        LocalMirrorFile(String path, File file) {
            this.path = path;
            this.file = file;
        }
    }

    private static final class BrowserEntry {
        final String name;
        final String path;
        final File file;
        final boolean directory;

        BrowserEntry(String name, String path, File file, boolean directory) {
            this.name = name;
            this.path = path;
            this.file = file;
            this.directory = directory;
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

    private static final class GitHubCommitResult {
        boolean noChanges;
        int changed;
        int deleted;
        String commitSha = "";
    }

    private static final class SyncManifest {
        final HashMap<String, SyncEntry> entries = new HashMap<>();
    }

    private static final class SyncEntry {
        final String path;
        String githubSha = "";
        String syncedHash = "";
        String localHash = "";
        long size;
        boolean dirty;
        boolean deleted;

        SyncEntry(String path) {
            this.path = path;
        }
    }

    private static final class GitHubCommitItem {
        final String sha;
        final String title;
        final String date;

        GitHubCommitItem(String sha, String title, String date) {
            this.sha = sha;
            this.title = title;
            this.date = date;
        }

        String shortSha() {
            return sha.length() <= 7 ? sha : sha.substring(0, 7);
        }

        String label() {
            String cleanDate = date == null ? "" : date.replace('T', ' ');
            int dot = cleanDate.indexOf('.');

            if (dot > 0) {
                cleanDate = cleanDate.substring(0, dot);
            }
            if (cleanDate.endsWith("Z")) {
                cleanDate = cleanDate.substring(0, cleanDate.length() - 1);
            }
            return shortSha() + "  " + cleanDate + "  " + title;
        }
    }

    private SyncManifest loadSyncManifest() throws IOException, JSONException {
        SyncManifest manifest = new SyncManifest();
        File file = syncManifestFile();

        if (!file.isFile()) {
            return manifest;
        }

        JSONObject root = new JSONObject(new String(readFileBytes(file), StandardCharsets.UTF_8));
        JSONObject entries = root.optJSONObject("entries");
        if (entries == null) {
            return manifest;
        }

        Iterator<String> keys = entries.keys();
        while (keys.hasNext()) {
            String rawPath = keys.next();
            String path = sanitizeTransferPath(rawPath, "");
            if (path.isEmpty()) {
                continue;
            }
            JSONObject json = entries.optJSONObject(rawPath);
            if (json == null) {
                continue;
            }

            SyncEntry entry = new SyncEntry(path);
            entry.githubSha = json.optString("githubSha", "");
            entry.syncedHash = json.optString("syncedHash", "");
            entry.localHash = json.optString("localHash", entry.syncedHash);
            if (entry.syncedHash.isEmpty()) {
                entry.syncedHash = entry.localHash;
            }
            entry.size = json.optLong("size", 0);
            entry.dirty = json.optBoolean("dirty", false);
            entry.deleted = json.optBoolean("deleted", false);
            manifest.entries.put(path, entry);
        }
        return manifest;
    }

    private void saveSyncManifest(SyncManifest manifest) throws IOException, JSONException {
        JSONObject root = new JSONObject();
        JSONObject entries = new JSONObject();

        root.put("version", 1);
        for (SyncEntry entry : manifest.entries.values()) {
            if (entry.path.isEmpty()) {
                continue;
            }
            JSONObject json = new JSONObject();
            json.put("githubSha", entry.githubSha == null ? "" : entry.githubSha);
            json.put("syncedHash", entry.syncedHash == null ? "" : entry.syncedHash);
            json.put("localHash", entry.localHash == null ? "" : entry.localHash);
            json.put("size", entry.size);
            json.put("dirty", entry.dirty);
            json.put("deleted", entry.deleted);
            entries.put(entry.path, json);
        }
        root.put("entries", entries);
        writeFileBytes(syncManifestFile(),
            root.toString().getBytes(StandardCharsets.UTF_8));
    }

    private File syncManifestFile() throws IOException {
        return new File(appStorageDir(), SYNC_MANIFEST_NAME);
    }

    private SyncEntry manifestEntry(SyncManifest manifest, String path) {
        String safePath = sanitizeTransferPath(path, "");
        SyncEntry entry = manifest.entries.get(safePath);

        if (entry == null) {
            entry = new SyncEntry(safePath);
            manifest.entries.put(safePath, entry);
        }
        return entry;
    }

    private void updateManifestRemoteClean(
        SyncManifest manifest,
        String path,
        byte[] data,
        String githubSha
    ) {
        SyncEntry entry = manifestEntry(manifest, path);
        String hash = sha256Hex(data);

        entry.githubSha = githubSha == null ? "" : githubSha;
        entry.syncedHash = hash;
        entry.localHash = hash;
        entry.size = data.length;
        entry.dirty = false;
        entry.deleted = false;
    }

    private void markManifestLocalWrite(String path, byte[] data) {
        try {
            SyncManifest manifest = loadSyncManifest();
            SyncEntry entry = manifestEntry(manifest, path);
            String hash = sha256Hex(data);

            entry.localHash = hash;
            entry.size = data.length;
            entry.deleted = false;
            entry.dirty = entry.syncedHash == null || entry.syncedHash.isEmpty() ||
                !entry.syncedHash.equals(hash);
            saveSyncManifest(manifest);
        } catch (IOException | JSONException | RuntimeException e) {
            mainHandler.post(() -> appendLog("Sync manifest update failed: " + e));
        }
    }

    private void markManifestDeleted(String path) {
        try {
            SyncManifest manifest = loadSyncManifest();
            SyncEntry entry = manifestEntry(manifest, path);

            entry.localHash = "";
            entry.size = 0;
            entry.deleted = true;
            entry.dirty = true;
            saveSyncManifest(manifest);
        } catch (IOException | JSONException | RuntimeException e) {
            mainHandler.post(() -> appendLog("Sync manifest delete failed: " + e));
        }
    }

    private void reconcileManifestWithLocalFiles(SyncManifest manifest) {
        HashSet<String> seen = new HashSet<>();
        ArrayList<LocalMirrorFile> localFiles;

        try {
            localFiles = listFolderFiles(remoteDir());
        } catch (IOException | RuntimeException e) {
            mainHandler.post(() -> appendLog("Sync manifest scan failed: " + e));
            return;
        }

        for (LocalMirrorFile local : localFiles) {
            seen.add(local.path);
            SyncEntry entry = manifest.entries.get(local.path);
            if (entry != null) {
                continue;
            }
            try {
                byte[] data = readFileBytes(local.file);
                entry = manifestEntry(manifest, local.path);
                entry.localHash = sha256Hex(data);
                entry.size = data.length;
                entry.dirty = true;
                entry.deleted = false;
            } catch (IOException | RuntimeException e) {
                mainHandler.post(() -> appendLog("Sync manifest scan failed: " + e));
            }
        }

        for (SyncEntry entry : manifest.entries.values()) {
            if (entry.deleted || seen.contains(entry.path)) {
                continue;
            }
            if (!entry.githubSha.isEmpty() || !entry.syncedHash.isEmpty()) {
                entry.deleted = true;
                entry.dirty = true;
            }
        }
    }

    private String sha256Hex(byte[] data) {
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            byte[] hash = digest.digest(data);
            StringBuilder out = new StringBuilder(hash.length * 2);
            for (byte b : hash) {
                out.append(String.format(Locale.US, "%02x", b & 0xff));
            }
            return out.toString();
        } catch (Exception e) {
            throw new IllegalStateException("SHA-256 is not available", e);
        }
    }

    private boolean saveMirrorFileSync(
        String fileName,
        byte[] data,
        boolean markDirty
    ) throws IOException {
        String safePath = sanitizeTransferPath(fileName, "typewrt.txt");
        File target = remoteFile(safePath);

        if (target.isFile()) {
            try {
                byte[] existingData = readFileBytes(target);
                if (bytesEqual(existingData, data)) {
                    if (markDirty) {
                        markManifestLocalWrite(safePath, data);
                    }
                    mainHandler.post(() -> setLatestFile(safePath, data, null));
                    return false;
                }
            } catch (IOException | RuntimeException ignored) {
                // If the local mirror is unreadable, overwrite it from GitHub.
            }
            writeFileBytes(target, data);
            if (markDirty) {
                markManifestLocalWrite(safePath, data);
            }
            mainHandler.post(() -> setLatestFile(safePath, data, null));
            return true;
        }

        writeFileBytes(target, data);
        if (markDirty) {
            markManifestLocalWrite(safePath, data);
        }
        mainHandler.post(() -> setLatestFile(safePath, data, null));
        return true;
    }

    private ArrayList<LocalMirrorFile> listLocalMirrorFiles() {
        try {
            return listFolderFiles(remoteDir());
        } catch (IOException | RuntimeException e) {
            mainHandler.post(() -> appendLog("Mirror listing failed: " + e));
            return new ArrayList<>();
        }
    }

    private boolean deleteLocalMirrorFile(File file) {
        try {
            boolean deleted = deleteRecursive(file);
            pruneEmptyParents(file.getParentFile(), remoteDir());
            return deleted;
        } catch (RuntimeException e) {
            mainHandler.post(() -> appendLog("Mirror delete failed: " + e));
            return false;
        } catch (IOException e) {
            mainHandler.post(() -> appendLog("Mirror delete failed: " + e));
            return false;
        }
    }

    private void saveFile(String fileName, byte[] data) {
        String safePath = sanitizeTransferPath(fileName, "typewrt.txt");

        try {
            saveMirrorFileSync(safePath, data, true);
            mainHandler.post(() -> {
                setLatestFile(safePath, data, null);
                setStatus("Saved " + safePath, "remote/");
                appendLog("Remote saved: " + safePath);
                refreshRemoteTree();
                setIdleButtons();
            });
        } catch (IOException | RuntimeException e) {
            mainHandler.post(() -> {
                setStatus("Save failed", e.getMessage() == null ? e.toString() : e.getMessage());
                appendLog("Save failed: " + e);
                setIdleButtons();
                updateFileActions();
            });
        }
    }

    private void saveOutputFile(String fileName, byte[] data) throws IOException {
        String safePath = sanitizeTransferPath(fileName, "typewrt.txt");
        File target = outputFile(safePath);

        writeFileBytes(target, data);
        mainHandler.post(() -> {
            setStatus("Exported " + safePath, "output/");
            appendLog("Output saved: " + safePath);
            updateFileActions();
            updatePandocSelectionView();
            refreshPandocOutputs();
        });
    }

    private boolean deleteRemoteFile(String path) {
        try {
            File file = remoteFile(path);
            boolean deleted = deleteRecursive(file);
            pruneEmptyParents(file.getParentFile(), remoteDir());
            return deleted;
        } catch (IOException | RuntimeException e) {
            appendLog("Remote delete failed: " + e);
            return false;
        }
    }

    private File appStorageDir() throws IOException {
        File root = getExternalFilesDir(null);

        if (root == null) {
            root = getFilesDir();
        }
        if (root == null) {
            throw new IOException("App storage is not available.");
        }
        if (!root.isDirectory() && !root.mkdirs()) {
            throw new IOException("Could not create app storage.");
        }
        return root;
    }

    private File remoteDir() throws IOException {
        return ensureFolder(new File(appStorageDir(), REMOTE_DIR_NAME));
    }

    private File outputDir() throws IOException {
        return ensureFolder(new File(appStorageDir(), OUTPUT_DIR_NAME));
    }

    private File ensureFolder(File folder) throws IOException {
        if (!folder.isDirectory() && !folder.mkdirs()) {
            throw new IOException("Could not create " + folder.getName() + ".");
        }
        return folder;
    }

    private File remoteFile(String path) throws IOException {
        return fileInside(remoteDir(), sanitizeTransferPath(path, "typewrt.txt"));
    }

    private File outputFile(String path) throws IOException {
        return fileInside(outputDir(), sanitizeTransferPath(path, "typewrt.txt"));
    }

    private File fileInside(File root, String path) throws IOException {
        File canonicalRoot = root.getCanonicalFile();
        File file = new File(canonicalRoot, path).getCanonicalFile();
        String rootPath = canonicalRoot.getPath();
        String filePath = file.getPath();

        if (!filePath.equals(rootPath) && !filePath.startsWith(rootPath + File.separator)) {
            throw new IOException("Path escapes app storage.");
        }
        return file;
    }

    private void writeFileBytes(File file, byte[] data) throws IOException {
        File parent = file.getParentFile();

        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("Could not create " + parent.getName() + ".");
        }
        try (OutputStream out = new FileOutputStream(file, false)) {
            out.write(data);
        }
    }

    private byte[] readFileBytes(File file) throws IOException {
        try (InputStream in = new FileInputStream(file);
             ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[4096];
            long total = 0;
            int n;

            while ((n = in.read(buffer)) != -1) {
                total += n;
                if (total > MAX_FILE_BYTES || total > Integer.MAX_VALUE) {
                    throw new IOException(file.getName() + " is larger than 20 MB.");
                }
                out.write(buffer, 0, n);
            }
            return out.toByteArray();
        }
    }

    private void copyStream(InputStream in, OutputStream out) throws IOException {
        byte[] buffer = new byte[8192];
        int n;

        while ((n = in.read(buffer)) != -1) {
            out.write(buffer, 0, n);
        }
    }

    private ArrayList<LocalMirrorFile> listFolderFiles(File root) throws IOException {
        ArrayList<LocalMirrorFile> files = new ArrayList<>();

        collectFolderFiles(root.getCanonicalFile(), root.getCanonicalFile(), files);
        Collections.sort(files, (a, b) -> a.path.compareToIgnoreCase(b.path));
        return files;
    }

    private void collectFolderFiles(
        File root,
        File dir,
        ArrayList<LocalMirrorFile> out
    ) throws IOException {
        File[] entries = dir.listFiles();

        if (entries == null) {
            return;
        }
        for (File entry : entries) {
            if (entry.isDirectory()) {
                collectFolderFiles(root, entry, out);
            } else if (entry.isFile()) {
                out.add(new LocalMirrorFile(relativeFilePath(root, entry), entry));
            }
        }
    }

    private String relativeFilePath(File root, File file) throws IOException {
        String rootPath = root.getCanonicalPath();
        String filePath = file.getCanonicalPath();

        if (filePath.equals(rootPath)) {
            return "";
        }
        if (filePath.startsWith(rootPath + File.separator)) {
            return sanitizeTransferPath(
                filePath.substring(rootPath.length() + 1).replace(File.separatorChar, '/'),
                "");
        }
        throw new IOException("File is outside app storage.");
    }

    private ArrayList<BrowserEntry> listBrowserEntries(File root, File current) throws IOException {
        ArrayList<BrowserEntry> entries = new ArrayList<>();
        File[] children = current.listFiles();

        if (children == null) {
            return entries;
        }
        Arrays.sort(children, (a, b) -> {
            if (a.isDirectory() != b.isDirectory()) {
                return a.isDirectory() ? -1 : 1;
            }
            return a.getName().compareToIgnoreCase(b.getName());
        });
        for (File child : children) {
            String path = relativeFilePath(root, child);
            if (!path.isEmpty()) {
                entries.add(new BrowserEntry(
                    child.getName(),
                    path,
                    child,
                    child.isDirectory()));
            }
        }
        return entries;
    }

    private boolean deleteRecursive(File file) {
        if (!file.exists()) {
            return false;
        }
        if (file.isDirectory()) {
            File[] entries = file.listFiles();
            if (entries != null) {
                for (File entry : entries) {
                    deleteRecursive(entry);
                }
            }
        }
        return file.delete();
    }

    private void pruneEmptyParents(File dir, File stopAt) throws IOException {
        File stop = stopAt.getCanonicalFile();

        while (dir != null) {
            File current = dir.getCanonicalFile();
            if (current.equals(stop)) {
                return;
            }
            File[] entries = current.listFiles();
            if (entries != null && entries.length > 0) {
                return;
            }
            if (!current.delete()) {
                return;
            }
            dir = current.getParentFile();
        }
    }

    private TextView browserRow(String text, int color, Runnable action) {
        TextView row = new TextView(this);
        row.setText(text);
        row.setTextColor(color);
        row.setTextSize(14);
        row.setTypeface(Typeface.MONOSPACE);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setMinHeight(dp(40));
        row.setPadding(dp(8), 0, dp(8), 0);
        row.setSingleLine(false);
        if (action != null) {
            row.setOnClickListener(v -> action.run());
            row.setBackground(rippleBackground(COLOR_SURFACE_HIGH, COLOR_SURFACE_HIGH, dp(6)));
        }
        return row;
    }

    private String pendingMarkForPath(String path, boolean directory) {
        boolean hasQueuedFile = false;
        boolean hasQueuedDelete = false;
        String prefix = path + "/";

        for (FileSnapshot snapshot : pendingSendFiles) {
            boolean match = directory ?
                snapshot.name.startsWith(prefix) :
                snapshot.name.equals(path);
            if (!match) {
                continue;
            }
            if (snapshot.deleteMarker) {
                hasQueuedDelete = true;
            } else {
                hasQueuedFile = true;
            }
        }
        if (hasQueuedDelete) {
            return "x ";
        }
        if (hasQueuedFile) {
            return "* ";
        }
        return "  ";
    }

    private boolean isDeleteQueued(String path) {
        for (FileSnapshot snapshot : pendingSendFiles) {
            if (snapshot.deleteMarker && snapshot.name.equals(path)) {
                return true;
            }
        }
        return false;
    }

    private void queueDeleteForPath(String path) {
        String safePath = sanitizeTransferPath(path, "");

        if (safePath.isEmpty()) {
            setStatus("Delete path missing", "No repository file selected.");
            return;
        }
        deleteRemoteFile(safePath);
        markManifestDeleted(safePath);
        for (int i = pendingSendFiles.size() - 1; i >= 0; i--) {
            if (pendingSendFiles.get(i).name.equals(safePath)) {
                pendingSendFiles.remove(i);
            }
        }
        pendingSendFiles.add(FileSnapshot.deleteMarker(safePath));
        restorePending = true;
        setStatus("Queued delete", safePath);
        appendLog("Queued delete: " + safePath);
        refreshRemoteTree();
        updateSendFileActions();
    }

    private void showFileViewer(String path, File file) {
        Thread thread = new Thread(() -> {
            try {
                byte[] data = readFileBytes(file);
                String text = displayTextForFile(data);
                boolean markdown = isMarkdownFile(path);
                mainHandler.post(() -> showFileViewerDialog(path, text, markdown));
            } catch (IOException | RuntimeException e) {
                mainHandler.post(() ->
                    setStatus("Could not open file", usefulMessage(e)));
            }
        }, "typewrt-file-viewer");
        thread.start();
    }

    private String displayTextForFile(byte[] data) {
        if (!looksLikeText(data)) {
            return "(binary file)";
        }
        String text = new String(data, StandardCharsets.UTF_8);
        int maxChars = 12000;

        if (text.length() > maxChars) {
            return text.substring(0, maxChars) + "\n\n(file preview truncated)";
        }
        return text;
    }

    private boolean looksLikeText(byte[] data) {
        int controls = 0;
        int limit = Math.min(data.length, 4096);

        for (int i = 0; i < limit; i++) {
            int b = data[i] & 0xff;
            if (b == 0) {
                return false;
            }
            if (b < 32 && b != '\n' && b != '\r' && b != '\t') {
                controls++;
            }
        }
        return limit == 0 || controls < Math.max(3, limit / 100);
    }

    private void showFileViewerDialog(String path, String text, boolean markdown) {
        TextView body = new TextView(this);
        body.setText(markdown ? highlightMarkdown(text) : text);
        body.setTextColor(COLOR_TEXT_PRIMARY);
        body.setTextSize(14);
        body.setTypeface(Typeface.MONOSPACE);
        body.setPadding(dp(14), dp(12), dp(14), dp(12));
        body.setTextIsSelectable(true);

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(COLOR_BACKGROUND);
        scroll.addView(body);

        AlertDialog.Builder builder = new AlertDialog.Builder(this)
            .setTitle(path)
            .setView(scroll)
            .setNegativeButton("Close", null);

        if (!isDeleteQueued(path)) {
            builder.setPositiveButton("Queue delete", (dialog, which) -> queueDeleteForPath(path));
        }
        builder.show();
    }

    private boolean isMarkdownFile(String path) {
        String lower = path.toLowerCase(Locale.US);
        return lower.endsWith(".md") || lower.endsWith(".markdown") ||
            lower.endsWith("readme") || lower.endsWith("readme.txt");
    }

    private boolean isYamlFile(String path) {
        String lower = path.toLowerCase(Locale.US);
        return lower.endsWith(".yaml") || lower.endsWith(".yml");
    }

    private SpannableString highlightMarkdown(String text) {
        SpannableString span = new SpannableString(text);
        int lineStart = 0;
        boolean codeBlock = false;

        while (lineStart <= text.length()) {
            int lineEnd = text.indexOf('\n', lineStart);
            if (lineEnd < 0) {
                lineEnd = text.length();
            }
            String line = text.substring(lineStart, lineEnd);
            String trimmed = line.trim();

            if (trimmed.startsWith("```")) {
                codeBlock = !codeBlock;
                setSpan(span, new ForegroundColorSpan(COLOR_TEXT_MUTED), lineStart, lineEnd);
            } else if (codeBlock) {
                setSpan(span, new ForegroundColorSpan(COLOR_TEXT_MUTED), lineStart, lineEnd);
            } else if (line.startsWith("#")) {
                setSpan(span, new ForegroundColorSpan(COLOR_BLUE), lineStart, lineEnd);
                setSpan(span, new StyleSpan(Typeface.BOLD), lineStart, lineEnd);
            } else if (trimmed.startsWith("- ") || trimmed.startsWith("* ") ||
                    trimmed.matches("[0-9]+\\.\\s+.*")) {
                setSpan(span, new ForegroundColorSpan(COLOR_TEXT_SECONDARY), lineStart, lineEnd);
            } else if (trimmed.startsWith(">")) {
                setSpan(span, new ForegroundColorSpan(COLOR_TEXT_MUTED), lineStart, lineEnd);
            }
            highlightInlineMarkdown(span, text, lineStart, lineEnd);
            if (lineEnd == text.length()) {
                break;
            }
            lineStart = lineEnd + 1;
        }
        return span;
    }

    private void highlightInlineMarkdown(
        SpannableString span,
        String text,
        int start,
        int end
    ) {
        int pos = start;

        while (pos < end) {
            int left = text.indexOf('`', pos);
            if (left < 0 || left >= end) {
                break;
            }
            int right = text.indexOf('`', left + 1);
            if (right < 0 || right >= end) {
                break;
            }
            setSpan(span, new ForegroundColorSpan(COLOR_BLUE), left, right + 1);
            pos = right + 1;
        }

        pos = start;
        while (pos < end) {
            int left = text.indexOf('[', pos);
            if (left < 0 || left >= end) {
                break;
            }
            int mid = text.indexOf("](", left);
            int right = mid < 0 ? -1 : text.indexOf(')', mid + 2);
            if (mid < 0 || right < 0 || right >= end) {
                break;
            }
            setSpan(span, new ForegroundColorSpan(COLOR_BLUE), left, right + 1);
            pos = right + 1;
        }
    }

    private void setSpan(SpannableString text, Object what, int start, int end) {
        if (start < end) {
            text.setSpan(what, start, end, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
        }
    }

    private void refreshRemoteTree() {
        if (remoteBrowserView == null) {
            return;
        }
        Thread thread = new Thread(() -> {
            ArrayList<BrowserEntry> entries = new ArrayList<>();
            String shownPath;
            String error = null;

            try {
                File root = remoteDir();
                File current = remoteBrowserPath.isEmpty() ?
                    root : fileInside(root, remoteBrowserPath);

                if (!current.isDirectory()) {
                    remoteBrowserPath = "";
                    current = root;
                }
                shownPath = remoteBrowserPath;
                File[] children = current.listFiles();
                if (children != null) {
                    Arrays.sort(children, (a, b) -> {
                        if (a.isDirectory() != b.isDirectory()) {
                            return a.isDirectory() ? -1 : 1;
                        }
                        return a.getName().compareToIgnoreCase(b.getName());
                    });
                    for (File child : children) {
                        String path = relativeFilePath(root, child);
                        if (!path.isEmpty()) {
                            entries.add(new BrowserEntry(
                                child.getName(),
                                path,
                                child,
                                child.isDirectory()));
                        }
                    }
                }
            } catch (IOException | RuntimeException e) {
                shownPath = remoteBrowserPath;
                error = usefulMessage(e);
            }
            String finalShownPath = shownPath;
            String finalError = error;
            mainHandler.post(() -> {
                renderRemoteBrowser(finalShownPath, entries, finalError);
                if (activeFileView != null && latestFileSnapshot() == null) {
                    activeFileView.setText("Repository ready.");
                }
            });
        }, "typewrt-remote-tree");
        thread.start();
    }

    private void renderRemoteBrowser(
        String shownPath,
        List<BrowserEntry> entries,
        String error
    ) {
        remoteBrowserView.removeAllViews();
        remoteBrowserView.addView(browserRow(
            "remote/" + (shownPath.isEmpty() ? "" : shownPath),
            COLOR_BLUE,
            null));

        if (error != null) {
            remoteBrowserView.addView(browserRow(error, COLOR_TEXT_SECONDARY, null));
            return;
        }
        if (!shownPath.isEmpty()) {
            remoteBrowserView.addView(browserRow("..", COLOR_TEXT_PRIMARY, () -> {
                int slash = remoteBrowserPath.lastIndexOf('/');
                remoteBrowserPath = slash < 0 ? "" : remoteBrowserPath.substring(0, slash);
                refreshRemoteTree();
            }));
        }

        Set<String> visiblePaths = new HashSet<>();
        int visibleRows = 0;
        for (BrowserEntry entry : entries) {
            visiblePaths.add(entry.path);
            String mark = pendingMarkForPath(entry.path, entry.directory);
            String label = entry.directory ?
                mark + "[+] " + entry.name :
                mark + entry.name + "  " + entry.file.length() + " b";

            remoteBrowserView.addView(browserRow(label, COLOR_TEXT_PRIMARY, () -> {
                if (entry.directory) {
                    remoteBrowserPath = entry.path;
                    refreshRemoteTree();
                } else {
                    showFileViewer(entry.path, entry.file);
                }
            }));
            visibleRows++;
        }
        for (FileSnapshot snapshot : pendingSendFiles) {
            if (!snapshot.deleteMarker || visiblePaths.contains(snapshot.name)) {
                continue;
            }
            if (!parentPath(snapshot.name).equals(shownPath)) {
                continue;
            }
            remoteBrowserView.addView(browserRow(
                "x " + fileNameFromPath(snapshot.name),
                COLOR_TEXT_MUTED,
                null));
            visibleRows++;
        }
        if (visibleRows == 0) {
            remoteBrowserView.addView(browserRow("empty", COLOR_TEXT_MUTED, null));
        }
    }

    private String parentPath(String path) {
        int slash = path.lastIndexOf('/');

        return slash < 0 ? "" : path.substring(0, slash);
    }

    private String fileNameFromPath(String path) {
        int slash = path.lastIndexOf('/');

        return slash < 0 ? path : path.substring(slash + 1);
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

    private void setStatus(String status, String details) {
        statusView.setText(status);
        detailsView.setText(details);
    }

    private void appendLog(String message) {
    }
}
