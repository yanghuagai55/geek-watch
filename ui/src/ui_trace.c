#include "ui_internal.h"

static ui_trace_fn trace_fn;
static void (*service_fn)(void);

void ui_set_service(void (*fn)(void)) {
    service_fn = fn;
}

void ui_service(void) {
    if (service_fn) service_fn();
}

void ui_set_trace(ui_trace_fn fn) {
    trace_fn = fn;
}

void ui_trace_emit(const char *message) {
    if (trace_fn && message) {
        trace_fn(message);
    }
}
