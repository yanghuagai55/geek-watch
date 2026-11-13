/* 用系统字体光栅化成 1 位字模，写出 ui/src/font_data.c。
   运行一次即可。手表运行时不再依赖 GDI。 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 64KB Flash。每个 16×16 汉字大约 44 字节，连 ASCII 一起收到 520 个汉字为止。
   不在表里的码位，手表画 8×16 空心方框。串口一条消息正文最多 23 字节 UTF-8。
   这不是 GB2312 全集。工具默认发「你好」，这两个字排在补充表最前。 */
enum { CELL = 16, MAX_GLYPH = 640, CJK_MAX = 520, MAX_BYTES = 64 };

typedef struct grec {
    uint32_t cp;
    uint8_t w;
    uint8_t h;
    uint8_t advance;
    int nbytes;
    int ink;
    uint8_t bytes[MAX_BYTES];
} grec_t;

static grec_t recs[MAX_GLYPH];
static int nrec;

static int ink_at(const uint32_t *px, int x, int y) {
    uint32_t p;
    int r, g, b;

    if (x < 0 || y < 0 || x >= CELL || y >= CELL) {
        return 0;
    }
    p = px[y * CELL + x];
    b = (int)(p & 255u);
    g = (int)((p >> 8) & 255u);
    r = (int)((p >> 16) & 255u);
    return (r + g + b) < 400;
}

static void set_bit(grec_t *g, int x, int y) {
    int stride = (g->w + 7) >> 3;
    int index = y * stride + (x >> 3);

    if (index < 0 || index >= MAX_BYTES) {
        return;
    }
    g->bytes[index] |= (uint8_t)(0x80u >> (x & 7));
    g->ink++;
}

static HFONT make_font(const wchar_t *name, int charset, int px) {
    return CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       charset, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, name);
}

static int face_usable(HDC dc, wchar_t *got, int got_n) {
    if (!GetTextFaceW(dc, got_n, got)) {
        return 0;
    }
    if (got[0] == 0) {
        return 0;
    }
    return _wcsicmp(got, L"System") != 0;
}

static HFONT pick_font(HDC dc, const wchar_t **names, const int *sets, int count, int px) {
    int i;

    for (i = 0; i < count; i++) {
        HFONT font = make_font(names[i], sets[i], px);
        HFONT old;
        wchar_t got[64];
        int ok;

        if (!font) {
            continue;
        }
        old = (HFONT)SelectObject(dc, font);
        ok = face_usable(dc, got, 64);
        SelectObject(dc, old);
        if (ok) {
            wprintf(L"font %s\n", got);
            return font;
        }
        DeleteObject(font);
    }
    return 0;
}

