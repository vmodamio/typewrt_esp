#include "typewrt_ble.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_store.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "typewrt_power.h"

#define TYPEWRT_BLE_DEVICE_NAME "Typewrt"
#define TYPEWRT_BLE_SERVICE_UUID 0xffe0
#define TYPEWRT_BLE_TX_UUID 0xffe1
#define TYPEWRT_BLE_RX_UUID 0xffe2
#define TYPEWRT_BLE_ATT_MTU 247
#define TYPEWRT_BLE_MAX_NOTIFY (TYPEWRT_BLE_ATT_MTU - 3)
#define TYPEWRT_BLE_DLE_TX_OCTETS 251
#define TYPEWRT_BLE_DLE_TX_TIME_US 2120
#define TYPEWRT_BLE_RETRY_DELAY_MS 15
#define TYPEWRT_BLE_NOTIFY_RETRIES 200
#define TYPEWRT_BLE_NOTIFY_SETTLE_MS 1
#define TYPEWRT_BLE_DISCONNECT_DELAY_MS 120
#define TYPEWRT_BLE_SHUTDOWN_DELAY_MS 350
#define TYPEWRT_BLE_SHUTDOWN_STACK 3072
#define TYPEWRT_BLE_SHUTDOWN_PRIO 3
#define TYPEWRT_BLE_TRANSFER_STACK 4096
#define TYPEWRT_BLE_TRANSFER_PRIO 4
#define TYPEWRT_BLE_RX_LINE_MAX 256
#define TYPEWRT_BLE_RX_NAME_MAX 96

void ble_store_config_init(void);

typedef enum {
    TYPEWRT_BLE_OFF = 0,
    TYPEWRT_BLE_IDLE,
    TYPEWRT_BLE_WAITING,
    TYPEWRT_BLE_SENDING,
    TYPEWRT_BLE_RECEIVING,
    TYPEWRT_BLE_DONE,
    TYPEWRT_BLE_ERROR,
} typewrt_ble_state_t;

typedef enum {
    TYPEWRT_BLE_MODE_NONE = 0,
    TYPEWRT_BLE_MODE_SEND,
    TYPEWRT_BLE_MODE_RECEIVE,
} typewrt_ble_mode_t;

static const char *TAG = "typewrt_ble";
static StaticSemaphore_t ble_mutex_storage;
static SemaphoreHandle_t ble_mutex;
static bool ble_stack_started;
static bool ble_stack_stopping;
static bool ble_enabled;
static bool ble_synced;
static bool ble_advertising;
static bool ble_sleep_locked;
static uint8_t ble_own_addr_type;
static uint16_t ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t ble_tx_handle;
static bool ble_notify_enabled;
static typewrt_ble_state_t ble_state = TYPEWRT_BLE_OFF;
static typewrt_ble_mode_t ble_mode;
static char ble_last_msg[128] = "ble off";
static unsigned ble_status_generation;

static TaskHandle_t ble_transfer_task_handle;
static TaskHandle_t ble_shutdown_task_handle;
static bool ble_transfer_cancel;
static char *ble_pending_name;
static char *ble_pending_path;
static char *ble_pending_data;
static size_t ble_pending_len;
static bool ble_pending_is_buffer;

static char *ble_receive_dir;
static char *ble_receive_path;
static int ble_receive_fd = -1;
static bool ble_receive_write_started;
static size_t ble_receive_expected;
static size_t ble_receive_received;
static unsigned ble_receive_completed_session;
static char ble_receive_name[TYPEWRT_BLE_RX_NAME_MAX];
static char ble_receive_line[TYPEWRT_BLE_RX_LINE_MAX];
static size_t ble_receive_line_len;

static void typewrt_ble_try_start_transfer(void);
static void typewrt_ble_advertise(void);
static void typewrt_ble_schedule_stack_shutdown(void);

static void typewrt_ble_lock_prepare(void)
{
    if (!ble_mutex) {
        ble_mutex = xSemaphoreCreateMutexStatic(&ble_mutex_storage);
    }
}

static void typewrt_ble_lock(void)
{
    typewrt_ble_lock_prepare();
    xSemaphoreTake(ble_mutex, portMAX_DELAY);
}

static void typewrt_ble_unlock(void)
{
    xSemaphoreGive(ble_mutex);
}

static char *typewrt_ble_strdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);

    if (copy) {
        memcpy(copy, s, len);
    }
    return copy;
}

static const char *typewrt_ble_basename(const char *path)
{
    const char *slash;

    if (!path || !*path) {
        return "unnamed.txt";
    }
    slash = strrchr(path, '/');
    return slash && slash[1] ? slash + 1 : path;
}

