# Icon / animation SVG prompt spec (for an external generator AI)

Paste the block below to a capable AI to produce drop-in icon & animation data
for this watch UI. The renderer is NOT a general SVG engine: it draws only
**straight segments and filled discs**, integer coordinates, one integer scale.
Anything outside this format will not render.

## Hard renderer contract (do not violate)

- Command struct (C99, `ui/ui.h`):
  `typedef struct { int8_t x0,y0,x1,y1,r; uint8_t ci; } ui_svg_cmd_t;`
  - `r == 0`: straight segment `(x0,y0)->(x1,y1)`.
  - `r > 0`: filled disc, centre `(x0,y0)`, radius `r` (x1/y1 ignored).
  - `ci`: colour index. `0` = base colour passed at draw; `1..n` = `pal[ci-1]`.
- All coordinates are **int8 (-128..127)** in the icon's own local box; the host
  multiplies by an integer `scale` and translates. Keep the box small (see below).
- Stroke thickness is a draw-time parameter (pixels), NOT in the data. Limbs are
  single centre-lines; the renderer thickens them and rounds the caps. Do NOT
  outline limbs as filled polygons.
- Max 48 commands per frame (hard cap); target **<= 12** for ROM.
- Data must be `static const` (held by reference until the frame flushes).
- No curves, no arcs, no gradients, no text, no transforms in the data.
  Approximate curves with 2-4 short segments or a disc.
- Integer only. No floating point anywhere in the returned code.

## Local coordinate boxes

- Runner: `x 0..32`, `y 0..42`, **ground line y = 42** (feet touch 42 on contact).
- Mail / gear / generic icon: `x 0..48`, `y 0..28` (or square 0..32 for gear).

## Colour (this is a COLOUR screen — never all-white)

Return a palette as RGB565 `uint16_t` and reference it via `ci`. Suggested base
palette (match the firmware):
```
bg     0x0063  (6,12,28)      text   0xEFBF  (236,244,255)
ring   0x969F  (150,210,255)  accent 0xFCE1  (255,159,10)  orange
blue   0x039C  (0,114,230)    green  0x368B  (48,209,88)
pink   0xF9AB  (255,55,95)
```
Use >= 2 colours per icon (e.g. body = accent, head/hands = text, or envelope =
ring, letter = text). `ci 0` may be used for the dominant colour.

## Motion requirements

- Runner: **5 key poses**, 60 ms/frame, infinite loop. Must read as running, not
  marching: ~30° forward lean (head ahead of hips), a **flight phase** (both feet
  above y=42 in >=2 poses), exaggerated stride (rear leg trails near-horizontal),
  knees clearly bent, arms bent ~90° swinging **counter** to the legs, vertical
  bob ~5% of height. Use extreme key poses, NOT evenly-spaced stiff tweens.
- Mail: ping-pong (open then reverse-close), ~1.4 s each way. Rounded-corner
  envelope; a letter slides out of the top on open and retracts on close.
  Provide 3-5 key poses of the letter/flap; the host interpolates with easing.
- Gear: provide ONE clean symmetric pose (ring + 8 rounded teeth + centre hole);
  the host rotates it in code. Minimal / Apple-like, not chunky blobs.

## Required output format

Return ONLY compilable C99, no prose:
```c
/* palette */
static const uint16_t icon_pal[] = { 0xFCE1, 0xEFBF };
/* runner: [pose][cmd], ci set per command */
static const ui_svg_cmd_t run_svg[5][10] = {
  { {26,8, 0,0,3, 1}, {23,14, 17,24,0, 0}, ... },   /* head disc ci1, torso ci0 ... */
  ...
};
```
Order within a pose: torso, leg1(knee,foot), leg2(knee,foot), arm1(elbow,hand),
arm2(elbow,hand), head disc last. Keep 10 commands/pose for the runner.
State the intended frame ms and loop mode in a one-line comment.

## Budget & licence

- <= ~400 bytes of Flash per icon cycle (commands + palette).
- Original work or permissively-licensed reference only (MIT/BSD/CC0). No GPL.
- If replicating a reference GIF, describe poses from it; do not embed its pixels.

---

## Ready-to-paste prompt

> You are generating vector icon/animation data for a 128x160 colour MCU watch.
> Output ONLY C99 `static const ui_svg_cmd_t` tables + a RGB565 palette, using
> exactly this struct: `{int8 x0,y0,x1,y1,r; uint8 ci;}` (r=0 line, r>0 disc at
> x0,y0; ci=0 base colour, ci>=1 palette[ci-1]). Local box: runner x0..32,y0..42
> with ground y=42; icons x0..48,y0..28. <=12 commands/frame, int8 coords,
> integer only, single centre-line limbs (host thickens+rounds caps). COLOUR
> screen: use >=2 palette colours, never all-white. Runner = 5 key poses @60ms
> loop with ~30° forward lean, flight phase (feet above 42 in >=2 poses), trailing
> near-horizontal rear leg, bent knees, ~90° arms counter-swinging, ~5% vertical
> bob; extreme key poses not stiff tweens. Mail = rounded envelope, letter slides
> out/in, ping-pong 3-5 poses. Gear = one minimal symmetric ring+8 teeth+hole
> (host rotates). <=400B Flash per cycle. Original or MIT/BSD/CC0 only.
> Reference run style: [attach the run GIF]. Return compilable C, no prose.