static void render_char(HDC dc, HBITMAP bmp, uint32_t *px, HFONT font, wchar_t ch) {
    HGDIOBJ old_bmp = SelectObject(dc, bmp);
    HGDIOBJ old_font = SelectObject(dc, font);
    RECT rc;
    HBRUSH brush;

    rc.left = 0;
    rc.top = 0;
    rc.right = CELL;
    rc.bottom = CELL;
    brush = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(dc, &rc, brush);
    DeleteObject(brush);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    DrawTextW(dc, &ch, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    GdiFlush();
    SelectObject(dc, old_font);
    SelectObject(dc, old_bmp);
    (void)px;
}

static int add_glyph(HDC dc, HBITMAP bmp, uint32_t *px, HFONT font,
                     uint32_t cp, int dest_w, int dest_h) {
    grec_t *g;
    int y, x;

    if (nrec >= MAX_GLYPH) {
        return 0;
    }
    g = &recs[nrec];
    memset(g, 0, sizeof *g);
    g->cp = cp;
    g->w = (uint8_t)dest_w;
    g->h = (uint8_t)dest_h;
    g->advance = (uint8_t)dest_w;
    g->nbytes = dest_h * ((dest_w + 7) >> 3);

    render_char(dc, bmp, px, font, (wchar_t)cp);
    if (cp == ' ') {
        nrec++;
        return 1;
    }
    if (dest_w == 8 && dest_h == 16) {
        for (y = 0; y < 16; y++) {
            for (x = 0; x < 8; x++) {
                if (ink_at(px, x * 2, y) || ink_at(px, x * 2 + 1, y)) {
                    set_bit(g, x, y);
                }
            }
        }
    } else {
        for (y = 0; y < dest_h; y++) {
            for (x = 0; x < dest_w; x++) {
                if (ink_at(px, x, y)) {
                    set_bit(g, x, y);
                }
            }
        }
    }
    nrec++;
    return 1;
}

static int cmp_rec(const void *a, const void *b) {
    const grec_t *ga = (const grec_t *)a;
    const grec_t *gb = (const grec_t *)b;
    if (ga->cp < gb->cp) {
        return -1;
    }
    if (ga->cp > gb->cp) {
        return 1;
    }
    return 0;
}

static int write_source(const char *path) {
    FILE *fp;
    int i, b;
    int offset = 0;

    fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "cannot write %s\n", path);
        return 0;
    }
    fprintf(fp, "/* 由 host/gen_font.c 生成。字模在 Flash，不进 16KB 预算。\n");
    fprintf(fp, "   ASCII 32..126，外加最多 %d 个汉字和常用标点。\n", CJK_MAX);
    fprintf(fp, "   缺的码位画成空心方框。不是整张 GB2312。\n");
    fprintf(fp, "   串口一条消息正文最多 23 字节 UTF-8。 */\n");
    fprintf(fp, "#include \"font.h\"\n\n");
    fprintf(fp, "static const uint8_t bits[] = {\n");
    for (i = 0; i < nrec; i++) {
        fprintf(fp, "    /* U+%04X */\n", recs[i].cp);
        for (b = 0; b < recs[i].nbytes; b++) {
            fprintf(fp, "%s0x%02X%s",
                    (b % 12 == 0) ? "    " : "",
                    recs[i].bytes[b],
                    (b + 1 == recs[i].nbytes) ? ",\n" : ",");
            if ((b + 1) % 12 == 0 && b + 1 != recs[i].nbytes) {
                fprintf(fp, "\n");
            }
        }
    }
    fprintf(fp, "};\n\n");
    fprintf(fp, "static const ui_glyph_t glyphs[] = {\n");
    for (i = 0; i < nrec; i++) {
        fprintf(fp, "    {0x%04Xu, %u, %u, %u, %u},\n",
                recs[i].cp, recs[i].w, recs[i].h, recs[i].advance, offset);
        offset += recs[i].nbytes;
    }
    fprintf(fp, "};\n\n");
    fprintf(fp, "const uint8_t *ui_font_bits(void) { return bits; }\n\n");
    fprintf(fp, "const ui_glyph_t *ui_font_find(uint32_t cp) {\n");
    fprintf(fp, "    int lo = 0;\n");
    fprintf(fp, "    int hi = (int)(sizeof glyphs / sizeof glyphs[0]) - 1;\n");
    fprintf(fp, "    while (lo <= hi) {\n");
    fprintf(fp, "        int mid = (lo + hi) >> 1;\n");
    fprintf(fp, "        if (glyphs[mid].cp == cp) return &glyphs[mid];\n");
    fprintf(fp, "        if (glyphs[mid].cp < cp) lo = mid + 1;\n");
    fprintf(fp, "        else hi = mid - 1;\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "    return 0;\n");
    fprintf(fp, "}\n");
    fclose(fp);
    return 1;
}

