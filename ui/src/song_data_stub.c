#include "song_data.h"

/* Minimal-build stand-in: 华文琥珀 not linked. song_draw falls back to the
   small dot font. Keep the full tables in song_data.c for on-demand builds. */
const uint8_t song_cover[1] = {0};
const song_glyph_t song_glyph[1] = {{0, 0, 0, 0, 0, 0, 0, 0}};
const int song_glyph_n = 0;
const int song_line_h[2] = {0, 0};