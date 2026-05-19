#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"

#include "typewrt_ble.h"
#include "typewrt_board.h"
#include "typewrt_display.h"
#include "typewrt_keyboard.h"
#include "typewrt_power.h"
#include "typewrt_sdcard.h"

#define TYPEWRT_LED_BOOT_BLINKS 2
#define TYPEWRT_LED_BOOT_PULSE_US 250000
#define TYPEWRT_LED_SD_BLINK_PERIOD_US 75000
#define TYPEWRT_BATTERY_CHECK_HIGH_US (5ULL * 60ULL * 1000000ULL)
#define TYPEWRT_BATTERY_CHECK_MED_US (2ULL * 60ULL * 1000000ULL)
#define TYPEWRT_BATTERY_CHECK_LOW_US (60ULL * 1000000ULL)
#define TYPEWRT_BATTERY_WARN_LOW_REPEAT_US (5ULL * 60ULL * 1000000ULL)
#define TYPEWRT_BATTERY_WARN_CRIT_REPEAT_US (2ULL * 60ULL * 1000000ULL)
#define TYPEWRT_BATTERY_WARN_LOW_SOC_TENTHS 250
#define TYPEWRT_BATTERY_WARN_CRIT_SOC_TENTHS 100
#define TYPEWRT_BATTERY_WARN_PULSE_ON_US 150000
#define TYPEWRT_BATTERY_WARN_PULSE_GAP_US 850000
#define TYPEWRT_BATTERY_WARN_LOW_PULSES 3
#define TYPEWRT_BATTERY_WARN_CRIT_TOGGLE_US 100000
#define TYPEWRT_BATTERY_WARN_CRIT_DURATION_US 2000000
#define TYPEWRT_POWEROFF_SD_WAIT_MS 3000
#define TYPEWRT_POWEROFF_SD_POLL_MS 20
#define TYPEWRT_RTC_I2C_PORT I2C_NUM_0
#define TYPEWRT_RTC_I2C_FREQ_HZ 100000
#define PCF8523_I2C_ADDRESS 0x68
#define PCF8523_CONTROL_1_REG 0x00
#define PCF8523_CONTROL_1_STOP BIT(5)
#define PCF8523_TIME_REG 0x03
#define PCF8523_SECONDS_OS BIT(7)
#define MAX17048_I2C_ADDRESS 0x36
#define MAX17048_VCELL_REG 0x02
#define MAX17048_SOC_REG 0x04
#define MAX17048_CRATE_REG 0x16
#define MAX17048_ABSENT_SOC_TENTHS 1050
#define MAX17048_ABSENT_VOLTAGE_UV 4350000

static const char *TAG = "typewrt_power";
static i2c_master_bus_handle_t rtc_i2c_bus;
static i2c_master_dev_handle_t rtc_i2c_dev;
static i2c_master_dev_handle_t battery_i2c_dev;
static bool battery_gauge_available;
static esp_timer_handle_t boot_led_timer;
static esp_timer_handle_t battery_led_timer;
static esp_timer_handle_t sd_write_led_timer;
static volatile uint32_t typewrt_sleep_locks;
static volatile uint32_t boot_led_steps;
static volatile bool boot_led_on;
static volatile bool battery_led_active;
static volatile bool battery_led_locked;
static volatile bool battery_led_on;
static volatile uint32_t battery_led_steps;
static uint32_t battery_led_on_us;
static uint32_t battery_led_off_us;
static int64_t battery_next_check_us;
static int64_t battery_next_warning_us;
static volatile int64_t typewrt_ui_wakeup_deadline_us;
static uint8_t battery_warning_level;
static volatile uint32_t typewrt_sd_write_locks;
static volatile bool sd_write_led_on;

typedef struct {
    uint32_t voltage_uv;
    uint32_t soc_tenths;
    int32_t rate_tenths;
    bool absent;
} typewrt_battery_sample_t;

static void typewrt_power_led_update(void);
static void typewrt_led_boot_blink_start(void);
static void typewrt_battery_monitor_check(bool force);
static void typewrt_battery_monitor_start(void);
static void typewrt_timer_wakeup_prepare(void);
static void typewrt_usb_wakeup_prepare(void);
static void typewrt_unused_board_pins_init(void);
static void typewrt_unused_board_pins_poweroff(void);
static void typewrt_power_led_high_z(void);
static void typewrt_esp_domain_diagnostic_high_z(void);

static int bcd_to_dec(uint8_t value)
{
    return ((value >> 4) * 10) + (value & 0x0f);
}

static uint8_t dec_to_bcd(int value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static esp_err_t rtc_pcf8523_read_reg(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(rtc_i2c_dev, &reg, sizeof(reg),
        value, sizeof(*value), 1000);
}

static esp_err_t rtc_pcf8523_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};

    return i2c_master_transmit(rtc_i2c_dev, data, sizeof(data), 1000);
}

static esp_err_t rtc_pcf8523_clear_bits(uint8_t reg, uint8_t bits)
{
    uint8_t value;
    esp_err_t ret = rtc_pcf8523_read_reg(reg, &value);

    if (ret != ESP_OK) {
        return ret;
    }
    return rtc_pcf8523_write_reg(reg, value & ~bits);
}

static esp_err_t rtc_pcf8523_start_oscillator(void)
{
    return rtc_pcf8523_clear_bits(PCF8523_CONTROL_1_REG, PCF8523_CONTROL_1_STOP);
}