static int write_preview(const char *path) {
    const int scale = 3;
    const int cell = CELL * scale;
    const int cols = 16;
    int rows = (nrec + cols - 1) / cols;
    int width = cols * cell;
    int height = rows * cell;
    int stride = (width * 3 + 3) & ~3;
    uint8_t *pixels;
    FILE *fp;
    int i;
    uint8_t header[54];
    uint32_t file_size;

    pixels = (uint8_t *)calloc((size_t)stride * (size_t)height, 1);
    if (!pixels) {
        return 0;
    }
    for (i = 0; i < nrec; i++) {
        int col = i % cols;
        int row = i / cols;
        int ox = col * cell;
        int oy = row * cell;
        int stride_b = (recs[i].w + 7) >> 3;
        int gy, gx;

        for (gy = 0; gy < recs[i].h; gy++) {
            for (gx = 0; gx < recs[i].w; gx++) {
                int on = (recs[i].bytes[gy * stride_b + (gx >> 3)] >> (7 - (gx & 7))) & 1;
                int sy, sx;
                for (sy = 0; sy < scale; sy++) {
                    for (sx = 0; sx < scale; sx++) {
                        int px = ox + gx * scale + sx;
                        int py = oy + gy * scale + sy;
                        int bmp_y = height - 1 - py;
                        uint8_t *p = pixels + bmp_y * stride + px * 3;
                        if (on) {
                            p[0] = 20;
                            p[1] = 20;
                            p[2] = 20;
                        } else {
                            p[0] = 245;
                            p[1] = 245;
                            p[2] = 245;
                        }
                    }
                }
            }
        }
    }

    fp = fopen(path, "wb");
    if (!fp) {
        free(pixels);
        return 0;
    }
    file_size = 54u + (uint32_t)stride * (uint32_t)height;
    memset(header, 0, sizeof header);
    header[0] = 'B';
    header[1] = 'M';
    header[2] = (uint8_t)file_size;
    header[3] = (uint8_t)(file_size >> 8);
    header[4] = (uint8_t)(file_size >> 16);
    header[5] = (uint8_t)(file_size >> 24);
    header[10] = 54;
    header[14] = 40;
    header[18] = (uint8_t)width;
    header[19] = (uint8_t)(width >> 8);
    header[22] = (uint8_t)height;
    header[23] = (uint8_t)(height >> 8);
    header[26] = 1;
    header[28] = 24;
    fwrite(header, 1, 54, fp);
    fwrite(pixels, 1, (size_t)stride * (size_t)height, fp);
    fclose(fp);
    free(pixels);
    return 1;
}

static int utf8_one(const unsigned char **ps, uint32_t *cp) {
    const unsigned char *s = *ps;

    if (*s == 0) {
        return 0;
    }
    if (*s < 0x80) {
        *cp = *s;
        *ps = s + 1;
        return 1;
    }
    if ((*s & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(*s & 0x1F) << 6) | (s[1] & 0x3F);
        *ps = s + 2;
        return 1;
    }
    if ((*s & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(*s & 0x0F) << 12) |
              ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        *ps = s + 3;
        return 1;
    }
    *ps = s + 1;
    *cp = 0;
    return 1;
}

static int have_cp(uint32_t cp) {
    int i;

    for (i = 0; i < nrec; i++) {
        if (recs[i].cp == cp) {
            return 1;
        }
    }
    return 0;
}

