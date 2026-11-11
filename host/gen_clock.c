/* 把 Inter 的数字光栅成 4 位灰度，给表盘用。
   运行一次即可。手表上不再依赖这套字体文件。
   Inter 是 SIL Open Font License 1.1，可免费商用。 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { DIGIT_N = 10, BOX = 96 };

typedef struct digit {
    int w;
    int h;
    int ox;
    int oy;
    int advance;
    uint8_t gray[BOX * BOX];
} digit_t;

static void *load_file(const char *path, DWORD *size_out) {
    HANDLE file;
    DWORD size;
    DWORD got = 0;
    void *buf;

    file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    size = GetFileSize(file, NULL);
    buf = malloc(size);
    if (!buf || !ReadFile(file, buf, size, &got, NULL)) {
        free(buf);
        CloseHandle(file);
        return NULL;
    }
    CloseHandle(file);
    *size_out = got;
    return buf;
}

static uint16_t ru16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t ru32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int ttf_family(const uint8_t *p, DWORD n, wchar_t *out, int out_n) {
    uint16_t tables;
    uint16_t count;
    uint16_t store;
    uint32_t name_off = 0;
    int i;
    int found = 0;

    if (n < 12) {
        return 0;
    }
    tables = ru16(p + 4);
    for (i = 0; i < tables; i++) {
        const uint8_t *rec = p + 12 + i * 16;
        if (n < (DWORD)(12 + (i + 1) * 16)) {
            return 0;
        }
        if (rec[0] == 'n' && rec[1] == 'a' && rec[2] == 'm' && rec[3] == 'e') {
            name_off = ru32(rec + 8);
        }
    }
    if (!name_off || name_off + 6 > n) {
        return 0;
    }
    count = ru16(p + name_off + 2);
    store = ru16(p + name_off + 4);
    for (i = 0; i < count; i++) {
        const uint8_t *rec = p + name_off + 6 + i * 12;
        uint16_t platform;
        uint16_t name_id;
        uint16_t len;
        uint16_t off;
        int k;
        if (name_off + 6 + (i + 1) * 12 > n) {
            break;
        }
        platform = ru16(rec);
        name_id = ru16(rec + 6);
        len = ru16(rec + 8);
        off = ru16(rec + 10);
        if (platform != 3 || name_id != 1 || len < 2 || (len % 2) != 0) {
            continue;
        }
        if (name_off + store + off + len > n) {
            continue;
        }
        if (len / 2 >= out_n) {
            len = (uint16_t)((out_n - 1) * 2);
        }
        for (k = 0; k < len / 2; k++) {
            out[k] = (wchar_t)ru16(p + name_off + store + off + k * 2);
        }
        out[len / 2] = 0;
        found = 1;
        break;
    }
    return found;
}

static int capture_digit(HDC hdc, wchar_t ch, digit_t *d) {
    GLYPHMETRICS gm;
    MAT2 mat;
    DWORD bytes;
    BYTE *buf;
    int stride;
    int x;
    int y;

    memset(&mat, 0, sizeof mat);
    mat.eM11.value = 1;
    mat.eM22.value = 1;
    memset(&gm, 0, sizeof gm);
    bytes = GetGlyphOutlineW(hdc, ch, GGO_GRAY8_BITMAP, &gm, 0, NULL, &mat);
    if (bytes == GDI_ERROR) {
        fprintf(stderr, "glyph %lc failed\n", ch);
        return 0;
    }
    buf = (BYTE *)calloc(bytes ? bytes : 1, 1);
    if (bytes && !buf) {
        return 0;
    }
    if (bytes && GetGlyphOutlineW(hdc, ch, GGO_GRAY8_BITMAP, &gm, bytes, buf, &mat) == GDI_ERROR) {
        free(buf);
        return 0;
    }
    d->w = (int)gm.gmBlackBoxX;
    d->h = (int)gm.gmBlackBoxY;
    d->advance = (int)gm.gmCellIncX;
    d->ox = gm.gmptGlyphOrigin.x;
    d->oy = gm.gmptGlyphOrigin.y;
    if (d->w > BOX || d->h > BOX) {
        fprintf(stderr, "digit %lc too big %dx%d\n", ch, d->w, d->h);
        free(buf);
        return 0;
    }
    stride = (d->w + 3) & ~3;
    memset(d->gray, 0, sizeof d->gray);
    for (y = 0; y < d->h; y++) {
        for (x = 0; x < d->w; x++) {
            int g = buf[y * stride + x];
            if (g > 64) {
                g = 64;
            }
            d->gray[y * d->w + x] = (uint8_t)g;
        }
    }
    free(buf);
    return 1;
}

static void blend(uint8_t *rgb, int w, int h, int x, int y, int cov) {
    int bg_r = 6, bg_g = 12, bg_b = 28;
    int fg_r = 236, fg_g = 244, fg_b = 255;
    int i;
    if (x < 0 || y < 0 || x >= w || y >= h || cov <= 0) {
        return;
    }
    i = (y * w + x) * 3;
    rgb[i + 0] = (uint8_t)((bg_b * (64 - cov) + fg_b * cov) / 64);
    rgb[i + 1] = (uint8_t)((bg_g * (64 - cov) + fg_g * cov) / 64);
    rgb[i + 2] = (uint8_t)((bg_r * (64 - cov) + fg_r * cov) / 64);
}

static void place_metrics(const digit_t *digits, int *cell_w, int *row_h, int *xs, int *ys) {
    int max_up = 0;
    int max_down = 0;
    int i;

    *cell_w = 0;
    for (i = 0; i < DIGIT_N; i++) {
        int down = digits[i].h - digits[i].oy;
        if (digits[i].advance > *cell_w) {
            *cell_w = digits[i].advance;
        }
        if (digits[i].oy > max_up) {
            max_up = digits[i].oy;
        }
        if (down > max_down) {
            max_down = down;
        }
    }
    *row_h = max_up + max_down;
    for (i = 0; i < DIGIT_N; i++) {
        xs[i] = (*cell_w - digits[i].advance) / 2 + digits[i].ox;
        ys[i] = max_up - digits[i].oy;
    }
}

static int write_preview(const char *path, const digit_t *digits, int cell_w, int row_h, const int *xs, const int *ys) {
    const char *sample[2] = {"10", "32"};
    int gap_x = 8;
    int gap_y = 10;
    int block_w = cell_w * 2 + gap_x;
    int block_h = row_h * 2 + gap_y;
    int x0 = (128 - block_w) / 2;
    int y0 = (108 - block_h) / 2;
    int row;
    uint8_t *rgb;
    FILE *fp;
    uint8_t header[54];
    int x;
    int y;

    rgb = (uint8_t *)malloc((size_t)128 * 160 * 3);
    if (!rgb) {
        return 0;
    }
    for (y = 0; y < 160; y++) {
        for (x = 0; x < 128; x++) {
            int i = (y * 128 + x) * 3;
            rgb[i] = 28;
            rgb[i + 1] = 12;
            rgb[i + 2] = 6;
        }
    }
    for (row = 0; row < 2; row++) {
        int i;
        for (i = 0; i < 2; i++) {
            int dig = sample[row][i] - '0';
            const digit_t *d = &digits[dig];
            int left = x0 + i * (cell_w + gap_x) + xs[dig];
            int top = y0 + row * (row_h + gap_y) + ys[dig];
            int px, py;
            for (py = 0; py < d->h; py++) {
                for (px = 0; px < d->w; px++) {
                    int g = d->gray[py * d->w + px];
                    int q = (g * 15 + 32) / 64;
                    blend(rgb, 128, 160, left + px, top + py, q * 64 / 15);
                }
            }
        }
    }
    memset(header, 0, sizeof header);
    header[0] = 'B';
    header[1] = 'M';
    *(uint32_t *)(header + 2) = 54 + 128 * 160 * 3;
    *(uint32_t *)(header + 10) = 54;
    *(uint32_t *)(header + 14) = 40;
    *(int32_t *)(header + 18) = 128;
    *(int32_t *)(header + 22) = -160;
    *(uint16_t *)(header + 26) = 1;
    *(uint16_t *)(header + 28) = 24;
    fp = fopen(path, "wb");
    if (!fp) {
        free(rgb);
        return 0;
    }
    fwrite(header, 1, 54, fp);
    fwrite(rgb, 1, (size_t)128 * 160 * 3, fp);
    fclose(fp);
    free(rgb);
    return 1;
}

static int emit_cover(FILE *fp, const digit_t *g, int n, int *off_base) {
    int i;
    int bytes = 0;
    for (i = 0; i < n; i++) {
        int count = g[i].w * g[i].h;
        int p;
        fprintf(fp, "    /* %d */\n", i);
        for (p = 0; p < count; p += 2) {
            int hi = (g[i].gray[p] * 15 + 32) / 64;
            int lo = 0;
            if (p + 1 < count) {
                lo = (g[i].gray[p + 1] * 15 + 32) / 64;
            }
            if (hi > 15) {
                hi = 15;
            }
            if (lo > 15) {
                lo = 15;
            }
            fprintf(fp, "0x%02X,", (hi << 4) | lo);
            bytes++;
            if ((p / 2) % 12 == 11) {
                fprintf(fp, "\n");
            }
        }
        fprintf(fp, "\n");
        *off_base += (count + 1) / 2;
    }
    return bytes;
}

