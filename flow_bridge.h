#ifndef FLOW_BRIDGE_H
#define FLOW_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int  eez_get_global_variable_int(uint32_t index);
void eez_set_global_variable_int(uint32_t index, int value);

#ifdef __cplusplus
}
#endif

#endif