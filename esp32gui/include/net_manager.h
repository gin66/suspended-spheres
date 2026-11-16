#pragma once

#include <stdbool.h>

#include "ui_model.h"

#ifdef __cplusplus
extern "C" {
#endif

void net_manager_init(void);
void net_manager_setup(void);
void net_manager_forget(void);
void net_manager_scan(void);
void net_manager_service(bool on);
void net_manager_service_toggle(void);
void net_manager_github_check(void);
bool net_manager_service_on(void);
void net_manager_status(char *out, unsigned len);
/* Bench only: the setup AP key shown on the panel. */
void net_manager_ap_key(char *out, unsigned len);
int net_manager_scan_count(void);
const char *net_manager_scan_ssid(int index);
void net_manager_snapshot(ui_snapshot_t *snap);

#ifdef __cplusplus
}
#endif