static esp_err_t rtc_pcf8523_write_time(const struct tm *rtc_tm)
{
    uint8_t data[8] = {
        PCF8523_TIME_REG,
        dec_to_bcd(rtc_tm->tm_sec),
        dec_to_bcd(rtc_tm->tm_min),
        dec_to_bcd(rtc_tm->tm_hour),
        dec_to_bcd(rtc_tm->tm_mday),
        dec_to_bcd(rtc_tm->tm_wday),
        dec_to_bcd(rtc_tm->tm_mon + 1),
        dec_to_bcd((rtc_tm->tm_year + 1900) % 100),
    };

    return i2c_master_transmit(rtc_i2c_dev, data, sizeof(data), 1000);
}

static bool rtc_time_is_valid(const struct tm *rtc_tm)
{
    int year = rtc_tm->tm_year + 1900;

    return year >= 2024 && year <= 2099 &&
        rtc_tm->tm_mon >= 0 && rtc_tm->tm_mon <= 11 &&
        rtc_tm->tm_mday >= 1 && rtc_tm->tm_mday <= 31 &&
        rtc_tm->tm_hour >= 0 && rtc_tm->tm_hour <= 23 &&
        rtc_tm->tm_min >= 0 && rtc_tm->tm_min <= 59 &&
        rtc_tm->tm_sec >= 0 && rtc_tm->tm_sec <= 59;
}

static bool rtc_pcf8523_read_regs(uint8_t data[7])
{
    uint8_t reg = PCF8523_TIME_REG;
    esp_err_t ret = i2c_master_transmit_receive(rtc_i2c_dev, &reg, sizeof(reg),
        data, 7, 1000);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PCF8523 read failed: %s", esp_err_to_name(ret));
        return false;
    }
    return true;
}

static void rtc_pcf8523_regs_to_tm(const uint8_t data[7], struct tm *rtc_tm)
{
    memset(rtc_tm, 0, sizeof(*rtc_tm));
    rtc_tm->tm_sec = bcd_to_dec(data[0] & 0x7f);
    rtc_tm->tm_min = bcd_to_dec(data[1] & 0x7f);
    rtc_tm->tm_hour = bcd_to_dec(data[2] & 0x3f);
    rtc_tm->tm_mday = bcd_to_dec(data[3] & 0x3f);
    rtc_tm->tm_wday = bcd_to_dec(data[4] & 0x07);
    rtc_tm->tm_mon = bcd_to_dec(data[5] & 0x1f) - 1;
    rtc_tm->tm_year = bcd_to_dec(data[6]) + 100;
    rtc_tm->tm_isdst = -1;
}

static bool rtc_pcf8523_read_time(struct tm *rtc_tm)
{
    uint8_t data[7];
    bool oscillator_stop;

    if (!rtc_pcf8523_read_regs(data)) {
        return false;
    }

    oscillator_stop = data[0] & PCF8523_SECONDS_OS;
    rtc_pcf8523_regs_to_tm(data, rtc_tm);

    if (!rtc_time_is_valid(rtc_tm)) {
        ESP_LOGW(TAG, "PCF8523 returned an invalid date/time");
        return false;
    }

    if (oscillator_stop) {
        esp_err_t ret = rtc_pcf8523_write_reg(PCF8523_TIME_REG,
            data[0] & ~PCF8523_SECONDS_OS);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "PCF8523 failed to clear oscillator-stop flag: %s",
                esp_err_to_name(ret));
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        if (!rtc_pcf8523_read_regs(data)) {
            return false;
        }
        if (data[0] & PCF8523_SECONDS_OS) {
            ESP_LOGW(TAG, "PCF8523 oscillator-stop flag is set; RTC time is not trusted");
            return false;
        }
        rtc_pcf8523_regs_to_tm(data, rtc_tm);
        if (!rtc_time_is_valid(rtc_tm)) {
            ESP_LOGW(TAG, "PCF8523 returned an invalid date/time after clearing OS flag");
            return false;
        }
        ESP_LOGI(TAG, "PCF8523 oscillator-stop flag was set and has been cleared");
    }

    return true;
}

static bool rtc_parse_datetime(const char *datetime, struct tm *rtc_tm)
{
    int year, month, day, hour, minute, second, used = 0;

    if (sscanf(datetime, "%d-%d-%d %d:%d:%d%n",
            &year, &month, &day, &hour, &minute, &second, &used) != 6 &&
        sscanf(datetime, "%d-%d-%d %d.%d.%d%n",
            &year, &month, &day, &hour, &minute, &second, &used) != 6 &&
        sscanf(datetime, "%d-%d-%d %d %d %d%n",
            &year, &month, &day, &hour, &minute, &second, &used) != 6) {
        return false;
    }
    while (isspace((unsigned char)datetime[used])) {
        used++;
    }
    if (datetime[used]) {
        return false;
    }

    memset(rtc_tm, 0, sizeof(*rtc_tm));
    rtc_tm->tm_year = year - 1900;
    rtc_tm->tm_mon = month - 1;
    rtc_tm->tm_mday = day;
    rtc_tm->tm_hour = hour;
    rtc_tm->tm_min = minute;
    rtc_tm->tm_sec = second;
    rtc_tm->tm_isdst = -1;
    if (!rtc_time_is_valid(rtc_tm)) {
        return false;
    }

    time_t normalized = mktime(rtc_tm);
    if (normalized == (time_t)-1) {
        return false;
    }
    return rtc_tm->tm_year == year - 1900 &&
        rtc_tm->tm_mon == month - 1 &&
        rtc_tm->tm_mday == day &&
        rtc_tm->tm_hour == hour &&
        rtc_tm->tm_min == minute &&
        rtc_tm->tm_sec == second;
}

