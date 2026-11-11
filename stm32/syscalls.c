#include <errno.h>
#include <stddef.h>

void *_sbrk(ptrdiff_t inc) {
    extern char _end;
    static char *brk;
    char *prev;

    if (!brk) {
        brk = &_end;
    }
    prev = brk;
    brk += inc;
    return prev;
}

int _write(int fd, const void *buf, unsigned len) {
    (void)fd;
    (void)buf;
    return (int)len;
}

void _exit(int code) {
    (void)code;
    for (;;) {
    }
}