static int write_song(const char *path,
                      const digit_t *digits, const int *dx, const int *dy, const int *dadv, int clock_h,
                      const digit_t *titles, const uint32_t *cps, int title_n,
                      const int *tx, const int *ty, const int *tadv, int title_h) {
    FILE *fp;
    int i;
    int off;
    int bytes;

    fp = fopen(path, "wb");
    if (!fp) {
        return 0;
    }
    fprintf(fp, "/* 华文琥珀。时钟数字和中文标题都从这套字来。\n");
    fprintf(fp, "   在电脑上光栅进 Flash，表上按 1:1 画。 */\n");
    fprintf(fp, "#include \"song_data.h\"\n\n");
    fprintf(fp, "const int song_line_h[2] = {%d, %d};\n\n", title_h, clock_h);
    fprintf(fp, "const uint8_t song_cover[] = {\n");
    off = 0;
    bytes = emit_cover(fp, titles, title_n, &off);
    bytes += emit_cover(fp, digits, 10, &off);
    fprintf(fp, "};\n\n");
    fprintf(fp, "const song_glyph_t song_glyph[] = {\n");
    off = 0;
    for (i = 0; i < title_n; i++) {
        int count = titles[i].w * titles[i].h;
        fprintf(fp, "    {0x%04X, 0, %d, %d, %d, %d, %d, %d},\n",
                cps[i], titles[i].w, titles[i].h, tadv[i], tx[i], ty[i], off);
        off += (count + 1) / 2;
    }
    for (i = 0; i < 10; i++) {
        int count = digits[i].w * digits[i].h;
        fprintf(fp, "    {'%d', 1, %d, %d, %d, %d, %d, %d},\n",
                i, digits[i].w, digits[i].h, dadv[i], dx[i], dy[i], off);
        off += (count + 1) / 2;
    }
    fprintf(fp, "};\n\n");
    fprintf(fp, "const int song_glyph_n = %d;\n", title_n + 10);
    fclose(fp);
    printf("song cover %d bytes, titles %d clock row %d\n", bytes, title_n, clock_h);
    return 1;
}

