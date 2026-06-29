#include "flow_bridge.h"
#include "lvglui/eez-flow.h"

extern "C" int eez_get_global_variable_int(uint32_t index) {
    return eez::flow::getGlobalVariable(index).getInt();
}

extern "C" void eez_set_global_variable_int(uint32_t index, int value) {
    eez::flow::setGlobalVariable(index, eez::Value(value, eez::VALUE_TYPE_INT32));
}