#include "ui_internal.h"

static ui_trace_fn trace_fn;

void ui_set_trace(ui_trace_fn fn) {
    trace_fn = fn;
}

void ui_trace_emit(const char *message) {
    if (trace_fn && message) {
        trace_fn(message);
    }
}