static bool rtc_set_system_time(const struct tm *rtc_tm)
{
    struct tm tm_copy = *rtc_tm;
    time_t rtc_time = mktime(&tm_copy);
    if (rtc_time == (time_t)-1) {
        ESP_LOGW(TAG, "RTC time conversion failed");
        return false;
    }

    struct timeval tv = {
        .tv_sec = rtc_time,
        .tv_usec = 0,
    };
    if (settimeofday(&tv, NULL) != 0) {
        ESP_LOGW(TAG, "settimeofday from RTC failed: %s", strerror(errno));
        return false;
    }
    return true;
}

bool typewrt_rtc_get_datetime(char *out, size_t out_len)
{
    struct tm rtc_tm;

    if (!out || out_len == 0) {
        return false;
    }
    if (!rtc_i2c_dev || !rtc_pcf8523_read_time(&rtc_tm)) {
        time_t now = time(NULL);
        localtime_r(&now, &rtc_tm);
    }
    return strftime(out, out_len, "%d %b %Y  %H:%M", &rtc_tm) > 0;
}

const char *typewrt_rtc_set_datetime(const char *datetime, char *out, size_t out_len)
{
    struct tm rtc_tm;
    esp_err_t ret;

    if (!rtc_i2c_dev) {
        return "rtc unavailable";
    }
    if (!rtc_parse_datetime(datetime, &rtc_tm)) {
        return "rtc syntax: YYYY-MM-DD HH.MM.SS";
    }
    ret = rtc_pcf8523_start_oscillator();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PCF8523 start oscillator failed: %s", esp_err_to_name(ret));
        return "rtc start failed";
    }
    ret = rtc_pcf8523_write_time(&rtc_tm);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PCF8523 write time failed: %s", esp_err_to_name(ret));
        return "rtc write failed";
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    if (!rtc_pcf8523_read_time(&rtc_tm)) {
        return "rtc verify failed";
    }
    if (!rtc_set_system_time(&rtc_tm)) {
        return "system clock failed";
    }
    if (out && out_len) {
        typewrt_rtc_get_datetime(out, out_len);
    }
    return NULL;
}

bool typewrt_rtc_init(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = TYPEWRT_RTC_I2C_PORT,
        .sda_io_num = TYPEWRT_PIN_RTC_SDA,
        .scl_io_num = TYPEWRT_PIN_RTC_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&bus_config, &rtc_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2C init for RTC failed: %s", esp_err_to_name(ret));
        return false;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF8523_I2C_ADDRESS,
        .scl_speed_hz = TYPEWRT_RTC_I2C_FREQ_HZ,
    };
    ret = i2c_master_bus_add_device(rtc_i2c_bus, &dev_config, &rtc_i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PCF8523 add-device failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = i2c_master_probe(rtc_i2c_bus, PCF8523_I2C_ADDRESS, 1000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PCF8523 not found at I2C address 0x%02x: %s",
            PCF8523_I2C_ADDRESS, esp_err_to_name(ret));
        return false;
    }

    struct tm rtc_tm;
    if (!rtc_pcf8523_read_time(&rtc_tm)) {
        return false;
    }

    if (!rtc_set_system_time(&rtc_tm)) {
        return false;
    }

    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &rtc_tm);
    ESP_LOGI(TAG, "System clock set from PCF8523: %s", time_buf);
    return true;
}

void typewrt_rtc_i2c_power_pins_high_z(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << TYPEWRT_PIN_RTC_SDA) |
            (1ULL << TYPEWRT_PIN_RTC_SCL),
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };

    gpio_config(&io_conf);
}

void typewrt_shared_spi_power_pins_high_z(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << TYPEWRT_PIN_SPI_MOSI) |
            (1ULL << TYPEWRT_PIN_SPI_MISO) |
            (1ULL << TYPEWRT_PIN_SPI_CLK) |
            (1ULL << TYPEWRT_PIN_DISPLAY_CS) |
            (1ULL << TYPEWRT_PIN_SD_CS),
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };

    gpio_config(&io_conf);
}

static esp_err_t battery_max17048_read_word(uint8_t reg, uint16_t *value)
{
    uint8_t data[2];
    esp_err_t ret;

    if (!battery_i2c_dev || !value) {
        return ESP_ERR_INVALID_STATE;
    }
    ret = i2c_master_transmit_receive(battery_i2c_dev, &reg, sizeof(reg),
        data, sizeof(data), 1000);
    if (ret != ESP_OK) {
        return ret;
    }
    *value = ((uint16_t)data[0] << 8) | data[1];
    return ESP_OK;
}

