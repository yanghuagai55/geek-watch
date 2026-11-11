/* 从串口收手表回传的画面，存成 BMP。
   用法：recv_shot.exe COM5 host\obj
   帧：A5 11 FF 长度3字节 宽 高 RGB565 异或。 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static HANDLE open_port(const char *name) {
    char path[32];
    HANDLE h;
    DCB dcb;
    COMMTIMEOUTS to;

    snprintf(path, sizeof path, "\\\\.\\%s", name);
    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return h;
    }
    memset(&dcb, 0, sizeof dcb);
    dcb.DCBlength = sizeof dcb;
    GetCommState(h, &dcb);
    dcb.BaudRate = 115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    SetCommState(h, &dcb);
    to.ReadIntervalTimeout = 50;
    to.ReadTotalTimeoutConstant = 200;
    to.ReadTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant = 200;
    to.WriteTotalTimeoutMultiplier = 0;
    SetCommTimeouts(h, &to);
    return h;
}

static int read_byte(HANDLE h, uint8_t *b) {
    DWORD n = 0;
    if (!ReadFile(h, b, 1, &n, NULL) || n != 1) {
        return 0;
    }
    return 1;
}

static int save_bmp(const char *path, int w, int h, const uint8_t *rgb565) {
    FILE *fp;
    uint8_t header[54];
    int x, y;
    int row = (w * 3 + 3) & ~3;
    uint8_t *line = (uint8_t *)malloc((size_t)row);

    if (!line) {
        return 0;
    }
    fp = fopen(path, "wb");
    if (!fp) {
        free(line);
        return 0;
    }
    memset(header, 0, sizeof header);
    header[0] = 'B';
    header[1] = 'M';
    *(uint32_t *)(header + 2) = 54u + (uint32_t)row * (uint32_t)h;
    *(uint32_t *)(header + 10) = 54;
    *(uint32_t *)(header + 14) = 40;
    *(int32_t *)(header + 18) = w;
    *(int32_t *)(header + 22) = h;
    *(uint16_t *)(header + 26) = 1;
    *(uint16_t *)(header + 28) = 24;
    fwrite(header, 1, 54, fp);
    for (y = h - 1; y >= 0; y--) {
        memset(line, 0, (size_t)row);
        for (x = 0; x < w; x++) {
            int i = (y * w + x) * 2;
            uint16_t p = (uint16_t)((rgb565[i] << 8) | rgb565[i + 1]);
            int r = (p >> 11) & 31;
            int g = (p >> 5) & 63;
            int b = p & 31;
            line[x * 3 + 0] = (uint8_t)((b << 3) | (b >> 2));
            line[x * 3 + 1] = (uint8_t)((g << 2) | (g >> 4));
            line[x * 3 + 2] = (uint8_t)((r << 3) | (r >> 2));
        }
        fwrite(line, 1, (size_t)row, fp);
    }
    fclose(fp);
    free(line);
    return 1;
}

int main(int argc, char **argv) {
    const char *port = argc > 1 ? argv[1] : "COM5";
    const char *dir = argc > 2 ? argv[2] : "host/obj";
    HANDLE h = open_port(port);
    int shots = 0;
    int idle = 0;
    uint8_t win[3] = {0, 0, 0};

    if (h == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "cannot open %s\n", port);
        return 1;
    }
    printf("listen %s\n", port);
    while (shots < 4 && idle < 400) {
        uint8_t b;
        if (!read_byte(h, &b)) {
            idle++;
            continue;
        }
        idle = 0;
        win[0] = win[1];
        win[1] = win[2];
        win[2] = b;
        if (win[0] == 0xA5 && win[1] == 0x11 && win[2] == 0xFF) {
            uint8_t lenb[3];
            uint32_t total;
            int w, ht, i;
            uint8_t *pix;
            uint8_t xor_calc = (uint8_t)(0xA5 ^ 0x11 ^ 0xFF);
            uint8_t xor_got = 0;
            char path[260];
            if (!read_byte(h, &lenb[0]) || !read_byte(h, &lenb[1]) || !read_byte(h, &lenb[2])) {
                break;
            }
            xor_calc ^= lenb[0] ^ lenb[1] ^ lenb[2];
            total = (uint32_t)lenb[0] | ((uint32_t)lenb[1] << 8) | ((uint32_t)lenb[2] << 16);
            if (total < 4 || total > 128u * 160u * 2u + 4u) {
                fprintf(stderr, "bad length %lu\n", (unsigned long)total);
                continue;
            }
            pix = (uint8_t *)malloc(total);
            if (!pix) {
                break;
            }
            for (i = 0; i < (int)total; i++) {
                if (!read_byte(h, &pix[i])) {
                    free(pix);
                    pix = 0;
                    break;
                }
                xor_calc ^= pix[i];
            }
            if (!pix || !read_byte(h, &xor_got)) {
                free(pix);
                break;
            }
            if (xor_got != xor_calc) {
                fprintf(stderr, "xor %02X want %02X\n", xor_got, xor_calc);
                free(pix);
                continue;
            }
            w = pix[0] | (pix[1] << 8);
            ht = pix[2] | (pix[3] << 8);
            snprintf(path, sizeof path, "%s/board_%d.bmp", dir, shots);
            if (save_bmp(path, w, ht, pix + 4)) {
                printf("saved %s %dx%d\n", path, w, ht);
                shots++;
            }
            free(pix);
        }
    }
    CloseHandle(h);
    printf("shots %d\n", shots);
    return shots ? 0 : 1;
}