static void typewrt_ble_set_msg_locked(const char *msg)
{
    snprintf(ble_last_msg, sizeof(ble_last_msg), "%s", msg);
    ble_status_generation++;
}

static void typewrt_ble_hold_awake_locked(void)
{
    if (!ble_sleep_locked) {
        typewrt_sleep_lock();
        ble_sleep_locked = true;
    }
}

static void typewrt_ble_release_awake_locked(void)
{
    if (ble_sleep_locked) {
        ble_sleep_locked = false;
        typewrt_sleep_unlock();
    }
}

static void typewrt_ble_free_pending_locked(void)
{
    free(ble_pending_name);
    free(ble_pending_path);
    free(ble_pending_data);
    ble_pending_name = NULL;
    ble_pending_path = NULL;
    ble_pending_data = NULL;
    ble_pending_len = 0;
    ble_pending_is_buffer = false;
}

static void typewrt_ble_receive_close_locked(bool discard)
{
    if (ble_receive_fd >= 0) {
        close(ble_receive_fd);
        ble_receive_fd = -1;
    }
    if (ble_receive_write_started) {
        ble_receive_write_started = false;
        typewrt_sd_write_end();
    }
    if (discard && ble_receive_path) {
        unlink(ble_receive_path);
    }
    free(ble_receive_path);
    ble_receive_path = NULL;
    ble_receive_expected = 0;
    ble_receive_received = 0;
    ble_receive_name[0] = '\0';
}

static void typewrt_ble_receive_reset_locked(bool discard)
{
    typewrt_ble_receive_close_locked(discard);
    ble_receive_line_len = 0;
}

static bool typewrt_ble_rx_safe_name(const char *name, char *out, size_t out_len)
{
    const char *base = name;
    size_t n = 0;

    if (!name || !*name || out_len < 2) {
        return false;
    }
    for (const char *p = name; *p; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    while (*base == ' ' || *base == '\t') {
        base++;
    }
    for (const char *p = base; *p && n + 1 < out_len; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c == 0x7f || c == '/' || c == '\\' || c == ':') {
            out[n++] = '_';
        } else {
            out[n++] = (char)c;
        }
    }
    while (n && (out[n - 1] == ' ' || out[n - 1] == '\t')) {
        n--;
    }
    out[n] = '\0';
    return n && strcmp(out, ".") && strcmp(out, "..");
}

static char *typewrt_ble_rx_join_path(const char *dir, const char *name)
{
    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);
    int slash = dir_len && dir[dir_len - 1] != '/';
    char *path = malloc(dir_len + slash + name_len + 1);

    if (!path) {
        return NULL;
    }
    memcpy(path, dir, dir_len);
    if (slash) {
        path[dir_len++] = '/';
    }
    memcpy(path + dir_len, name, name_len + 1);
    return path;
}

static int typewrt_ble_rx_fail_locked(int err)
{
    char msg[128];

    typewrt_ble_receive_reset_locked(true);
    ble_state = TYPEWRT_BLE_WAITING;
    snprintf(msg, sizeof(msg), "ble recv failed: %s", strerror(err));
    typewrt_ble_set_msg_locked(msg);
    return err;
}

static int typewrt_ble_rx_finish_file_locked(void)
{
    char msg[128];
    char name[TYPEWRT_BLE_RX_NAME_MAX];
    size_t received = ble_receive_received;

    snprintf(name, sizeof(name), "%s", ble_receive_name);
    typewrt_ble_receive_close_locked(false);
    ble_receive_completed_session++;
    ble_state = TYPEWRT_BLE_WAITING;
    snprintf(msg, sizeof(msg), "ble received %u bytes: %s",
        (unsigned)received, name);
    typewrt_ble_set_msg_locked(msg);
    return 0;
}

static void typewrt_ble_shutdown_task(void *arg)
{
    esp_err_t ret = ESP_OK;
    int rc;

    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(TYPEWRT_BLE_SHUTDOWN_DELAY_MS));

    typewrt_ble_lock();
    if (!ble_stack_started || ble_enabled || ble_stack_stopping) {
        ble_shutdown_task_handle = NULL;
        typewrt_ble_unlock();
        vTaskDelete(NULL);
        return;
    }
    ble_stack_stopping = true;
    typewrt_ble_unlock();

    rc = nimble_port_stop();
    if (rc) {
        ESP_LOGW(TAG, "NimBLE stop failed: %d", rc);
    } else {
        ret = nimble_port_deinit();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "NimBLE deinit failed: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "BLE stack powered down");
        }
    }

    typewrt_ble_lock();
    if (!rc && ret == ESP_OK) {
        ble_stack_started = false;
        ble_synced = false;
        ble_advertising = false;
        ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ble_notify_enabled = false;
        ble_tx_handle = 0;
        typewrt_ble_release_awake_locked();
    }
    ble_stack_stopping = false;
    ble_shutdown_task_handle = NULL;
    typewrt_ble_unlock();

    vTaskDelete(NULL);
}

