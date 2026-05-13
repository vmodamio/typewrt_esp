#pragma once

#include <stddef.h>
#include <stdbool.h>

const char *typewrt_ble_send_file(const char *display_path, const char *fs_path);
const char *typewrt_ble_send_buffer(const char *name, const char *data, size_t len);
void typewrt_ble_get_status(char *out, size_t out_len);
void typewrt_ble_stop(void);

