#include "ui_internal.h"

#ifndef UI_NO_TRACE
static ui_trace_fn trace_fn;
#endif
static void (*service_fn)(void);

void ui_set_service(void (*fn)(void)) {
    service_fn = fn;
}

void ui_service(void) {
    if (service_fn) service_fn();
}

#ifndef UI_NO_TRACE
void ui_set_trace(ui_trace_fn fn) {
    trace_fn = fn;
}

void ui_trace_emit(const char *message) {
    if (trace_fn && message) {
        trace_fn(message);
    }
}
#else
void ui_set_trace(ui_trace_fn fn) {
    (void)fn;
}
#endif