static void typewrt_ble_schedule_stack_shutdown(void)
{
    BaseType_t ok;

    typewrt_ble_lock();
    if (!ble_stack_started || ble_enabled || ble_shutdown_task_handle) {
        typewrt_ble_unlock();
        return;
    }
    ok = xTaskCreate(typewrt_ble_shutdown_task, "ble_off",
        TYPEWRT_BLE_SHUTDOWN_STACK, NULL, TYPEWRT_BLE_SHUTDOWN_PRIO,
        &ble_shutdown_task_handle);
    typewrt_ble_unlock();

    if (ok != pdPASS) {
        typewrt_ble_lock();
        ble_shutdown_task_handle = NULL;
        typewrt_ble_unlock();
        ESP_LOGW(TAG, "Failed to create BLE shutdown task");
    }
}

static int typewrt_ble_rx_write_locked(const uint8_t *data, size_t len)
{
    while (len) {
        ssize_t n = write(ble_receive_fd, data, len);

        if (n < 0) {
            return errno ? errno : EIO;
        }
        if (n == 0) {
            return EIO;
        }
        data += n;
        len -= (size_t)n;
    }
    return 0;
}

static int typewrt_ble_rx_start_file_locked(char *line)
{
    char *name;
    char *end;
    unsigned long size;
    char safe[TYPEWRT_BLE_RX_NAME_MAX];
    char msg[128];

    if (!line[0]) {
        return 0;
    }
    if (!strncmp(line, "TYPEWRT-END ", 12)) {
        return 0;
    }
    if (strncmp(line, "TYPEWRT-FILE ", 13)) {
        return typewrt_ble_rx_fail_locked(EINVAL);
    }
    line += 13;
    errno = 0;
    size = strtoul(line, &end, 10);
    if (errno || end == line || *end != ' ') {
        return typewrt_ble_rx_fail_locked(EINVAL);
    }
    name = end + 1;
    if (!typewrt_ble_rx_safe_name(name, safe, sizeof(safe))) {
        return typewrt_ble_rx_fail_locked(EINVAL);
    }
    if (!ble_receive_dir) {
        return typewrt_ble_rx_fail_locked(ENOENT);
    }
    ble_receive_path = typewrt_ble_rx_join_path(ble_receive_dir, safe);
    if (!ble_receive_path) {
        return typewrt_ble_rx_fail_locked(ENOMEM);
    }
    snprintf(ble_receive_name, sizeof(ble_receive_name), "%s", safe);
    ble_receive_expected = (size_t)size;
    ble_receive_received = 0;
    typewrt_sd_write_begin();
    ble_receive_write_started = true;
    ble_receive_fd = open(ble_receive_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (ble_receive_fd < 0) {
        return typewrt_ble_rx_fail_locked(errno ? errno : EIO);
    }
    ble_state = TYPEWRT_BLE_RECEIVING;
    snprintf(msg, sizeof(msg), "ble receiving %s", safe);
    typewrt_ble_set_msg_locked(msg);
    if (!ble_receive_expected) {
        return typewrt_ble_rx_finish_file_locked();
    }
    return 0;
}

static int typewrt_ble_receive_data_locked(const uint8_t *data, size_t len)
{
    while (len) {
        if (ble_receive_fd < 0) {
            const uint8_t *newline = memchr(data, '\n', len);
            size_t take = newline ? (size_t)(newline - data) : len;

            if (ble_receive_line_len + take >= sizeof(ble_receive_line)) {
                return typewrt_ble_rx_fail_locked(ENAMETOOLONG);
            }
            memcpy(ble_receive_line + ble_receive_line_len, data, take);
            ble_receive_line_len += take;
            data += take;
            len -= take;
            if (!newline) {
                return 0;
            }
            if (ble_receive_line_len &&
                    ble_receive_line[ble_receive_line_len - 1] == '\r') {
                ble_receive_line_len--;
            }
            ble_receive_line[ble_receive_line_len] = '\0';
            ble_receive_line_len = 0;
            int err = typewrt_ble_rx_start_file_locked(ble_receive_line);
            if (err) {
                return err;
            }
            data++;
            len--;
        } else {
            size_t remaining = ble_receive_expected - ble_receive_received;
            size_t take = remaining < len ? remaining : len;
            int err = typewrt_ble_rx_write_locked(data, take);

            if (err) {
                return typewrt_ble_rx_fail_locked(err);
            }
            ble_receive_received += take;
            data += take;
            len -= take;
            if (ble_receive_received == ble_receive_expected) {
                typewrt_ble_rx_finish_file_locked();
            }
        }
    }
    return 0;
}

static void typewrt_ble_stop_transport(bool terminate_connection)
{
    uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
    bool stop_adv = false;

    typewrt_ble_lock();
    ble_enabled = false;
    ble_mode = TYPEWRT_BLE_MODE_NONE;
    ble_transfer_cancel = true;
    if (!ble_transfer_task_handle) {
        typewrt_ble_free_pending_locked();
    }
    typewrt_ble_receive_reset_locked(true);
    free(ble_receive_dir);
    ble_receive_dir = NULL;
    ble_receive_completed_session = 0;
    if (ble_advertising) {
        ble_advertising = false;
        stop_adv = true;
    }
    if (terminate_connection && ble_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        conn_handle = ble_conn_handle;
    }
    ble_notify_enabled = false;
    ble_state = TYPEWRT_BLE_OFF;
    typewrt_ble_set_msg_locked("ble off");
    typewrt_ble_release_awake_locked();
    typewrt_ble_unlock();

    if (stop_adv) {
        (void)ble_gap_adv_stop();
    }
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    typewrt_ble_schedule_stack_shutdown();
}

static void typewrt_ble_tune_connection(uint16_t conn_handle)
{
    int rc;

#if CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT
    rc = ble_gap_set_prefered_le_phy(conn_handle,
        BLE_GAP_LE_PHY_2M_MASK, BLE_GAP_LE_PHY_2M_MASK,
        BLE_GAP_LE_PHY_CODED_ANY);
    if (rc) {
        ESP_LOGW(TAG, "2M PHY request failed: %d", rc);
    }

    rc = ble_gap_set_data_len(conn_handle,
        TYPEWRT_BLE_DLE_TX_OCTETS, TYPEWRT_BLE_DLE_TX_TIME_US);
    if (rc) {
        ESP_LOGW(TAG, "DLE request failed: %d", rc);
    }
#endif

    const struct ble_gap_upd_params params = {
        .itvl_min = 6,              /* 7.5 ms */
        .itvl_max = 12,             /* 15 ms */
        .latency = 0,
        .supervision_timeout = 400, /* 4 s */
        .min_ce_len = 0,
        .max_ce_len = 0,
    };
    rc = ble_gap_update_params(conn_handle, &params);

    if (rc && rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "Connection parameter update failed: %d", rc);
    }
}