static esp_err_t typewrt_battery_read_sample(typewrt_battery_sample_t *sample)
{
    uint16_t vcell_raw;
    uint16_t soc_raw;
    uint16_t crate_raw;
    int16_t crate;

    if (!sample) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!battery_gauge_available || !battery_i2c_dev) {
        return ESP_ERR_INVALID_STATE;
    }

    if (battery_max17048_read_word(MAX17048_VCELL_REG, &vcell_raw) != ESP_OK ||
            battery_max17048_read_word(MAX17048_SOC_REG, &soc_raw) != ESP_OK ||
            battery_max17048_read_word(MAX17048_CRATE_REG, &crate_raw) != ESP_OK) {
        return ESP_FAIL;
    }

    sample->voltage_uv = (uint32_t)(((uint64_t)vcell_raw * 78125U) / 1000U);
    sample->soc_tenths = ((uint32_t)soc_raw * 10U + 128U) / 256U;
    sample->absent = sample->soc_tenths > MAX17048_ABSENT_SOC_TENTHS ||
        sample->voltage_uv > MAX17048_ABSENT_VOLTAGE_UV;
    crate = (int16_t)crate_raw;
    sample->rate_tenths = ((int32_t)crate * 208 + (crate >= 0 ? 50 : -50)) / 100;
    return ESP_OK;
}

bool typewrt_battery_get_status(char *out, size_t out_len)
{
    typewrt_battery_sample_t sample;
    esp_err_t ret;
    uint32_t rate_abs_tenths;
    const char *state;
    const char *rate_sign;

    if (!out || out_len == 0) {
        return false;
    }
    typewrt_sleep_lock();
    ret = typewrt_battery_read_sample(&sample);
    if (ret == ESP_ERR_INVALID_STATE) {
        snprintf(out, out_len, "battery unavailable");
        typewrt_sleep_unlock();
        return false;
    }
    if (ret != ESP_OK) {
        snprintf(out, out_len, "battery read failed");
        typewrt_sleep_unlock();
        return false;
    }

    if (sample.absent) {
        snprintf(out, out_len,
            "bat absent %" PRIu32 ".%03" PRIu32 "V soc %" PRIu32 ".%" PRIu32 "%%",
            sample.voltage_uv / 1000000U, (sample.voltage_uv % 1000000U) / 1000U,
            sample.soc_tenths / 10U, sample.soc_tenths % 10U);
        typewrt_sleep_unlock();
        return true;
    }

    rate_abs_tenths = sample.rate_tenths < 0 ?
        (uint32_t)-sample.rate_tenths : (uint32_t)sample.rate_tenths;
    if (sample.rate_tenths > 0) {
        state = "chg";
    } else if (sample.rate_tenths < 0) {
        state = "dis";
    } else {
        state = "idle";
    }
    rate_sign = sample.rate_tenths < 0 ? "-" : sample.rate_tenths > 0 ? "+" : "";

    snprintf(out, out_len,
        "bat %" PRIu32 ".%" PRIu32 "%% %" PRIu32 ".%03" PRIu32
        "V %s %s%" PRIu32 ".%" PRIu32 "%%/h",
        sample.soc_tenths / 10U, sample.soc_tenths % 10U,
        sample.voltage_uv / 1000000U, (sample.voltage_uv % 1000000U) / 1000U,
        state, rate_sign, rate_abs_tenths / 10U, rate_abs_tenths % 10U);
    typewrt_sleep_unlock();
    return true;
}

bool typewrt_battery_init(void)
{
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MAX17048_I2C_ADDRESS,
        .scl_speed_hz = TYPEWRT_RTC_I2C_FREQ_HZ,
    };
    esp_err_t ret;
    char status[96];

    if (!rtc_i2c_bus) {
        ESP_LOGW(TAG, "MAX17048 skipped; I2C bus is unavailable");
        return false;
    }
    ret = i2c_master_bus_add_device(rtc_i2c_bus, &dev_config, &battery_i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MAX17048 add-device failed: %s", esp_err_to_name(ret));
        return false;
    }
    ret = i2c_master_probe(rtc_i2c_bus, MAX17048_I2C_ADDRESS, 1000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MAX17048 not found at I2C address 0x%02x: %s",
            MAX17048_I2C_ADDRESS, esp_err_to_name(ret));
        (void)i2c_master_bus_rm_device(battery_i2c_dev);
        battery_i2c_dev = NULL;
        return false;
    }

    battery_gauge_available = true;
    if (typewrt_battery_get_status(status, sizeof(status))) {
        ESP_LOGI(TAG, "MAX17048 %s", status);
    }
    typewrt_battery_monitor_start();
    return true;
}

void typewrt_sleep_lock(void)
{
    __atomic_add_fetch(&typewrt_sleep_locks, 1, __ATOMIC_RELAXED);
}

void typewrt_sleep_unlock(void)
{
    uint32_t locks = __atomic_load_n(&typewrt_sleep_locks, __ATOMIC_RELAXED);

    while (locks) {
        if (__atomic_compare_exchange_n(&typewrt_sleep_locks, &locks,
                locks - 1, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
            return;
        }
    }
}

void typewrt_sleep_set_ui_wakeup_us(uint64_t delay_us)
{
    int64_t deadline = esp_timer_get_time() + (int64_t)delay_us;

    __atomic_store_n(&typewrt_ui_wakeup_deadline_us, deadline,
        __ATOMIC_RELAXED);
}

void typewrt_sleep_clear_ui_wakeup(void)
{
    __atomic_store_n(&typewrt_ui_wakeup_deadline_us, 0, __ATOMIC_RELAXED);
}

static bool typewrt_sleep_is_locked(void)
{
    return __atomic_load_n(&typewrt_sleep_locks, __ATOMIC_RELAXED) != 0;
}

static bool typewrt_light_sleep_allowed(void)
{
    return !typewrt_sleep_is_locked() &&
        typewrt_keyboard_idle();
}

void typewrt_light_sleep_if_idle(void)
{
#if TYPEWRT_ENABLE_LIGHT_SLEEP
    esp_err_t ret;

    if (!typewrt_light_sleep_allowed()) {
        return;
    }
    typewrt_battery_monitor_check(false);
    if (!typewrt_light_sleep_allowed()) {
        return;
    }

    ret = typewrt_keyboard_stop_scanning();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Failed to stop keyboard scan timer before sleep: %s",
            esp_err_to_name(ret));
        return;
    }

    if (!typewrt_light_sleep_allowed()) {
        ESP_ERROR_CHECK(typewrt_keyboard_start_scanning());
        return;
    }

    typewrt_power_led_update();
    typewrt_usb_wakeup_prepare();
    typewrt_display_prepare_sleep();
    typewrt_timer_wakeup_prepare();
    typewrt_keyboard_prepare_wakeup_rows();
    ret = esp_light_sleep_start();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "light sleep failed: %s", esp_err_to_name(ret));
    }
    typewrt_power_led_update();
    typewrt_display_after_sleep();
    typewrt_battery_monitor_check(false);

    ESP_ERROR_CHECK(typewrt_keyboard_start_scanning());
