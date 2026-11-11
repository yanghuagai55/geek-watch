#include <stdint.h>

#ifndef UI_SVG_CMD_T_DEFINED
#define UI_SVG_CMD_T_DEFINED
typedef struct { int8_t x0,y0,x1,y1,r; uint8_t ci; } ui_svg_cmd_t;
#endif

/* ci=0: base colour at draw time
 * ci=1: 0xEFBF (text white)
 * ci=2: 0xFCE1 (accent orange)
 * ci=3: 0x969F (ring light blue)
 * ci=4: 0x0063 (bg dark navy)
 * ci=5: 0x039C (blue)
 */
static const uint16_t icon_pal[] = { 0xEFBF, 0xFCE1, 0x969F, 0x0063, 0x039C };

/* runner: 5 poses, 60 ms/frame, infinite loop (ground y=42, box 32x42)
 * order: torso, leg1(thigh,shin), leg2(thigh,shin), arm1(upper,lower), arm2(upper,lower), head
 */
static const ui_svg_cmd_t run_svg[5][10] = {
  /* P0: Contact - right foot strikes ground (y=42), left leg tucked high behind */
  { {21,13, 14,24, 0,0}, {14,24, 21,32, 0,0}, {21,32, 23,42, 0,0}, {14,24,  7,28, 0,2}, { 7,28,  2,21, 0,2},
    {20,15, 25,21, 0,2}, {25,21, 29,15, 0,1}, {20,15, 13,20, 0,0}, {13,20, 17,26, 0,1}, {24, 8,  0, 0, 3,1} },

  /* P1: Down/Load - lowest bob, right knee deeply loaded (y=42), left leg swinging through */
  { {21,15, 15,26, 0,0}, {15,26, 22,34, 0,0}, {22,34, 19,42, 0,0}, {15,26, 15,34, 0,2}, {15,34,  9,32, 0,2},
    {20,17, 19,24, 0,2}, {19,24, 25,23, 0,1}, {20,17, 17,24, 0,0}, {17,24, 23,25, 0,1}, {24,10,  0, 0, 3,1} },

  /* P2: Toe-off/Drive - pushing off (y=42), left knee driving high, arms at max split */
  { {22,12, 15,23, 0,0}, {15,23, 10,32, 0,0}, {10,32,  5,42, 0,0}, {15,23, 24,23, 0,2}, {24,23, 22,32, 0,2},
    {21,14, 13,18, 0,2}, {13,18, 16,25, 0,1}, {21,14, 27,19, 0,0}, {27,19, 31,12, 0,1}, {25, 7,  0, 0, 3,1} },

  /* P3: Flight peak - highest bob, BOTH feet airborne (y<42), extreme split stride */
  { {23,10, 16,21, 0,0}, {16,21,  8,26, 0,0}, { 8,26,  1,23, 0,0}, {16,21, 26,22, 0,2}, {26,22, 29,31, 0,2},
    {22,12, 13,16, 0,2}, {13,16, 17,22, 0,1}, {22,12, 28,17, 0,0}, {28,17, 32,11, 0,1}, {26, 5,  0, 0, 3,1} },

  /* P4: Flight reach - airborne (y<42), front leg extending for strike, rear leg folding */
  { {22,11, 15,22, 0,0}, {15,22,  9,29, 0,0}, { 9,29,  4,24, 0,0}, {15,22, 23,28, 0,2}, {23,28, 28,37, 0,2},
    {21,13, 15,19, 0,2}, {15,19, 20,24, 0,1}, {21,13, 26,19, 0,0}, {26,19, 29,14, 0,1}, {25, 6,  0, 0, 3,1} }
};

/* mail: 4 key poses, 350 ms/pose (~1.4s each way), ping-pong loop (box 48x28)
 * order: box(4), inner folds(2), flap(2), letter(3) -> 11 commands/pose
 */
static const ui_svg_cmd_t mail_svg[4][11] = {
  /* P0: Closed - flap down, letter tucked inside */
  { { 6, 8, 42, 8, 0,0}, {42, 8, 42,26, 0,0}, {42,26,  6,26, 0,0}, { 6,26,  6, 8, 0,0},
    { 6,26, 18,17, 0,3}, {42,26, 30,17, 0,3},
    { 6, 8, 24,19, 0,3}, {42, 8, 24,19, 0,3},
    {12,10, 12,10, 0,1}, {12,10, 36,10, 0,1}, {36,10, 36,10, 0,1} },

  /* P1: Flap half-open - flap lifting, letter rim emerging */
  { { 6, 8, 42, 8, 0,0}, {42, 8, 42,26, 0,0}, {42,26,  6,26, 0,0}, { 6,26,  6, 8, 0,0},
    { 6,26, 18,17, 0,3}, {42,26, 30,17, 0,3},
    { 6, 8, 24, 5, 0,3}, {42, 8, 24, 5, 0,3},
    {12,10, 12, 8, 0,1}, {12, 8, 36, 8, 0,1}, {36, 8, 36,10, 0,1} },

  /* P2: Flap open - flap fully up, letter sliding out */
  { { 6, 8, 42, 8, 0,0}, {42, 8, 42,26, 0,0}, {42,26,  6,26, 0,0}, { 6,26,  6, 8, 0,0},
    { 6,26, 18,17, 0,3}, {42,26, 30,17, 0,3},
    { 6, 8, 24, 1, 0,3}, {42, 8, 24, 1, 0,3},
    {12,14, 12, 5, 0,1}, {12, 5, 36, 5, 0,1}, {36, 5, 36,14, 0,1} },

  /* P3: Letter extended - letter risen cleanly out of top */
  { { 6, 8, 42, 8, 0,0}, {42, 8, 42,26, 0,0}, {42,26,  6,26, 0,0}, { 6,26,  6, 8, 0,0},
    { 6,26, 18,17, 0,3}, {42,26, 30,17, 0,3},
    { 6, 8, 24, 1, 0,3}, {42, 8, 24, 1, 0,3},
    {12,15, 12, 2, 0,1}, {12, 2, 36, 2, 0,1}, {36, 2, 36,15, 0,1} }
};

/* gear: 1 pose, host-rotated continuous loop (box 32x32, centre 16,16)
 * order: 4 dia-tooth axes (8 rounded teeth), outer body disc, inner accent ring, centre hole
 */
static const ui_svg_cmd_t gear_svg[1][7] = {
  { {16, 2, 16,30, 0,0}, { 2,16, 30,16, 0,0}, { 6, 6, 26,26, 0,0}, { 6,26, 26, 6, 0,0},
    {16,16,  0, 0,11,0}, {16,16,  0, 0, 8,3}, {16,16,  0, 0, 5,4} }
};