void typewrt_ble_stop(void)
{
    typewrt_ble_stop_transport(true);
}

static size_t typewrt_ble_notify_chunk_size(void)
{
    uint16_t mtu;
    uint16_t conn_handle;

    typewrt_ble_lock();
    conn_handle = ble_conn_handle;
    typewrt_ble_unlock();

    if (conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return 20;
    }
    mtu = ble_att_mtu(conn_handle);
    if (mtu <= 3) {
        return 20;
    }
    mtu -= 3;
    return mtu > TYPEWRT_BLE_MAX_NOTIFY ? TYPEWRT_BLE_MAX_NOTIFY : mtu;
}

static int typewrt_ble_notify(const void *data, size_t len)
{
    const uint8_t *p = data;

    while (len) {
        size_t chunk_len = typewrt_ble_notify_chunk_size();
        int rc = BLE_HS_ENOMEM;

        if (chunk_len > len) {
            chunk_len = len;
        }

        for (int retry = 0; retry < TYPEWRT_BLE_NOTIFY_RETRIES; retry++) {
            struct os_mbuf *om;
            uint16_t conn_handle;
            bool notify_enabled;
            bool cancel;

            typewrt_ble_lock();
            conn_handle = ble_conn_handle;
            notify_enabled = ble_notify_enabled;
            cancel = ble_transfer_cancel || !ble_enabled;
            typewrt_ble_unlock();

            if (cancel) {
                return ECANCELED;
            }
            if (conn_handle == BLE_HS_CONN_HANDLE_NONE || !notify_enabled) {
                return ENOTCONN;
            }

            om = ble_hs_mbuf_from_flat(p, chunk_len);
            if (om) {
                rc = ble_gatts_notify_custom(conn_handle, ble_tx_handle, om);
            }
            if (rc == 0) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(TYPEWRT_BLE_RETRY_DELAY_MS));
        }
        if (rc) {
            return EIO;
        }

        p += chunk_len;
        len -= chunk_len;
        vTaskDelay(pdMS_TO_TICKS(TYPEWRT_BLE_NOTIFY_SETTLE_MS));
    }
    return 0;
}