#endif
}


bool typewrt_usb_power_present(void)
{
    return gpio_get_level(TYPEWRT_PIN_5V_EN) == 1;
}

static void typewrt_led_set(bool on)
{
    (void)gpio_hold_dis(TYPEWRT_PIN_LEDN);
    gpio_set_level(TYPEWRT_PIN_LEDN, on ? 0 : 1);
}

static void typewrt_power_led_high_z(void)
{
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << TYPEWRT_PIN_LEDN),
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };

    (void)gpio_hold_dis(TYPEWRT_PIN_LEDN);
    gpio_config(&led_conf);
}

static void typewrt_esp_domain_diagnostic_high_z(void)
{
    const uint64_t high_z_mask =
        (1ULL << TYPEWRT_PIN_RST_EN) |
        (1ULL << TYPEWRT_PIN_UART_TX) |
        (1ULL << TYPEWRT_PIN_UART_RX);
    gpio_config_t io_conf = {
        .pin_bit_mask = high_z_mask,
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };

    (void)uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(20));
    (void)gpio_hold_dis(TYPEWRT_PIN_RST_EN);
    gpio_config(&io_conf);
}

static void typewrt_power_led_update(void)
{
    if (__atomic_load_n(&typewrt_sd_write_locks, __ATOMIC_RELAXED) != 0 ||
            __atomic_load_n(&battery_led_active, __ATOMIC_RELAXED) ||
            __atomic_load_n(&boot_led_steps, __ATOMIC_RELAXED) != 0) {
        return;
    }
    typewrt_led_set(typewrt_usb_power_present());
    gpio_hold_en(TYPEWRT_PIN_LEDN);
}

static void typewrt_boot_led_cancel(void)
{
    __atomic_store_n(&boot_led_steps, 0, __ATOMIC_RELAXED);
    if (boot_led_timer) {
        esp_err_t ret = esp_timer_stop(boot_led_timer);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Failed to stop boot LED timer: %s",
                esp_err_to_name(ret));
        }
    }
}

static void typewrt_boot_led_timer_callback(void *arg)
{
    uint32_t steps;

    (void)arg;
    if (__atomic_load_n(&typewrt_sd_write_locks, __ATOMIC_RELAXED) != 0) {
        __atomic_store_n(&boot_led_steps, 0, __ATOMIC_RELAXED);
        return;
    }
    steps = __atomic_load_n(&boot_led_steps, __ATOMIC_RELAXED);
    if (!steps) {
        return;
    }

    boot_led_on = !boot_led_on;
    typewrt_led_set(boot_led_on);
    steps = __atomic_sub_fetch(&boot_led_steps, 1, __ATOMIC_RELAXED);
    if (!steps) {
        typewrt_power_led_update();
        return;
    }
    esp_err_t ret = esp_timer_start_once(boot_led_timer,
        TYPEWRT_LED_BOOT_PULSE_US);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to continue boot LED timer: %s",
            esp_err_to_name(ret));
        __atomic_store_n(&boot_led_steps, 0, __ATOMIC_RELAXED);
        typewrt_power_led_update();
    }
}

static bool typewrt_boot_led_timer_prepare(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = typewrt_boot_led_timer_callback,
        .name = "boot_led",
    };
    esp_err_t ret;

    if (boot_led_timer) {
        return true;
    }
    ret = esp_timer_create(&timer_args, &boot_led_timer);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to create boot LED timer: %s",
            esp_err_to_name(ret));
        return false;
    }
    return true;
}

static void typewrt_led_boot_blink_start(void)
{
    esp_err_t ret;

    if (!typewrt_boot_led_timer_prepare()) {
        return;
    }
    ret = esp_timer_stop(boot_led_timer);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Failed to reset boot LED timer: %s",
            esp_err_to_name(ret));
    }

    boot_led_on = !typewrt_usb_power_present();
    __atomic_store_n(&boot_led_steps, TYPEWRT_LED_BOOT_BLINKS * 2 - 1,
        __ATOMIC_RELAXED);
    typewrt_led_set(boot_led_on);
    ret = esp_timer_start_once(boot_led_timer, TYPEWRT_LED_BOOT_PULSE_US);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start boot LED timer: %s",
            esp_err_to_name(ret));
        __atomic_store_n(&boot_led_steps, 0, __ATOMIC_RELAXED);
        typewrt_power_led_update();
    }
}