static int write_c(const char *path, const digit_t *digits, int cell_w, int row_h, const int *xs, const int *ys) {
    FILE *fp;
    int i;
    int nbyte = 0;

    fp = fopen(path, "wb");
    if (!fp) {
        return 0;
    }
    fprintf(fp, "/* 表盘数字。由 host/gen_clock.c 从 Inter Medium 光栅而来。\n");
    fprintf(fp, "   Inter, Copyright 2016 The Inter Project Authors.\n");
    fprintf(fp, "   SIL Open Font License 1.1, https://scripts.sil.org/OFL */\n");
    fprintf(fp, "#include \"clock_font.h\"\n\n");
    fprintf(fp, "const int clock_cell_w = %d;\n", cell_w);
    fprintf(fp, "const int clock_row_h = %d;\n\n", row_h);
    fprintf(fp, "const uint8_t clock_cover[] = {\n");
    for (i = 0; i < DIGIT_N; i++) {
        int count = digits[i].w * digits[i].h;
        int p;
        fprintf(fp, "    /* %d */\n", i);
        for (p = 0; p < count; p += 2) {
            int hi = (digits[i].gray[p] * 15 + 32) / 64;
            int lo = 0;
            if (p + 1 < count) {
                lo = (digits[i].gray[p + 1] * 15 + 32) / 64;
            }
            if (hi > 15) {
                hi = 15;
            }
            if (lo > 15) {
                lo = 15;
            }
            fprintf(fp, "0x%02X,", (hi << 4) | lo);
            nbyte++;
            if ((p / 2) % 12 == 11) {
                fprintf(fp, "\n");
            }
        }
        fprintf(fp, "\n");
    }
    fprintf(fp, "};\n\n");
    fprintf(fp, "const clock_glyph_t clock_digit[10] = {\n");
    {
        int off = 0;
        for (i = 0; i < DIGIT_N; i++) {
            int count = digits[i].w * digits[i].h;
            fprintf(fp, "    {%d, %d, %d, %d, %d},\n",
                    digits[i].w, digits[i].h, xs[i], ys[i], off);
            off += (count + 1) / 2;
        }
    }
    fprintf(fp, "};\n");
    fclose(fp);
    printf("clock cover %d bytes, cell %dx%d\n", nbyte, cell_w, row_h);
    return 1;
}