static int typewrt_ble_transfer_file(int fd, size_t *sent)
{
    uint8_t buf[TYPEWRT_BLE_MAX_NOTIFY];

    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));

        if (n < 0) {
            return errno ? errno : EIO;
        }
        if (n == 0) {
            return 0;
        }
        int err = typewrt_ble_notify(buf, (size_t)n);
        if (err) {
            return err;
        }
        *sent += (size_t)n;
    }
}

static int typewrt_ble_transfer_buffer(const char *data, size_t len, size_t *sent)
{
    while (*sent < len) {
        size_t chunk_len = len - *sent;
        int err;

        if (chunk_len > TYPEWRT_BLE_MAX_NOTIFY) {
            chunk_len = TYPEWRT_BLE_MAX_NOTIFY;
        }
        err = typewrt_ble_notify(data + *sent, chunk_len);
        if (err) {
            return err;
        }
        *sent += chunk_len;
    }
    return 0;
}

static void typewrt_ble_finish_transfer(const char *msg, typewrt_ble_state_t state)
{
    bool stop_adv = false;
    uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;

    typewrt_ble_lock();
    ble_state = state;
    typewrt_ble_set_msg_locked(msg);
    ble_transfer_task_handle = NULL;
    ble_enabled = false;
    ble_mode = TYPEWRT_BLE_MODE_NONE;
    ble_transfer_cancel = false;
    if (ble_advertising) {
        ble_advertising = false;
        stop_adv = true;
    }
    if (ble_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        conn_handle = ble_conn_handle;
    }
    ble_notify_enabled = false;
    typewrt_ble_release_awake_locked();
    typewrt_ble_unlock();

    if (stop_adv) {
        (void)ble_gap_adv_stop();
    }
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    typewrt_ble_schedule_stack_shutdown();
}

static void typewrt_ble_transfer_task(void *arg)
{
    char *name;
    char *path;
    char *data;
    size_t len;
    bool is_buffer;
    size_t sent = 0;
    int fd = -1;
    int err = 0;
    char header[256];
    char footer[256];
    char msg[128];

    (void)arg;

    typewrt_ble_lock();
    name = ble_pending_name;
    path = ble_pending_path;
    data = ble_pending_data;
    len = ble_pending_len;
    is_buffer = ble_pending_is_buffer;
    ble_pending_name = NULL;
    ble_pending_path = NULL;
    ble_pending_data = NULL;
    ble_pending_len = 0;
    ble_pending_is_buffer = false;
    ble_state = TYPEWRT_BLE_SENDING;
    snprintf(ble_last_msg, sizeof(ble_last_msg), "ble sending %s", name);
    typewrt_ble_unlock();

    if (!is_buffer) {
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            err = errno ? errno : ENOENT;
        }
    }

    if (!err) {
        int n = snprintf(header, sizeof(header),
            "TYPEWRT-FILE %u %s\n", (unsigned)len, name);
        err = n > 0 ? typewrt_ble_notify(header, (size_t)n) : EIO;
    }
    if (!err) {
        if (is_buffer) {
            err = typewrt_ble_transfer_buffer(data, len, &sent);
        } else {
            err = typewrt_ble_transfer_file(fd, &sent);
        }
    }
    if (!err) {
        int n = snprintf(footer, sizeof(footer),
            "\nTYPEWRT-END %u %s\n", (unsigned)sent, name);
        err = n > 0 ? typewrt_ble_notify(footer, (size_t)n) : EIO;
    }

    if (fd >= 0) {
        close(fd);
    }
    free(name);
    free(path);
    free(data);

    if (err) {
        snprintf(msg, sizeof(msg), "ble send failed: %s", strerror(err));
        ESP_LOGW(TAG, "%s", msg);
        typewrt_ble_finish_transfer(msg, TYPEWRT_BLE_ERROR);
    } else {
        vTaskDelay(pdMS_TO_TICKS(TYPEWRT_BLE_DISCONNECT_DELAY_MS));
        snprintf(msg, sizeof(msg), "ble sent %u bytes", (unsigned)sent);
        ESP_LOGI(TAG, "%s", msg);
        typewrt_ble_finish_transfer(msg, TYPEWRT_BLE_DONE);
    }

    vTaskDelete(NULL);
}

static void typewrt_ble_try_start_transfer(void)
{
    typewrt_ble_lock();
    if (ble_enabled &&
            ble_mode == TYPEWRT_BLE_MODE_SEND &&
            ble_conn_handle != BLE_HS_CONN_HANDLE_NONE &&
            ble_notify_enabled &&
            ble_state == TYPEWRT_BLE_WAITING &&
            !ble_transfer_task_handle) {
        ble_state = TYPEWRT_BLE_SENDING;
        if (xTaskCreate(typewrt_ble_transfer_task, "ble_xfer",
                TYPEWRT_BLE_TRANSFER_STACK, NULL, TYPEWRT_BLE_TRANSFER_PRIO,
                &ble_transfer_task_handle) != pdPASS) {
            typewrt_ble_unlock();
            typewrt_ble_finish_transfer("ble task failed", TYPEWRT_BLE_ERROR);
            return;
        }
    }
    typewrt_ble_unlock();
}