static void typewrt_battery_led_finish(void)
{
    bool locked = __atomic_exchange_n(&battery_led_locked, false,
        __ATOMIC_RELAXED);

    __atomic_store_n(&battery_led_active, false, __ATOMIC_RELAXED);
    __atomic_store_n(&battery_led_steps, 0, __ATOMIC_RELAXED);
    typewrt_power_led_update();
    if (locked) {
        typewrt_sleep_unlock();
    }
}

static void typewrt_battery_led_cancel(void)
{
    if (battery_led_timer) {
        esp_err_t ret = esp_timer_stop(battery_led_timer);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Failed to stop battery LED timer: %s",
                esp_err_to_name(ret));
        }
    }
    typewrt_battery_led_finish();
}

static void typewrt_battery_led_timer_callback(void *arg)
{
    uint32_t steps;

    (void)arg;
    if (__atomic_load_n(&typewrt_sd_write_locks, __ATOMIC_RELAXED) != 0) {
        typewrt_battery_led_finish();
        return;
    }
    steps = __atomic_load_n(&battery_led_steps, __ATOMIC_RELAXED);
    if (!__atomic_load_n(&battery_led_active, __ATOMIC_RELAXED) || !steps) {
        typewrt_battery_led_finish();
        return;
    }

    if (battery_led_off_us) {
        if (battery_led_on) {
            battery_led_on = false;
            typewrt_led_set(false);
            if (__atomic_sub_fetch(&battery_led_steps, 1, __ATOMIC_RELAXED) == 0) {
                typewrt_battery_led_finish();
                return;
            }
            esp_timer_start_once(battery_led_timer, battery_led_off_us);
            return;
        }
        battery_led_on = true;
        typewrt_led_set(true);
        esp_timer_start_once(battery_led_timer, battery_led_on_us);
        return;
    }

    if (__atomic_sub_fetch(&battery_led_steps, 1, __ATOMIC_RELAXED) == 0) {
        typewrt_battery_led_finish();
        return;
    }
    battery_led_on = !battery_led_on;
    typewrt_led_set(battery_led_on);
    esp_timer_start_once(battery_led_timer, battery_led_on_us);
}

static bool typewrt_battery_led_timer_prepare(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = typewrt_battery_led_timer_callback,
        .name = "bat_led",
    };
    esp_err_t ret;

    if (battery_led_timer) {
        return true;
    }
    ret = esp_timer_create(&timer_args, &battery_led_timer);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to create battery LED timer: %s",
            esp_err_to_name(ret));
        return false;
    }
    return true;
}

static void typewrt_battery_led_start_low(void)
{
    if (!typewrt_battery_led_timer_prepare()) {
        return;
    }
    typewrt_battery_led_cancel();
    typewrt_boot_led_cancel();
    typewrt_sleep_lock();
    __atomic_store_n(&battery_led_locked, true, __ATOMIC_RELAXED);
    __atomic_store_n(&battery_led_active, true, __ATOMIC_RELAXED);
    __atomic_store_n(&battery_led_steps, TYPEWRT_BATTERY_WARN_LOW_PULSES,
        __ATOMIC_RELAXED);
    battery_led_on_us = TYPEWRT_BATTERY_WARN_PULSE_ON_US;
    battery_led_off_us = TYPEWRT_BATTERY_WARN_PULSE_GAP_US;
    battery_led_on = true;
    typewrt_led_set(true);
    esp_timer_start_once(battery_led_timer, battery_led_on_us);
}

static void typewrt_battery_led_start_critical(void)
{
    if (!typewrt_battery_led_timer_prepare()) {
        return;
    }
    typewrt_battery_led_cancel();
    typewrt_boot_led_cancel();
    typewrt_sleep_lock();
    __atomic_store_n(&battery_led_locked, true, __ATOMIC_RELAXED);
    __atomic_store_n(&battery_led_active, true, __ATOMIC_RELAXED);
    __atomic_store_n(&battery_led_steps,
        TYPEWRT_BATTERY_WARN_CRIT_DURATION_US / TYPEWRT_BATTERY_WARN_CRIT_TOGGLE_US,
        __ATOMIC_RELAXED);
    battery_led_on_us = TYPEWRT_BATTERY_WARN_CRIT_TOGGLE_US;
    battery_led_off_us = 0;
    battery_led_on = true;
    typewrt_led_set(true);
    esp_timer_start_once(battery_led_timer, battery_led_on_us);
}

static void typewrt_sd_write_led_timer_callback(void *arg)
{
    (void)arg;

    if (__atomic_load_n(&typewrt_sd_write_locks, __ATOMIC_RELAXED) == 0) {
        return;
    }
    sd_write_led_on = !sd_write_led_on;
    typewrt_led_set(sd_write_led_on);
}

static bool typewrt_sd_write_led_timer_prepare(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = typewrt_sd_write_led_timer_callback,
        .name = "sd_led",
    };
    esp_err_t ret;

    if (sd_write_led_timer) {
        return true;
    }
    ret = esp_timer_create(&timer_args, &sd_write_led_timer);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to create SD write LED timer: %s",
            esp_err_to_name(ret));
        return false;
    }
    return true;
}

