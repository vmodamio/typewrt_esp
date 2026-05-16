#pragma once

#include <stddef.h>
#include <stdbool.h>

typedef struct {
	const char *display_path;
	const char *fs_path;
} typewrt_ble_file_request;

const char *typewrt_ble_send_file(const char *display_path, const char *fs_path);
const char *typewrt_ble_send_files(const typewrt_ble_file_request *files,
	size_t count);
const char *typewrt_ble_send_buffer(const char *name, const char *data, size_t len);
const char *typewrt_ble_receive_dir(const char *dir);
void typewrt_ble_get_status(char *out, size_t out_len);
unsigned typewrt_ble_status_generation(void);
void typewrt_ble_stop(void);
bool typewrt_ble_prepare_poweroff(void);