static int typewrt_ble_access(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    char status[128];
    uint8_t *data;
    int len;
    int err;

    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        typewrt_ble_get_status(status, sizeof(status));
        return os_mbuf_append(ctxt->om, status, strlen(status)) == 0 ? 0 :
            BLE_ATT_ERR_INSUFFICIENT_RES;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        len = OS_MBUF_PKTLEN(ctxt->om);
        if (len <= 0) {
            return 0;
        }
        data = malloc((size_t)len);
        if (!data) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        if (ble_hs_mbuf_to_flat(ctxt->om, data, len, NULL)) {
            free(data);
            return BLE_ATT_ERR_UNLIKELY;
        }
        if (len == 3 && !memcmp(data, "off", 3)) {
            free(data);
            typewrt_ble_stop();
            return 0;
        }
        typewrt_ble_lock();
        if (ble_enabled && ble_mode == TYPEWRT_BLE_MODE_RECEIVE) {
            err = typewrt_ble_receive_data_locked(data, (size_t)len);
        } else {
            err = 0;
        }
        typewrt_ble_unlock();
        free(data);
        if (err) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        return 0;

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static const struct ble_gatt_svc_def typewrt_ble_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(TYPEWRT_BLE_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(TYPEWRT_BLE_TX_UUID),
                .access_cb = typewrt_ble_access,
                .val_handle = &ble_tx_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid = BLE_UUID16_DECLARE(TYPEWRT_BLE_RX_UUID),
                .access_cb = typewrt_ble_access,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            { 0 }
        },
    },
    { 0 }
};

static int typewrt_ble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        typewrt_ble_lock();
        ble_advertising = false;
        if (event->connect.status == 0) {
            ble_conn_handle = event->connect.conn_handle;
            typewrt_ble_hold_awake_locked();
            if (ble_mode == TYPEWRT_BLE_MODE_RECEIVE) {
                typewrt_ble_set_msg_locked("ble recv connected");
            } else {
                typewrt_ble_set_msg_locked("ble connected; subscribe to ffe1");
            }
        } else if (ble_enabled) {
            typewrt_ble_unlock();
            typewrt_ble_advertise();
            return 0;
        }
        typewrt_ble_unlock();
        if (event->connect.status == 0) {
            typewrt_ble_tune_connection(event->connect.conn_handle);
        }
        typewrt_ble_try_start_transfer();
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
    {
        bool shutdown_after_disconnect = false;

        typewrt_ble_lock();
        if (ble_conn_handle == event->disconnect.conn.conn_handle) {
            ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ble_notify_enabled = false;
            if (ble_enabled && ble_mode == TYPEWRT_BLE_MODE_RECEIVE) {
                unsigned count = ble_receive_completed_session;
                bool discard = ble_receive_fd >= 0;

                typewrt_ble_receive_reset_locked(discard);
                free(ble_receive_dir);
                ble_receive_dir = NULL;
                ble_enabled = false;
                ble_mode = TYPEWRT_BLE_MODE_NONE;
                ble_state = count ? TYPEWRT_BLE_DONE : TYPEWRT_BLE_ERROR;
                if (count == 1) {
                    typewrt_ble_set_msg_locked("ble received 1 file");
                } else if (count > 1) {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "ble received %u files", count);
                    typewrt_ble_set_msg_locked(msg);
                } else {
                    typewrt_ble_set_msg_locked("ble recv cancelled");
                }
                typewrt_ble_release_awake_locked();
                shutdown_after_disconnect = true;
            } else if (ble_enabled) {
                typewrt_ble_set_msg_locked(ble_mode == TYPEWRT_BLE_MODE_RECEIVE ?
                    "ble recv waiting" : "ble waiting for phone");
            }
        }
        typewrt_ble_unlock();
        if (shutdown_after_disconnect) {
            typewrt_ble_schedule_stack_shutdown();
            return 0;
        }
        typewrt_ble_advertise();
        return 0;
    }

    case BLE_GAP_EVENT_ADV_COMPLETE:
        typewrt_ble_lock();
        ble_advertising = false;
        typewrt_ble_unlock();
        typewrt_ble_advertise();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == ble_tx_handle) {
            typewrt_ble_lock();
            ble_notify_enabled = event->subscribe.cur_notify;
            if (ble_notify_enabled && ble_mode == TYPEWRT_BLE_MODE_SEND) {
                typewrt_ble_set_msg_locked("ble subscriber ready");
            }
            typewrt_ble_unlock();
            typewrt_ble_try_start_transfer();
        }
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated: conn=%u mtu=%u",
            event->mtu.conn_handle, event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
        ESP_LOGI(TAG, "PHY update: status=%d conn=%u tx=%u rx=%u",
            event->phy_updated.status, event->phy_updated.conn_handle,
            event->phy_updated.tx_phy, event->phy_updated.rx_phy);
        return 0;

    case BLE_GAP_EVENT_DATA_LEN_CHG:
        ESP_LOGI(TAG, "DLE update: conn=%u tx=%u/%u rx=%u/%u",
            event->data_len_chg.conn_handle,
            event->data_len_chg.max_tx_octets,
            event->data_len_chg.max_tx_time,
            event->data_len_chg.max_rx_octets,
            event->data_len_chg.max_rx_time);
        return 0;

    default:
        return 0;
    }
}