void typewrt_sd_write_begin(void)
{
    uint32_t locks;
    esp_err_t ret;

    typewrt_sleep_lock();
    typewrt_boot_led_cancel();
    typewrt_battery_led_cancel();
    locks = __atomic_add_fetch(&typewrt_sd_write_locks, 1, __ATOMIC_RELAXED);
    if (locks != 1) {
        return;
    }
    if (!typewrt_sd_write_led_timer_prepare()) {
        return;
    }

    sd_write_led_on = true;
    typewrt_led_set(true);
    ret = esp_timer_stop(sd_write_led_timer);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Failed to reset SD write LED timer: %s",
            esp_err_to_name(ret));
    }
    ret = esp_timer_start_periodic(sd_write_led_timer,
        TYPEWRT_LED_SD_BLINK_PERIOD_US);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start SD write LED timer: %s",
            esp_err_to_name(ret));
    }
}

void typewrt_sd_write_end(void)
{
    uint32_t locks = __atomic_load_n(&typewrt_sd_write_locks,
        __ATOMIC_RELAXED);

    while (locks) {
        if (__atomic_compare_exchange_n(&typewrt_sd_write_locks, &locks,
                locks - 1, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
            if (locks == 1) {
                if (sd_write_led_timer) {
                    esp_err_t ret = esp_timer_stop(sd_write_led_timer);
                    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
                        ESP_LOGW(TAG, "Failed to stop SD write LED timer: %s",
                            esp_err_to_name(ret));
                    }
                }
                typewrt_power_led_update();
            }
            typewrt_sleep_unlock();
            return;
        }
    }
}

size_t typewrt_heap_free_bytes(void)
{
    return heap_caps_get_free_size(MALLOC_CAP_8BIT);
}

size_t typewrt_heap_largest_free_block(void)
{
    return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}

static bool typewrt_sd_writes_idle(TickType_t timeout_ticks)
{
    TickType_t start = xTaskGetTickCount();
    TickType_t poll = pdMS_TO_TICKS(TYPEWRT_POWEROFF_SD_POLL_MS);

    if (!poll) {
        poll = 1;
    }
    while (__atomic_load_n(&typewrt_sd_write_locks,
            __ATOMIC_RELAXED) != 0) {
        if (xTaskGetTickCount() - start >= timeout_ticks) {
            ESP_LOGW(TAG, "Timed out waiting for SD writes to finish");
            return false;
        }
        vTaskDelay(poll);
    }
    return true;
}

static uint64_t typewrt_battery_check_interval_us(uint32_t soc_tenths)
{
    if (soc_tenths >= 500) {
        return TYPEWRT_BATTERY_CHECK_HIGH_US;
    }
    if (soc_tenths >= TYPEWRT_BATTERY_WARN_LOW_SOC_TENTHS) {
        return TYPEWRT_BATTERY_CHECK_MED_US;
    }
    return TYPEWRT_BATTERY_CHECK_LOW_US;
}

static void typewrt_battery_schedule_next_check(int64_t now, uint64_t delay_us)
{
    battery_next_check_us = now + (int64_t)delay_us;
}

static void typewrt_battery_monitor_check(bool force)
{
    typewrt_battery_sample_t sample;
    int64_t now = esp_timer_get_time();
    uint8_t warning_level = 0;
    uint64_t warning_repeat_us = 0;
    bool discharging;
    esp_err_t ret;

    if (!force && battery_next_check_us && now < battery_next_check_us) {
        return;
    }
    typewrt_sleep_lock();
    ret = typewrt_battery_read_sample(&sample);
    typewrt_sleep_unlock();
    if (ret != ESP_OK || sample.absent) {
        battery_warning_level = 0;
        battery_next_warning_us = 0;
        typewrt_battery_schedule_next_check(now, TYPEWRT_BATTERY_CHECK_HIGH_US);
        return;
    }

    typewrt_battery_schedule_next_check(now,
        typewrt_battery_check_interval_us(sample.soc_tenths));
    discharging = !typewrt_usb_power_present() && sample.rate_tenths <= 0;
    if (!discharging) {
        battery_warning_level = 0;
        battery_next_warning_us = 0;
        return;
    }

    if (sample.soc_tenths < TYPEWRT_BATTERY_WARN_CRIT_SOC_TENTHS) {
        warning_level = 2;
        warning_repeat_us = TYPEWRT_BATTERY_WARN_CRIT_REPEAT_US;
    } else if (sample.soc_tenths < TYPEWRT_BATTERY_WARN_LOW_SOC_TENTHS) {
        warning_level = 1;
        warning_repeat_us = TYPEWRT_BATTERY_WARN_LOW_REPEAT_US;
    } else {
        battery_warning_level = 0;
        battery_next_warning_us = 0;
        return;
    }

    if (warning_level > battery_warning_level || !battery_next_warning_us ||
            now >= battery_next_warning_us) {
        if (warning_level == 2) {
            typewrt_battery_led_start_critical();
        } else {
            typewrt_battery_led_start_low();
        }
        battery_next_warning_us = now + (int64_t)warning_repeat_us;
    }
    battery_warning_level = warning_level;
}

