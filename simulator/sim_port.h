#pragma once

#include <stdint.h>

#include "ad_keys.h"

#ifdef __cplusplus
extern "C" {
#endif

void sim_port_set_score_path(const char *path);
void sim_port_advance_time(uint32_t ms);
void sim_port_inject_key(uint8_t key, ad_keys_event_type_t type);

#ifdef __cplusplus
}
#endif