static void typewrt_ble_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;
    bool should_advertise;

    typewrt_ble_lock();
    should_advertise = ble_synced && ble_enabled && !ble_advertising &&
        ble_conn_handle == BLE_HS_CONN_HANDLE_NONE;
    typewrt_ble_unlock();
    if (!should_advertise) {
        return;
    }

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.name = (uint8_t *)TYPEWRT_BLE_DEVICE_NAME;
    fields.name_len = strlen(TYPEWRT_BLE_DEVICE_NAME);
    fields.name_is_complete = 1;
    fields.uuids16 = (ble_uuid16_t[]) {
        BLE_UUID16_INIT(TYPEWRT_BLE_SERVICE_UUID),
    };
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    rc = ble_gap_adv_set_fields(&fields);
    if (rc) {
        ESP_LOGW(TAG, "Failed to set advertising data: %d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(ble_own_addr_type, NULL, BLE_HS_FOREVER,
        &adv_params, typewrt_ble_gap_event, NULL);
    if (rc) {
        ESP_LOGW(TAG, "Failed to start advertising: %d", rc);
        return;
    }

    typewrt_ble_lock();
    ble_advertising = true;
    typewrt_ble_set_msg_locked(ble_mode == TYPEWRT_BLE_MODE_RECEIVE ?
        "ble recv waiting" : "ble waiting for phone");
    typewrt_ble_unlock();
}

static void typewrt_ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE reset: %d", reason);
}

static void typewrt_ble_on_sync(void)
{
    int rc;

    rc = ble_att_set_preferred_mtu(TYPEWRT_BLE_ATT_MTU);
    if (rc) {
        ESP_LOGW(TAG, "Preferred MTU setup failed: %d", rc);
    }

    rc = ble_hs_id_infer_auto(0, &ble_own_addr_type);
    if (rc) {
        ESP_LOGW(TAG, "Failed to infer BLE address: %d", rc);
        return;
    }

#if CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT
    rc = ble_gap_set_prefered_default_le_phy(
        BLE_GAP_LE_PHY_2M_MASK, BLE_GAP_LE_PHY_2M_MASK);
    if (rc) {
        ESP_LOGW(TAG, "Default 2M PHY preference failed: %d", rc);
    }
#endif

    typewrt_ble_lock();
    ble_synced = true;
    typewrt_ble_unlock();
    typewrt_ble_advertise();
}

static void typewrt_ble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static const char *typewrt_ble_init(void)
{
    esp_err_t ret;
    int rc;

    typewrt_ble_lock_prepare();
    typewrt_ble_lock();
    if (ble_stack_stopping) {
        typewrt_ble_unlock();
        return "ble stopping";
    }
    if (ble_stack_started) {
        typewrt_ble_unlock();
        return NULL;
    }
    typewrt_ble_unlock();

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
            ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return "ble nvs failed";
    }

    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NimBLE init failed: %s", esp_err_to_name(ret));
        return "ble init failed";
    }

    ble_hs_cfg.reset_cb = typewrt_ble_on_reset;
    ble_hs_cfg.sync_cb = typewrt_ble_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    rc = ble_gatts_count_cfg(typewrt_ble_gatt_svcs);
    if (rc) {
        return "ble gatt failed";
    }
    rc = ble_gatts_add_svcs(typewrt_ble_gatt_svcs);
    if (rc) {
        return "ble gatt failed";
    }
    rc = ble_svc_gap_device_name_set(TYPEWRT_BLE_DEVICE_NAME);
    if (rc) {
        return "ble name failed";
    }
    ble_store_config_init();
    nimble_port_freertos_init(typewrt_ble_host_task);

    typewrt_ble_lock();
    ble_stack_started = true;
    ble_state = TYPEWRT_BLE_IDLE;
    typewrt_ble_set_msg_locked("ble ready");
    typewrt_ble_unlock();
    return NULL;
}

