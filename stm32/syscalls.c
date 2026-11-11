#include <errno.h>
#include <stddef.h>

void *_sbrk(ptrdiff_t inc) {
    extern char _end, _heap_end;
    static char *brk;
    char *prev;

    if (!brk) {
        brk = &_end;
    }
    if (inc > &_heap_end - brk || inc < &_end - brk) {
        errno = ENOMEM;
        return (void *)-1;
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
