#ifndef MUSIC_H
#define MUSIC_H

#include <stdint.h>

/* 整页音乐界面：标题+当前歌词+播放/暂停按钮。数据来自 link_media_*。 */
void music_page_draw(uint8_t a);
/* KEY2 切换播放/暂停返回1，其余返回0。 */
int music_page_key(uint8_t pressed);

#endif