static const char *typewrt_ble_queue_transfer(char *name, char *path,
    char *data, size_t len, bool is_buffer)
{
    const char *err = typewrt_ble_init();

    if (err) {
        goto fail;
    }

    typewrt_ble_lock();
    if (ble_transfer_task_handle ||
            (ble_enabled && ble_mode == TYPEWRT_BLE_MODE_RECEIVE)) {
        typewrt_ble_unlock();
        err = "ble busy";
        goto fail;
    }
    typewrt_ble_free_pending_locked();
    ble_pending_name = name;
    ble_pending_path = path;
    ble_pending_data = data;
    ble_pending_len = len;
    ble_pending_is_buffer = is_buffer;
    ble_transfer_cancel = false;
    ble_enabled = true;
    ble_mode = TYPEWRT_BLE_MODE_SEND;
    ble_state = TYPEWRT_BLE_WAITING;
    snprintf(ble_last_msg, sizeof(ble_last_msg),
        "ble waiting: %s", name);
    typewrt_ble_hold_awake_locked();
    typewrt_ble_unlock();

    typewrt_ble_advertise();
    typewrt_ble_try_start_transfer();
    return NULL;

fail:
    free(name);
    free(path);
    free(data);
    return err;
}

const char *typewrt_ble_send_file(const char *display_path, const char *fs_path)
{
    struct stat st;
    char *name = NULL;
    char *path = NULL;

    if (!fs_path || !*fs_path) {
        return "ble file missing";
    }
    if (stat(fs_path, &st) < 0 || !S_ISREG(st.st_mode)) {
        return "ble file not found";
    }
    name = typewrt_ble_strdup(typewrt_ble_basename(
        display_path && *display_path ? display_path : fs_path));
    path = typewrt_ble_strdup(fs_path);
    if (!name || !path) {
        free(name);
        free(path);
        return "ble memory failed";
    }
    return typewrt_ble_queue_transfer(name, path, NULL, (size_t)st.st_size,
        false);
}

const char *typewrt_ble_send_buffer(const char *name_arg, const char *data,
    size_t len)
{
    char *name = NULL;
    char *copy = NULL;

    name = typewrt_ble_strdup(typewrt_ble_basename(name_arg));
    if (!name) {
        return "ble memory failed";
    }
    copy = malloc(len ? len : 1);
    if (!copy) {
        free(name);
        return "ble memory failed";
    }
    if (len) {
        memcpy(copy, data, len);
    }
    return typewrt_ble_queue_transfer(name, NULL, copy, len, true);
}

const char *typewrt_ble_receive_dir(const char *dir)
{
    struct stat st;
    char *copy;
    const char *err = typewrt_ble_init();

    if (err) {
        return err;
    }
    if (!dir || !*dir) {
        return "ble directory missing";
    }
    if (stat(dir, &st) < 0 || !S_ISDIR(st.st_mode)) {
        return "ble directory not found";
    }
    copy = typewrt_ble_strdup(dir);
    if (!copy) {
        return "ble memory failed";
    }

    typewrt_ble_lock();
    if (ble_transfer_task_handle ||
            (ble_enabled && ble_mode == TYPEWRT_BLE_MODE_SEND) ||
            ble_receive_fd >= 0) {
        typewrt_ble_unlock();
        free(copy);
        return "ble busy";
    }
    typewrt_ble_receive_reset_locked(false);
    free(ble_receive_dir);
    ble_receive_dir = copy;
    ble_receive_completed_session = 0;
    ble_transfer_cancel = false;
    ble_enabled = true;
    ble_mode = TYPEWRT_BLE_MODE_RECEIVE;
    ble_state = TYPEWRT_BLE_WAITING;
    typewrt_ble_set_msg_locked("ble recv waiting");
    typewrt_ble_hold_awake_locked();
    typewrt_ble_unlock();

    typewrt_ble_advertise();
    return NULL;
}

void typewrt_ble_get_status(char *out, size_t out_len)
{
    if (!out_len) {
        return;
    }
    typewrt_ble_lock();
    snprintf(out, out_len, "%s", ble_last_msg);
    typewrt_ble_unlock();
}

unsigned typewrt_ble_status_generation(void)
{
    unsigned gen;

    typewrt_ble_lock_prepare();
    typewrt_ble_lock();
    gen = ble_status_generation;
    typewrt_ble_unlock();
    return gen;
}