static void typewrt_timer_wakeup_prepare(void)
{
    int64_t now;
    uint64_t delay_us = 0;
    int64_t ui_deadline;
    bool have_timer = false;

    now = esp_timer_get_time();
    if (battery_gauge_available && battery_i2c_dev && battery_next_check_us) {
        delay_us = battery_next_check_us > now ?
            (uint64_t)(battery_next_check_us - now) : 1;
        have_timer = true;
    }
    ui_deadline = __atomic_load_n(&typewrt_ui_wakeup_deadline_us,
        __ATOMIC_RELAXED);
    if (ui_deadline > 0) {
        uint64_t ui_delay_us = ui_deadline > now ?
            (uint64_t)(ui_deadline - now) : 1;
        if (!have_timer || ui_delay_us < delay_us) {
            delay_us = ui_delay_us;
        }
        have_timer = true;
    }
    if (!have_timer) {
        (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
        return;
    }
    esp_sleep_enable_timer_wakeup(delay_us);
}

static void typewrt_battery_monitor_start(void)
{
    battery_next_check_us = 0;
    battery_next_warning_us = 0;
    battery_warning_level = 0;
    typewrt_battery_monitor_check(true);
}

void typewrt_reset_button_enable(bool enabled)
{
    (void)gpio_hold_dis(TYPEWRT_PIN_RST_EN);
    gpio_set_level(TYPEWRT_PIN_RST_EN, enabled ? 0 : 1);
    gpio_hold_en(TYPEWRT_PIN_RST_EN);
}

static void typewrt_usb_wakeup_prepare(void)
{
    (void)gpio_wakeup_disable(TYPEWRT_PIN_5V_EN);
    gpio_wakeup_enable(TYPEWRT_PIN_5V_EN,
        typewrt_usb_power_present() ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
}

static void typewrt_unused_board_pins_init(void)
{
    const uint64_t output_low_mask =
        (1ULL << TYPEWRT_PIN_BLUE_LED) |
        (1ULL << TYPEWRT_PIN_RGB_LED_DATA);

    (void)gpio_hold_dis(TYPEWRT_PIN_BLUE_LED);
    (void)gpio_hold_dis(TYPEWRT_PIN_RGB_LED_DATA);

    gpio_config_t output_conf = {
        .pin_bit_mask = output_low_mask,
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&output_conf);
    gpio_set_level(TYPEWRT_PIN_BLUE_LED, 0);
    gpio_set_level(TYPEWRT_PIN_RGB_LED_DATA, 0);
    gpio_hold_en(TYPEWRT_PIN_BLUE_LED);
    gpio_hold_en(TYPEWRT_PIN_RGB_LED_DATA);

    gpio_config_t input_conf = {
        .pin_bit_mask = (1ULL << TYPEWRT_PIN_LIGHT_SENSOR),
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&input_conf);
}

static void typewrt_unused_board_pins_poweroff(void)
{
    const uint64_t high_z_mask =
        (1ULL << TYPEWRT_PIN_LIGHT_SENSOR) |
        (1ULL << TYPEWRT_PIN_5V_EN);

    typewrt_unused_board_pins_init();

    gpio_config_t io_conf = {
        .pin_bit_mask = high_z_mask,
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_conf);
}

static void typewrt_power_domain_pins_high_z(void)
{
    typewrt_rtc_i2c_power_pins_high_z();
    typewrt_shared_spi_power_pins_high_z();
    typewrt_keyboard_power_pins_high_z();
}

bool typewrt_power_off(void)
{
    typewrt_sleep_lock();

    if (!typewrt_ble_prepare_poweroff() ||
            !typewrt_sd_writes_idle(pdMS_TO_TICKS(TYPEWRT_POWEROFF_SD_WAIT_MS)) ||
            !typewrt_sdcard_unmount_for_poweroff()) {
        typewrt_sleep_unlock();
        return false;
    }

    typewrt_display_stop_for_poweroff();
    (void)typewrt_keyboard_stop_scanning();

    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    (void)gpio_wakeup_disable(TYPEWRT_PIN_5V_EN);
    typewrt_keyboard_disable_wakeup();
    typewrt_unused_board_pins_poweroff();

    typewrt_power_led_high_z();

    typewrt_power_domain_pins_high_z();
    typewrt_esp_domain_diagnostic_high_z();
    (void)esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_OFF);

    (void)gpio_hold_dis(TYPEWRT_PIN_LDO2_EN);
    gpio_set_level(TYPEWRT_PIN_LDO2_EN, 0);
    gpio_hold_en(TYPEWRT_PIN_LDO2_EN);
    gpio_deep_sleep_hold_en();

    esp_deep_sleep_start();
    return true;
}

void typewrt_power_init(void)
{
    gpio_deep_sleep_hold_dis();
    (void)gpio_hold_dis(TYPEWRT_PIN_LDO2_EN);
    (void)gpio_hold_dis(TYPEWRT_PIN_LEDN);
    (void)gpio_hold_dis(TYPEWRT_PIN_RST_EN);
    (void)gpio_hold_dis(TYPEWRT_PIN_BLUE_LED);
    (void)gpio_hold_dis(TYPEWRT_PIN_RGB_LED_DATA);

    gpio_config_t power_conf = {
        .pin_bit_mask = (1ULL << TYPEWRT_PIN_LDO2_EN),
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&power_conf);
    gpio_set_level(TYPEWRT_PIN_LDO2_EN, 1);
    gpio_hold_en(TYPEWRT_PIN_LDO2_EN);

    gpio_config_t usb_conf = {
        .pin_bit_mask = (1ULL << TYPEWRT_PIN_5V_EN),
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&usb_conf);
    typewrt_unused_board_pins_init();

    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << TYPEWRT_PIN_LEDN ) | (1ULL << TYPEWRT_PIN_RST_EN),
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&led_conf);
    gpio_set_level(TYPEWRT_PIN_LEDN, 1);
    typewrt_power_led_update();
    typewrt_reset_button_enable(true);
    typewrt_led_boot_blink_start();
}