/* 第一条必须是「你」，用来确认源文件按 UTF-8 编译。界面用字已经在 cjk[] 里。 */
static const char cjk_more[] =
    "你好"
    "，。！？、：；“”‘’（）—…《》·～"
    "谢谢再见请问对不起没关系早安晚安午安周末快乐生日快乐恭喜发财"
    "怎么样还好吗我知道了不明白等一下马上到今天晚上见明天上午开会"
    "记得带钥匙下雨了带伞吃饭了吗睡觉了在路上堵车加班下班请假"
    "取消确定返回收到明白辛苦了注意安全早点休息身体想你爱你忙吗"
    "在吗好的不行可以没有不是是的什么时候几点了多少钱在哪里"
    "打电话发消息回电有空吗愉快新年早上好晚上好下午好中午好"
    "啊呢吧呀嘛哦嗯哇哈喂爸妈哥姐弟妹朋友老师同学公司学校医院"
    "工作休息运动分钟秒钟星期几现在刚才已经还是或者因为所以但是"
    "如果虽然可是而且不过然后接着开始结束高兴难过生气怕累快慢"
    "新旧大小多少长短远近冷热饿渴病痛药水茶酒饭菜肉鱼米面果奶"
    "车站机场火车飞机出租步行短信微信手机网络充电非常感激忘记"
    "关灯锁门吃过还没睡不着起来顺利健康帮助需要应该可能谁这里那里"
    "零一二三四五六七八九十百千万亿半双东西南北左右前后旁边附近"
    "的一是不了人我在有他这为之大来以个中上们到说国和地也子时道"
    "出而要于就下得可年生自会那后能对着事其里所去行过家十用发天如"
    "然作方成者多日都三小军二无同么经法当起与好看学进种将还分此心"
    "前面又因只从想实民命水化长现理政力外文加电果体定回意正美情"
    "机她把被两间给已最重话几手主公使性等员真部义次门马车白思教"
    "山比或但身己位通路全数权且张系内题让该论世什并全入次别样点"
    "请稍等就来到家了出门今天下雨多穿一点有空一起祝你别熬夜干嘛"
    "吃饭没到了吗别急慢慢来下次见回头见保重啦回复没问题随便听你"
    "稍后联系正在开会不方便接位置地铁晚点飞机延误火车票酒店地址"
    "房间号密码钱包没电充电器银行卡便宜太贵开发票收据账单工资奖金"
    "加班费请假条批准改时间地点人物原因结果办法建议意见问题答案"
    "正确错误成功失败继续暂停播放停止上一首下一首音量屏幕日期年份"
    "月份天气温度湿度风速晴天阴天雨天雪天风大雾霾热死了好冷啊"
    "东西名字号码密码验证码通过失败重试保存删除修改新建打开关闭"
    "搜索分享下载上传完成进度剩余大约小时以前以后今天昨天明天后天"
    "早上中午晚上凌晨半夜周末假期上班放学放学回家出门进门坐下站起来"
    "喝水吃药看病挂号排队取号缴费充值提现转账收款付款红包零钱"
    "快递取件驿站包裹签收退货换货评价客服人工机器人"
    "会议通知议程纪要附件合同项目进度风险截止提醒闹钟倒计时"
    "跑步游泳骑行步数卡路里心率睡眠深睡浅睡清醒"
    "电量充满低电充电中蓝牙已连接已断开信号弱"
    "请尽快回复收到谢谢辛苦啦晚安好梦";

