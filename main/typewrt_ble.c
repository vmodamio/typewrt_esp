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
#define TYPEWRT_BLE_MAX_NOTIFY 180
#define TYPEWRT_BLE_RETRY_DELAY_MS 15
#define TYPEWRT_BLE_NOTIFY_RETRIES 200
#define TYPEWRT_BLE_NOTIFY_SETTLE_MS 8
#define TYPEWRT_BLE_DISCONNECT_DELAY_MS 120
#define TYPEWRT_BLE_TRANSFER_STACK 4096
#define TYPEWRT_BLE_TRANSFER_PRIO 4

void ble_store_config_init(void);

typedef enum {
    TYPEWRT_BLE_OFF = 0,
    TYPEWRT_BLE_IDLE,
    TYPEWRT_BLE_WAITING,
    TYPEWRT_BLE_SENDING,
    TYPEWRT_BLE_DONE,
    TYPEWRT_BLE_ERROR,
} typewrt_ble_state_t;

static const char *TAG = "typewrt_ble";
static StaticSemaphore_t ble_mutex_storage;
static SemaphoreHandle_t ble_mutex;
static bool ble_stack_started;
static bool ble_enabled;
static bool ble_synced;
static bool ble_advertising;
static bool ble_sleep_locked;
static uint8_t ble_own_addr_type;
static uint16_t ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t ble_tx_handle;
static bool ble_notify_enabled;
static typewrt_ble_state_t ble_state = TYPEWRT_BLE_OFF;
static char ble_last_msg[128] = "ble off";

static TaskHandle_t ble_transfer_task_handle;
static bool ble_transfer_cancel;
static char *ble_pending_name;
static char *ble_pending_path;
static char *ble_pending_data;
static size_t ble_pending_len;
static bool ble_pending_is_buffer;

static void typewrt_ble_try_start_transfer(void);
static void typewrt_ble_advertise(void);

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

static void typewrt_ble_stop_transport(bool terminate_connection)
{
    uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
    bool stop_adv = false;

    typewrt_ble_lock();
    ble_enabled = false;
    ble_transfer_cancel = true;
    if (!ble_transfer_task_handle) {
        typewrt_ble_free_pending_locked();
    }
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

    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        typewrt_ble_get_status(status, sizeof(status));
        return os_mbuf_append(ctxt->om, status, strlen(status)) == 0 ? 0 :
            BLE_ATT_ERR_INSUFFICIENT_RES;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        if (OS_MBUF_PKTLEN(ctxt->om) >= 3) {
            char cmd[8] = {0};
            int len = OS_MBUF_PKTLEN(ctxt->om);
            if (len > (int)sizeof(cmd) - 1) {
                len = sizeof(cmd) - 1;
            }
            if (ble_hs_mbuf_to_flat(ctxt->om, cmd, len, NULL) == 0 &&
                    !strncmp(cmd, "off", 3)) {
                typewrt_ble_stop();
            }
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
            typewrt_ble_set_msg_locked("ble connected; subscribe to ffe1");
        } else if (ble_enabled) {
            typewrt_ble_unlock();
            typewrt_ble_advertise();
            return 0;
        }
        typewrt_ble_unlock();
        typewrt_ble_try_start_transfer();
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        typewrt_ble_lock();
        if (ble_conn_handle == event->disconnect.conn.conn_handle) {
            ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ble_notify_enabled = false;
            if (ble_enabled) {
                typewrt_ble_set_msg_locked("ble waiting for phone");
            }
        }
        typewrt_ble_unlock();
        typewrt_ble_advertise();
        return 0;

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
            if (ble_notify_enabled) {
                typewrt_ble_set_msg_locked("ble subscriber ready");
            }
            typewrt_ble_unlock();
            typewrt_ble_try_start_transfer();
        }
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
    typewrt_ble_set_msg_locked("ble waiting for phone");
    typewrt_ble_unlock();
}

static void typewrt_ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE reset: %d", reason);
}

static void typewrt_ble_on_sync(void)
{
    int rc;

    rc = ble_hs_id_infer_auto(0, &ble_own_addr_type);
    if (rc) {
        ESP_LOGW(TAG, "Failed to infer BLE address: %d", rc);
        return;
    }

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
    if (ble_transfer_task_handle) {
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

void typewrt_ble_get_status(char *out, size_t out_len)
{
    if (!out_len) {
        return;
    }
    typewrt_ble_lock();
    snprintf(out, out_len, "%s", ble_last_msg);
    typewrt_ble_unlock();
}
