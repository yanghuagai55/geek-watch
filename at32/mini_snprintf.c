#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *buf;
    size_t cap;
    size_t pos;
} fmt_out_t;

static void fmt_putc(fmt_out_t *o, char ch) {
    if (o->pos + 1u < o->cap && o->buf) {
        o->buf[o->pos] = ch;
    }
    o->pos++;
}

static void fmt_pad(fmt_out_t *o, char ch, int count) {
    while (count-- > 0) {
        fmt_putc(o, ch);
    }
}

__attribute__((used))
int __wrap_snprintf(char *buf, size_t cap, const char *fmt, ...) {
    fmt_out_t o;
    va_list ap;

    o.buf = buf;
    o.cap = cap;
    o.pos = 0;
    va_start(ap, fmt);

    while (*fmt) {
        int left = 0, plus = 0, zero = 0, width = 0, prec = -1, is_long = 0;
        char spec;
        if (*fmt != '%') {
            fmt_putc(&o, *fmt++);
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            fmt_putc(&o, '%');
            fmt++;
            continue;
        }
        while (*fmt == '-' || *fmt == '+' || *fmt == '0') {
            if (*fmt == '-') left = 1;
            else if (*fmt == '+') plus = 1;
            else zero = 1;
            fmt++;
        }
        if (left) zero = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt++ - '0');
        }
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                prec = prec * 10 + (*fmt++ - '0');
            }
        }
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        }
        spec = *fmt ? *fmt++ : 0;
        if (!spec) break;

        if (spec == 's') {
            const char *s = va_arg(ap, const char *);
            int len = 0, pad;
            if (!s) s = "(null)";
            while (s[len] && (prec < 0 || len < prec)) len++;
            pad = width > len ? width - len : 0;
            if (!left) fmt_pad(&o, ' ', pad);
            while (len--) fmt_putc(&o, *s++);
            if (left) fmt_pad(&o, ' ', pad);
        } else if (spec == 'c') {
            char ch = (char)va_arg(ap, int);
            int pad = width > 1 ? width - 1 : 0;
            if (!left) fmt_pad(&o, ' ', pad);
            fmt_putc(&o, ch);
            if (left) fmt_pad(&o, ' ', pad);
        } else {
            char tmp[12];
            int n = 0;
            char sign = 0;
            uint32_t base = (spec == 'x' || spec == 'X') ? 16u : 10u;
            uint32_t uv;
            int tot, zpad, spad;
            if (spec == 'd' || spec == 'i') {
                long v = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
                if (v < 0) {
                    sign = '-';
                    uv = 0u - (uint32_t)v;
                } else {
                    if (plus) sign = '+';
                    uv = (uint32_t)v;
                }
            } else {
                uv = is_long ? (uint32_t)va_arg(ap, unsigned long)
                             : (uint32_t)va_arg(ap, unsigned int);
            }
            if (uv == 0u && prec != 0) {
                tmp[n++] = '0';
            } else {
                while (uv > 0u) {
                    uint32_t d = uv % base;
                    tmp[n++] = (char)(d < 10u ? ('0' + d)
                                              : ((spec == 'X' ? 'A' : 'a') + d - 10u));
                    uv /= base;
                }
            }
            zpad = (prec > n) ? (prec - n) : 0;
            tot = n + zpad + (sign ? 1 : 0);
            spad = width > tot ? width - tot : 0;
            if (!left && !zero) fmt_pad(&o, ' ', spad);
            if (sign) fmt_putc(&o, sign);
            if (!left && zero) fmt_pad(&o, '0', spad);
            fmt_pad(&o, '0', zpad);
            while (n--) fmt_putc(&o, tmp[n]);
            if (left) fmt_pad(&o, ' ', spad);
        }
    }
    va_end(ap);
    if (buf && cap > 0u) {
        buf[o.pos < cap ? o.pos : cap - 1u] = 0;
    }
    return (int)o.pos;
}

__attribute__((used))
int __wrap_atexit(void (*fn)(void)) {
    (void)fn;
    return 0;
}

__attribute__((used))
void __wrap_exit(int code) {
    (void)code;
    for (;;) {
    }
}

