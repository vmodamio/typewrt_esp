#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "typewrt_board.h"
#include "typewrt_sdcard.h"

static const char *TAG = "typewrt_sdcard";
static sdmmc_card_t *sd_card;
static bool sd_card_mounted;

void typewrt_sdcard_spi_pins_prepare(void)
{
    gpio_config_t cs_conf = {
        .pin_bit_mask = 1ULL << TYPEWRT_PIN_SD_CS,
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&cs_conf);
    gpio_set_level(TYPEWRT_PIN_SD_CS, 1);

    gpio_set_pull_mode(TYPEWRT_PIN_SPI_MISO, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(TYPEWRT_PIN_SPI_MOSI, GPIO_PULLUP_ONLY);
}

static bool typewrt_sdcard_write_probe(void)
{
    const char *path = TYPEWRT_SD_MOUNT_POINT "/.typewrt_probe";
    const char probe_text[] = "typewrt sd probe\n";
    char readback[sizeof(probe_text)] = {0};

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        ESP_LOGE(TAG, "SD card write probe create failed: %s", strerror(errno));
        return false;
    }
    ssize_t written = write(fd, probe_text, sizeof(probe_text) - 1);
    if (written != sizeof(probe_text) - 1) {
        ESP_LOGE(TAG, "SD card write probe write failed: %s", strerror(errno));
        close(fd);
        return false;
    }
    close(fd);

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        ESP_LOGE(TAG, "SD card write probe reopen failed: %s", strerror(errno));
        return false;
    }
    ssize_t read_len = read(fd, readback, sizeof(probe_text) - 1);
    close(fd);
    unlink(path);
    if (read_len != sizeof(probe_text) - 1 ||
            memcmp(readback, probe_text, sizeof(probe_text) - 1) != 0) {
        ESP_LOGE(TAG, "SD card write probe readback mismatch");
        return false;
    }

    ESP_LOGI(TAG, "SD card write probe succeeded");
    return true;
}

bool typewrt_sdcard_init(void)
{
    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();

    host.slot = TYPEWRT_SPI_HOST;
    host.max_freq_khz = 10000;
    host.unaligned_multi_block_rw_max_chunk_size = 8;

    slot_config.gpio_cs = TYPEWRT_PIN_SD_CS;
    slot_config.host_id = host.slot;

    ESP_LOGI(TAG, "Mounting SD card at %s on SPI host %d, CS:%d",
        TYPEWRT_SD_MOUNT_POINT, host.slot, TYPEWRT_PIN_SD_CS);
    esp_err_t ret = esp_vfs_fat_sdspi_mount(TYPEWRT_SD_MOUNT_POINT,
        &host, &slot_config, &mount_config, &sd_card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount FAT filesystem on SD card");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SD card: %s", esp_err_to_name(ret));
        }
        return false;
    }

    sd_card_mounted = true;
    sdmmc_card_print_info(stdout, sd_card);
    setenv("PWD", TYPEWRT_SD_MOUNT_POINT, 1);
    typewrt_sdcard_write_probe();
    ESP_LOGI(TAG, "SD card mounted; Nextvi file paths are relative to %s",
        TYPEWRT_SD_MOUNT_POINT);
    return true;
}


bool typewrt_sdcard_is_mounted(void)
{
    return sd_card_mounted;
}

bool typewrt_sdcard_unmount_for_poweroff(void)
{
    esp_err_t ret;

    if (!sd_card_mounted) {
        return true;
    }
    ret = esp_vfs_fat_sdcard_unmount(TYPEWRT_SD_MOUNT_POINT, sd_card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to unmount SD card before power off: %s",
            esp_err_to_name(ret));
        return false;
    }
    sd_card_mounted = false;
    sd_card = NULL;
    ESP_LOGI(TAG, "SD card unmounted for power off");
    return true;
}