int main(int argc, char **argv) {
    const char *out_path = argc > 1 ? argv[1] : "ui/src/font_data.c";
    const char *preview = argc > 2 ? argv[2] : "host/font_preview.bmp";
    const wchar_t *ascii_names[] = {L"Consolas", L"Courier New", L"Lucida Console"};
    const int ascii_sets[] = {ANSI_CHARSET, ANSI_CHARSET, ANSI_CHARSET};
    const wchar_t *cjk_names[] = {L"SimSun", L"NSimSun", L"Microsoft YaHei"};
    const int cjk_sets[] = {GB2312_CHARSET, GB2312_CHARSET, DEFAULT_CHARSET};
    static const uint32_t cjk[] = {
        0x4E2D, /* 中 */
        0x4E8E, /* 于 */
        0x4EAE, /* 亮 */
        0x4F4E, /* 低 */
        0x5173, /* 关 */
        0x52A8, /* 动 */
        0x5EA6, /* 度 */
        0x606F, /* 息 */
        0x672C, /* 本 */
        0x6D88, /* 消 */
        0x7248, /* 版 */
        0x8FD0, /* 运 */
        0x9AD8, /* 高 */
        0x5F00, /* 开 */
        0x5F0F, /* 式 */
        0x6A21, /* 模 */
        0x6B65, /* 步 */
        0x76D8, /* 盘 */
        0x7F6E, /* 置 */
        0x8868, /* 表 */
        0x8BA1, /* 计 */
        0x8BBE, /* 设 */
        0x8BD5, /* 试 */
        0x8C03, /* 调 */
        0x6B4C, /* 歌 */
        0x8BCD, /* 词 */
        0x62AC, /* 抬 */
        0x8155, /* 腕 */
        0x6309, /* 按 */
        0x952E, /* 键 */
        0x84DD, /* 蓝 */
        0x7259, /* 牙 */
        0x4F9B  /* 供 */
    };
    HDC screen;
    HDC mem;
    BITMAPINFO info;
    uint32_t *px = 0;
    HBITMAP bmp;
    HFONT ascii_font;
    HFONT cjk_font;
    int cp;
    int i;
    int failed = 0;

    SetProcessDPIAware();
    screen = GetDC(NULL);
    mem = CreateCompatibleDC(screen);
    ZeroMemory(&info, sizeof info);
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = CELL;
    info.bmiHeader.biHeight = -CELL;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    bmp = CreateDIBSection(mem, &info, DIB_RGB_COLORS, (void **)&px, NULL, 0);
    if (!bmp || !px) {
        fprintf(stderr, "DIB failed\n");
        return 1;
    }

    ascii_font = pick_font(mem, ascii_names, ascii_sets, 3, 16);
    cjk_font = pick_font(mem, cjk_names, cjk_sets, 3, 16);
    if (!ascii_font || !cjk_font) {
        fprintf(stderr, "font missing ascii=%p cjk=%p\n",
                (void *)ascii_font, (void *)cjk_font);
        return 1;
    }

    for (cp = 32; cp <= 126; cp++) {
        add_glyph(mem, bmp, px, ascii_font, (uint32_t)cp, 8, 16);
    }
    for (i = 0; i < (int)(sizeof cjk / sizeof cjk[0]); i++) {
        add_glyph(mem, bmp, px, cjk_font, cjk[i], 16, 16);
    }
    {
        const unsigned char *p = (const unsigned char *)cjk_more;
        uint32_t code = 0;
        int cjk_n = 0;
        int dropped = 0;

        if (!utf8_one(&p, &code) || code != 0x4F60) {
            fprintf(stderr, "cjk string is not utf-8 (U+%04X)\n", code);
            return 1;
        }
        for (i = 0; i < nrec; i++) {
            if (recs[i].cp > 127) {
                cjk_n++;
            }
        }
        p = (const unsigned char *)cjk_more;
        while (utf8_one(&p, &code)) {
            if (code < 128 || have_cp(code)) {
                continue;
            }
            if (cjk_n >= CJK_MAX || nrec >= MAX_GLYPH) {
                dropped++;
                continue;
            }
            if (!add_glyph(mem, bmp, px, cjk_font, code, 16, 16)) {
                fprintf(stderr, "glyph cap U+%04X\n", code);
                return 1;
            }
            cjk_n++;
        }
        if (dropped) {
            fprintf(stderr, "kept %d cjk, dropped %d past the flash limit\n",
                    cjk_n, dropped);
        } else {
            printf("cjk %d\n", cjk_n);
        }
    }
    qsort(recs, (size_t)nrec, sizeof recs[0], cmp_rec);

    for (i = 0; i < nrec; i++) {
        uint32_t code = recs[i].cp;
        int han = code >= 0x4E00 && code <= 0x9FFF;
        int need = (code >= '0' && code <= '9') || (code >= 'A' && code <= 'Z') ||
                   (code >= 'a' && code <= 'z') || code == '%' || code == '/' ||
                   code == ':' || code == '-' || code > 127;
        int min_ink = han ? 6 : (code > 127 ? 1 : 2);
        if (need && recs[i].ink < min_ink) {
            fprintf(stderr, "thin glyph U+%04X ink %d\n", code, recs[i].ink);
            failed = 1;
        }
    }
    if (failed) {
        return 1;
    }
    if (!write_source(out_path) || !write_preview(preview)) {
        fprintf(stderr, "write failed\n");
        return 1;
    }
    printf("glyphs %d -> %s\n", nrec, out_path);
    DeleteObject(ascii_font);
    DeleteObject(cjk_font);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
    return 0;
}