int main(int argc, char **argv) {
    const char *font_path;
    const char *out_c;
    const char *preview;
    int em;
    DWORD font_bytes = 0;
    void *font_buf = NULL;
    DWORD nfonts = 0;
    HANDLE added = NULL;
    HDC hdc;
    HFONT font;
    HFONT old;
    wchar_t face[64];
    TEXTMETRICW tm;
    digit_t digits[DIGIT_N];
    int i;
    int xs[DIGIT_N];
    int ys[DIGIT_N];
    int cell_w = 0;
    int row_h = 0;

    if (argc < 5) {
        fprintf(stderr, "usage: gen_clock font.ttf out.c preview.bmp em_px\n");
        return 1;
    }
    font_path = argv[1];
    out_c = argv[2];
    preview = argv[3];
    em = atoi(argv[4]);
    if (em < 16) {
        em = 16;
    }
    hdc = CreateCompatibleDC(NULL);
    font_buf = load_file(font_path, &font_bytes);
    if (font_buf) {
        added = AddFontMemResourceEx(font_buf, font_bytes, 0, &nfonts);
        if (!added) {
            fprintf(stderr, "AddFontMemResourceEx failed\n");
            return 1;
        }
        if (!ttf_family((const uint8_t *)font_buf, font_bytes, face, 64)) {
            fprintf(stderr, "no family name\n");
            return 1;
        }
    } else {
        MultiByteToWideChar(CP_UTF8, 0, font_path, -1, face, 64);
        printf("system face\n");
    }
    printf("family:");
    for (i = 0; face[i]; i++) {
        printf(" U+%04X", (unsigned)face[i]);
    }
    printf(" fonts %lu\n", nfonts);
    font = CreateFontW(-em, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       GB2312_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
    old = (HFONT)SelectObject(hdc, font);
    GetTextFaceW(hdc, 64, face);
    printf("selected:");
    for (i = 0; face[i]; i++) {
        printf(" U+%04X", (unsigned)face[i]);
    }
    printf("\n");
    if (!wcsstr(face, L"STHupo") && !wcsstr(face, L"Hupo") && !wcsstr(face, L"琥")) {
        fprintf(stderr, "clock face is not 华文琥珀\n");
        return 1;
    }
    GetTextMetricsW(hdc, &tm);
    for (i = 0; i < DIGIT_N; i++) {
        if (!capture_digit(hdc, (wchar_t)('0' + i), &digits[i])) {
            return 1;
        }
        printf("%d box %dx%d origin %d,%d advance %d\n",
               i, digits[i].w, digits[i].h, digits[i].ox, digits[i].oy, digits[i].advance);
        if (digits[i].advance > cell_w) {
            cell_w = digits[i].advance;
        }
    }
    place_metrics(digits, &cell_w, &row_h, xs, ys);
    printf("ascent %ld descent %ld cell %dx%d\n", tm.tmAscent, tm.tmDescent, cell_w, row_h);
    if (!write_preview(preview, digits, cell_w, row_h, xs, ys)) {
        fprintf(stderr, "preview failed\n");
        return 1;
    }
    {
        static const uint32_t title_cp[] = {
            0x8FD0, 0x52A8, 0x6D88, 0x606F, 0x8BBE, 0x7F6E
        };
        enum { TITLE_N = 6, TITLE_EM = 40 };
        digit_t titles[TITLE_N];
        int tx[TITLE_N];
        int ty[TITLE_N];
        int tadv[TITLE_N];
        int dadv[10];
        int title_h = 0;
        int max_up = 0;
        int max_down = 0;
        int n;
        HFONT title_font;

        for (n = 0; n < 10; n++) {
            dadv[n] = cell_w;
        }
        title_font = CreateFontW(-TITLE_EM, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"STHupo");
        SelectObject(hdc, title_font);
        for (n = 0; n < TITLE_N; n++) {
            int down;
            if (!capture_digit(hdc, (wchar_t)title_cp[n], &titles[n])) {
                return 1;
            }
            down = titles[n].h - titles[n].oy;
            if (titles[n].oy > max_up) {
                max_up = titles[n].oy;
            }
            if (down > max_down) {
                max_down = down;
            }
            printf("title U+%04X box %dx%d advance %d\n",
                   title_cp[n], titles[n].w, titles[n].h, titles[n].advance);
        }
        title_h = max_up + max_down;
        for (n = 0; n < TITLE_N; n++) {
            tx[n] = titles[n].ox;
            ty[n] = max_up - titles[n].oy;
            tadv[n] = titles[n].advance;
        }
        if (!write_song(out_c, digits, xs, ys, dadv, row_h,
                        titles, title_cp, TITLE_N, tx, ty, tadv, title_h)) {
            fprintf(stderr, "write song failed\n");
            return 1;
        }
        DeleteObject(title_font);
    }
    SelectObject(hdc, old);
    DeleteObject(font);
    DeleteDC(hdc);
    if (added) {
        RemoveFontMemResourceEx(added);
    }
    free(font_buf);
    return 0;
}
