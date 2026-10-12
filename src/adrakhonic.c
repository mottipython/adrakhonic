/*
 * adrakhonic: screensaver of the Adrakhonic theorem from "Anathem".
 *
 * A fraa at a slate proves, four different ways and with a different triangle each
 * time, that the square on the long side of a square-cornered triangle is as big as
 * the squares on the other two sides together.
 *
 *   adrakhonic            full screen on every display; any key or mouse movement quits
 *   adrakhonic --window   in a window; Esc or Q quits
 *   Adrakhonic.scr /s     Windows screensaver conventions
 */
#include <SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* The pictures (drawn with Gemini, prepared by tools/make_sprites.py) are built
 * into the program as PNG and JPEG files and decoded at start-up. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS /* else Windows builds need libwinpthread-1.dll */
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#ifdef _WIN32
#define ART_SECTION ".section .rdata,\"dr\"\n"
#else
#define ART_SECTION ".section .rodata\n"
#endif
#define ART(name, file) \
    __asm__(ART_SECTION ".global art_" #name "\nart_" #name ":\n.incbin \"assets/art/" file "\"\n" \
            ".global art_" #name "_end\nart_" #name "_end:\n.text\n"); \
    extern const unsigned char art_##name[], art_##name##_end[];
ART(slate, "slate.jpg")
ART(banner, "banner.png")
ART(chalk, "chalk.png")
ART(fraa_draw, "fraa_draw.png")
ART(fraa_point, "fraa_point.png")
ART(fraa_think, "fraa_think.png")
ART(fraa_glad, "fraa_glad.png")

enum { SP_SLATE, SP_BANNER, SP_CHALK, SP_FRAA_DRAW, SP_FRAA_POINT, SP_FRAA_THINK, SP_FRAA_GLAD, NSPRITE };
static struct { const unsigned char *file, *end; int w, h; unsigned char *px; } sprite[NSPRITE] = {
    {art_slate, art_slate_end},
    {art_banner, art_banner_end},
    {art_chalk, art_chalk_end},
    {art_fraa_draw, art_fraa_draw_end},
    {art_fraa_point, art_fraa_point_end},
    {art_fraa_think, art_fraa_think_end},
    {art_fraa_glad, art_fraa_glad_end},
};
static SDL_Texture **cur_tex; /* the pictures, for the renderer being drawn to */

static void load_sprites(void)
{
    for (int i = 0; i < NSPRITE; i++) {
        int n;
        sprite[i].px = stbi_load_from_memory(sprite[i].file, (int)(sprite[i].end - sprite[i].file), &sprite[i].w, &sprite[i].h, &n, 4);
        if (!sprite[i].px) { sprite[i].w = sprite[i].h = 1; }
    }
}
static void make_textures(SDL_Renderer *r, SDL_Texture **tex)
{
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    for (int i = 0; i < NSPRITE; i++) {
        tex[i] = NULL;
        if (!sprite[i].px) continue;
        tex[i] = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, sprite[i].w, sprite[i].h);
        if (!tex[i]) continue;
        SDL_UpdateTexture(tex[i], NULL, sprite[i].px, sprite[i].w * 4);
        SDL_SetTextureBlendMode(tex[i], SDL_BLENDMODE_BLEND);
    }
}

/* ---- options ---- */
static int opt_window = 0;
static double opt_speed = 1.0;
static uint64_t opt_seed = 0;
static const char *opt_snapshot = NULL;
static double opt_time = 30;
static int opt_w = 1920, opt_h = 1200;

/* ---- colours ---- */
typedef struct { Uint8 r, g, b; } Rgb;
static const Rgb BG = {20, 20, 18}, INK = {46, 32, 22}, CHORD = {130, 44, 30}, PAPER = {244, 240, 228};
static const Rgb RED = {226, 104, 88}, BLUE = {110, 170, 226}, GOLD = {240, 206, 98}, GREEN = {150, 204, 136};

/* ---- geometry buffer ---- */
static SDL_Vertex *vb;
static int *ib;
static int nv, ni, vcap, icap;

static void vb_need(int v, int i)
{
    if (nv + v > vcap) { vcap = (nv + v) * 2; vb = realloc(vb, sizeof *vb * vcap); }
    if (ni + i > icap) { icap = (ni + i) * 2; ib = realloc(ib, sizeof *ib * icap); }
}
static inline SDL_Color col(Rgb c, double a)
{
    SDL_Color o = {c.r, c.g, c.b, (Uint8)(a < 0 ? 0 : a > 1 ? 255 : a * 255)};
    return o;
}
static inline void vert(double x, double y, SDL_Color c)
{
    vb[nv].position.x = (float)x; vb[nv].position.y = (float)y;
    vb[nv].color = c; vb[nv].tex_coord.x = vb[nv].tex_coord.y = 0;
    nv++;
}
static void quad4(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3,
                  SDL_Color c0, SDL_Color c1, SDL_Color c2, SDL_Color c3)
{
    vb_need(4, 6);
    int b = nv;
    vert(x0, y0, c0); vert(x1, y1, c1); vert(x2, y2, c2); vert(x3, y3, c3);
    ib[ni++] = b; ib[ni++] = b + 1; ib[ni++] = b + 2; ib[ni++] = b; ib[ni++] = b + 2; ib[ni++] = b + 3;
}
static void quad(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3, SDL_Color c)
{
    quad4(x0, y0, x1, y1, x2, y2, x3, y3, c, c, c, c);
}
static void rect(double x, double y, double w, double h, SDL_Color c) { quad(x, y, x + w, y, x + w, y + h, x, y + h, c); }
static void disc(double cx, double cy, double r, SDL_Color centre, SDL_Color rim)
{
    int n = r < 4 ? 10 : r < 30 ? 28 : 56;
    vb_need(n + 1, 3 * n);
    int b = nv;
    vert(cx, cy, centre);
    for (int i = 0; i < n; i++) vert(cx + r * cos(i * 2 * M_PI / n), cy + r * sin(i * 2 * M_PI / n), rim);
    for (int i = 0; i < n; i++) { ib[ni++] = b; ib[ni++] = b + 1 + i; ib[ni++] = b + 1 + (i + 1) % n; }
}
/* a stroke with round ends */
static void line(double x0, double y0, double x1, double y1, double w, SDL_Color c)
{
    double dx = x1 - x0, dy = y1 - y0, l = hypot(dx, dy);
    if (l > 1e-9) {
        double nx = -dy / l * w / 2, ny = dx / l * w / 2;
        quad(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
    }
    if (w > 2.5) { disc(x0, y0, w / 2, c, c); disc(x1, y1, w / 2, c, c); }
}
__attribute__((unused)) static void ring(double cx, double cy, double r, double w, SDL_Color c)
{
    for (int i = 0; i < 64; i++) {
        double a = i * M_PI / 32, b = (i + 1) * M_PI / 32;
        line(cx + cos(a) * r, cy + sin(a) * r, cx + cos(b) * r, cy + sin(b) * r, w, c);
    }
}
static void flush(SDL_Renderer *r)
{
    if (ni) SDL_RenderGeometry(r, NULL, vb, nv, ib, ni);
    nv = ni = 0;
}

static SDL_Renderer *ren; /* where the current frame is being drawn */

/* a picture stretched over a rectangle; sx0..sx1 picks a slice of it across (0..1) */
static void pic_part(int which, double x, double y, double w, double h, double sx0, double sx1, double tilt, int flip)
{
    flush(ren);
    if (!cur_tex || !cur_tex[which]) return;
    SDL_Rect src = {(int)(sx0 * sprite[which].w), 0, (int)((sx1 - sx0) * sprite[which].w), sprite[which].h};
    SDL_FRect dst = {(float)x, (float)y, (float)w, (float)h};
    SDL_FPoint pivot = {(float)(w / 2), (float)h};
    SDL_RenderCopyExF(ren, cur_tex[which], &src, &dst, tilt, &pivot, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}
/* a picture `tall` high, standing with the point (ax, ay) of it (0..1 across and down) at (x, y) */
static void pic(int which, double x, double y, double tall, double ax, double ay)
{
    double w = tall * sprite[which].w / sprite[which].h;
    pic_part(which, x - w * ax, y - tall * ay, w, tall, 0, 1, 0, 0);
}

/* ---- lettering: pen strokes on a 4 x 6 grid, drawn with a round nib ---- */
static const char *GLYPH[] = {
    "A06 02 20 42 46|04 44", "B00 06 36 45 44 33 03|33 42 41 30 00", "C41 30 10 01 05 16 36 45",
    "D00 06 26 44 42 20 00", "E40 00 06 46|03 33", "F40 00 06|03 33", "G41 30 10 01 05 16 36 45 43 23",
    "H00 06|40 46|03 43", "I20 26|10 30|16 36", "J40 45 36 16 05", "K00 06|40 03 46", "L00 06 46",
    "M06 00 23 40 46", "N06 00 46 40", "O10 30 41 45 36 16 05 01 10", "P06 00 30 41 42 33 03",
    "Q10 30 41 45 36 16 05 01 10|24 46", "R06 00 30 41 42 33 03|23 46", "S41 30 10 01 02 13 33 44 45 36 16 05",
    "T00 40|20 26", "U00 05 16 36 45 40", "V00 26 40", "W00 16 23 36 40", "X00 46|40 06", "Y00 23 40|23 26",
    "Z00 40 06 46", "010 30 41 45 36 16 05 01 10", "111 20 26|16 36", "201 10 30 41 42 06 46",
    "301 10 30 41 42 33 23|33 44 45 36 16 05", "436 30 04 44", "540 00 03 33 44 45 36 16 05",
    "630 10 01 05 16 36 45 44 33 03", "700 40 16", "813 02 01 10 30 41 42 33 13 04 05 16 36 45 44 33",
    "943 13 02 01 10 30 41 45 36 16", ",25 26 17", ".26 26", ":22 22|25 25", "'20 21", "?01 10 30 41 42 24|26 26",
    "-13 33", "!20 24|26 26", "+03 43|21 25", "=02 42|04 44",
};
static const char *glyph(char ch)
{
    for (unsigned i = 0; i < sizeof GLYPH / sizeof *GLYPH; i++)
        if (GLYPH[i][0] == ch) return GLYPH[i] + 1;
    return NULL;
}
static double text_width(const char *str, double s) { return strlen(str) * 6.2 * s - 2.2 * s; }
/* text centred on cx, top at y; s is one grid step, so letters are 6s tall */
static void text(const char *str, double cx, double y, double s, double weight, SDL_Color c)
{
    double x = cx - text_width(str, s) / 2;
    for (; *str; str++, x += 6.2 * s) {
        const char *g = glyph(*str);
        if (!g) continue;
        int have = 0; double lx = 0, ly = 0;
        for (; *g; g++) {
            if (*g == '|') { have = 0; continue; }
            if (*g == ' ') continue;
            double gx = x + (g[0] - '0') * s, gy = y + (g[1] - '0') * s;
            g++;
            if (have) line(lx, ly, gx, gy, s * weight, c);
            disc(gx, gy, s * weight * (have && lx == gx && ly == gy ? 0.62 : 0.5), c, c); /* the nib is round */
            lx = gx; ly = gy; have = 1;
        }
    }
}
/* ---- the proofs ----
 *
 * The Adrakhonic theorem: in a triangle with a square corner, the square on the long
 * side is as big as the squares on the other two sides together. A round shows it
 * four ways, each with pieces that only slide, shear or turn, so that nothing is
 * taken on trust: what the pieces cover before, they cover after.
 *
 * The triangle has its square corner at O = (0, 0), with P = (a, 0) and Q = (0, b),
 * a no longer than b, and long side c. Figures are worked out in those units, y
 * upwards, and fitted to the slate when drawn. */
static double T, anim_t;
static uint64_t seed_state;
static double A, B, C;      /* this round's triangle */
#define TITLE 7.0
#define PROOF 27.0
#define NPROOF 4
#define ROUND (TITLE + NPROOF * PROOF)

static uint64_t next_rand(void)
{
    seed_state ^= seed_state << 13; seed_state ^= seed_state >> 7; seed_state ^= seed_state << 17;
    return seed_state;
}
static double frand(void) { return (next_rand() >> 11) * (1.0 / 9007199254740992.0); }
static double ramp(double t, double a, double b) { return t <= a ? 0 : t >= b ? 1 : (t - a) / (b - a); }
static double ease(double x) { return x * x * (3 - 2 * x); }
static double go(double t, double a, double b) { return ease(ramp(t, a, b)); }

static void new_round(void)
{
    T = 0;
    B = 1;
    A = 0.42 + 0.46 * frand(); /* never so nearly equal that the figures look square */
    C = hypot(A, B);
}

static void update(double dt)
{
    anim_t += dt;
    T += dt * opt_speed;
    if (T > ROUND) new_round();
}

/* ---- drawing ---- */
static double unit;
static double fx0, fy0, fs; /* where the figure's (0, 0) is on the slate, and its scale */
typedef struct { double x, y; } Pt;
static Pt pt(double x, double y) { Pt p = {x, y}; return p; }
static double scx(Pt p) { return fx0 + p.x * fs; }
static double scy(Pt p) { return fy0 - p.y * fs; }
static int scene_w;

/* fit the box (x0, y0)-(x1, y1) of the figure into the clear part of the slate */
static void fit(int w, int h, double x0, double y0, double x1, double y1)
{
    double rx0 = w * 0.34, rx1 = w * 0.9, ry0 = h * 0.13, ry1 = h * 0.71;
    fs = fmin((rx1 - rx0) / (x1 - x0), (ry1 - ry0) / (y1 - y0));
    fx0 = (rx0 + rx1) / 2 - (x0 + x1) / 2 * fs;
    fy0 = (ry0 + ry1) / 2 + (y0 + y1) / 2 * fs;
}

/* a shape coloured in with the side of the chalk */
static void fill(const Pt *p, int n, Rgb c, double alpha)
{
    if (alpha <= 0.004 || !cur_tex || !cur_tex[SP_CHALK]) return;
    flush(ren);
    SDL_Vertex v[16];
    int idx[42], ni2 = 0;
    for (int i = 0; i < n && i < 16; i++) {
        v[i].position.x = (float)scx(p[i]); v[i].position.y = (float)scy(p[i]);
        v[i].color = (SDL_Color){c.r, c.g, c.b, (Uint8)(fmin(alpha, 1) * 255)};
        /* the chalk grain belongs to the slate, not to the piece */
        v[i].tex_coord.x = (float)fmod(fabs(v[i].position.x) / (scene_w * 0.5), 1.0);
        v[i].tex_coord.y = (float)fmod(fabs(v[i].position.y) / (scene_w * 0.5), 1.0);
    }
    for (int i = 1; i + 1 < n; i++) { idx[ni2++] = 0; idx[ni2++] = i; idx[ni2++] = i + 1; }
    /* a piece that straddles a seam of the grain would smear, so keep each within one tile */
    float bx = 1e9f, by = 1e9f;
    for (int i = 0; i < n; i++) { if (v[i].position.x < bx) bx = v[i].position.x; if (v[i].position.y < by) by = v[i].position.y; }
    for (int i = 0; i < n; i++) {
        v[i].tex_coord.x = (v[i].position.x - bx) / (float)(scene_w * 0.62) + 0.02f;
        v[i].tex_coord.y = (v[i].position.y - by) / (float)(scene_w * 0.62) + 0.02f;
    }
    SDL_RenderGeometry(ren, cur_tex[SP_CHALK], v, n, idx, ni2);
}
/* a chalk line; `part` draws only the first so much of it */
static void stroke(Pt a, Pt b, Rgb c, double alpha, double part)
{
    if (alpha <= 0.004 || part <= 0) return;
    if (part > 1) part = 1;
    double x0 = scx(a), y0 = scy(a), x1 = x0 + (scx(b) - x0) * part, y1 = y0 + (scy(b) - y0) * part, wd = unit * 0.3;
    line(x0, y0, x1, y1, wd, col(c, alpha * 0.92));
    disc(x0, y0, wd / 2, col(c, alpha * 0.92), col(c, alpha * 0.92));
    disc(x1, y1, wd / 2, col(c, alpha * 0.92), col(c, alpha * 0.92));
}
static void outline(const Pt *p, int n, Rgb c, double alpha)
{
    for (int i = 0; i < n; i++) stroke(p[i], p[(i + 1) % n], c, alpha, 1);
}
static void piece(const Pt *p, int n, Rgb c, double alpha)
{
    fill(p, n, c, alpha * 0.9);
    outline(p, n, PAPER, alpha);
}
static void moved(const Pt *p, int n, double dx, double dy, Pt *out)
{
    for (int i = 0; i < n; i++) out[i] = pt(p[i].x + dx, p[i].y + dy);
}
static void square_on(Pt a, Pt b, Pt *out) /* the square on a-b, to the right of the way from a to b */
{
    double dx = b.x - a.x, dy = b.y - a.y;
    out[0] = a; out[1] = b; out[2] = pt(b.x + dy, b.y - dx); out[3] = pt(a.x + dy, a.y - dx);
}

static const char *say1, *say2; /* what is written under the figure just now */
static double proved;           /* 0..1 as a proof arrives at what was to be shown */
static int pose;                /* what the fraa is doing: 0 drawing, 1 pointing, 2 thinking, 3 delighted */

/* The triangle with a square on each side. */
static void figure_squares(double show, double fa, double fb, double fc)
{
    Pt O = pt(0, 0), P = pt(A, 0), Q = pt(0, B), tri[3] = {O, P, Q}, sa[4], sb[4], sc[4];
    square_on(O, P, sa); square_on(Q, O, sb); square_on(P, Q, sc);
    fill(sa, 4, RED, fa * 0.9); fill(sb, 4, BLUE, fb * 0.9); fill(sc, 4, GOLD, fc * 0.9);
    outline(sa, 4, PAPER, show * 0.7); outline(sb, 4, PAPER, show * 0.7); outline(sc, 4, PAPER, show * 0.7);
    piece(tri, 3, GREEN, show);
}

/* The key, kept in the corner of the slate the whole time: the triangle, each side
 * in its own colour, with its square in the same colour. Red is always the square on
 * the short side, blue the one on the middle side and gold the one on the long side,
 * in every proof. Under it the claim itself, in squares of the true sizes; it
 * brightens each time a proof arrives at it. */
static void draw_key(int w, int h, double show, double proved)
{
    double kx0 = fx0, ky0 = fy0, ks = fs;
    double rx0 = w * 0.088, rx1 = w * 0.268, ry0 = h * 0.13, ry1 = h * 0.37;
    fs = fmin((rx1 - rx0) / (A + 2 * B), (ry1 - ry0) / (2 * A + B));
    fx0 = (rx0 + rx1) / 2 - A / 2 * fs; fy0 = (ry0 + ry1) / 2 + B / 2 * fs;
    Pt O = pt(0, 0), P = pt(A, 0), Q = pt(0, B), sa[4], sb[4], sc[4];
    square_on(O, P, sa); square_on(Q, O, sb); square_on(P, Q, sc);
    fill(sa, 4, RED, show * 0.9); fill(sb, 4, BLUE, show * 0.9); fill(sc, 4, GOLD, show * 0.9);
    double keep = unit; unit *= 1.5; /* the sides of the triangle drawn heavily, in their colours */
    stroke(O, P, RED, show, 1); stroke(Q, O, BLUE, show, 1); stroke(P, Q, GOLD, show, 1);
    unit = keep;
    /* red square + blue square = gold square */
    double k = w * 0.036 / C, y = h * 0.455, x = w * 0.092, gapx = w * 0.026, bright = show * (0.6 + 0.4 * proved);
    double side[3] = {A * k, B * k, C * k};
    const Rgb *ink[3] = {&RED, &BLUE, &GOLD};
    const char *sign[2] = {"+", "="};
    fx0 = 0; fy0 = 0; fs = 1; /* straight on to the slate */
    for (int i = 0; i < 3; i++) {
        Pt sq[4] = {pt(x, -y), pt(x + side[i], -y), pt(x + side[i], -(y - side[i])), pt(x, -(y - side[i]))};
        fill(sq, 4, *ink[i], bright * 0.95); outline(sq, 4, PAPER, bright);
        x += side[i];
        if (i < 2) { text(sign[i], x + gapx / 2, y - C * k * 0.5 - unit * 0.9, unit * 0.34, 1.1, col(PAPER, bright)); x += gapx; }
    }
    fx0 = kx0; fy0 = ky0; fs = ks;
}

static void title(int w, int h, double t)
{
    fit(w, h, -B, -A, A + B, A + B);
    double in = go(t, 0.3, 1.8);
    figure_squares(in, go(t, 2, 3), go(t, 2.6, 3.6), go(t, 3.2, 4.2));
    say1 = "THE ADRAKHONIC THEOREM"; say2 = "THE SQUARE ON THE LONG SIDE IS AS BIG AS THE OTHER TWO TOGETHER";
    pose = t < 4.2 ? 0 : 1;
    proved = 0;
}

/* Four triangles in a square. They leave one hole, the square on the long side;
 * slid into the corners in pairs, they leave two, the squares on the other sides. */
static void proof_slide(int w, int h, double t)
{
    double s = A + B;
    fit(w, h, 0, 0, s, s);
    Pt frame[4] = {pt(0, 0), pt(s, 0), pt(s, s), pt(0, s)};
    Pt t1[3] = {pt(0, 0), pt(A, 0), pt(0, B)}, t2[3] = {pt(s, 0), pt(s, A), pt(A, 0)};
    Pt t3[3] = {pt(s, s), pt(B, s), pt(s, A)}, t4[3] = {pt(0, s), pt(0, B), pt(B, s)};
    Pt hole[4] = {pt(A, 0), pt(s, A), pt(B, s), pt(0, B)};
    Pt sqa[4] = {pt(0, 0), pt(A, 0), pt(A, A), pt(0, A)}, sqb[4] = {pt(A, A), pt(s, A), pt(s, s), pt(A, s)};
    double m4 = go(t, 9, 12), m3 = go(t, 12.5, 15.5), m1 = go(t, 16, 19);
    Pt q[3];
    outline(frame, 4, PAPER, go(t, 0.2, 1.2));
    fill(hole, 4, GOLD, (go(t, 4, 5.5) - go(t, 7.5, 8.8)) * 0.9);
    fill(sqa, 4, RED, go(t, 19.5, 21) * 0.9); fill(sqb, 4, BLUE, go(t, 20.2, 21.7) * 0.9);
    piece(t2, 3, GREEN, go(t, 1.4, 2.0));
    moved(t1, 3, 0, A * m1, q); piece(q, 3, GREEN, go(t, 1.0, 1.6));
    moved(t3, 3, -B * m3, 0, q); piece(q, 3, GREEN, go(t, 1.8, 2.4));
    moved(t4, 3, A * m4, -B * m4, q); piece(q, 3, GREEN, go(t, 2.2, 2.8));
    say1 = "BY SLIDING FOUR TRIANGLES";
    say2 = t < 4 ? "FOUR OF THE SAME TRIANGLE IN THE CORNERS OF A SQUARE" :
           t < 9 ? "THEY LEAVE ONE HOLE: THE SQUARE ON THE LONG SIDE" :
           t < 19.5 ? "SLIDE THREE OF THEM. NOTHING IS ADDED AND NOTHING TAKEN AWAY" :
           t < 23 ? "NOW THEY LEAVE TWO HOLES: THE SQUARES ON THE OTHER TWO SIDES" :
           "SAME SQUARE, SAME FOUR TRIANGLES: SO THE TWO HOLES EQUAL THE ONE";
    pose = t < 3 ? 0 : t < 9 ? 1 : t < 19.5 ? 2 : 3;
    proved = go(t, 21, 23);
}

/* Two squares side by side. Two cuts make two triangles; slide each one across and
 * the whole is a single square, on the long side. */
static void proof_two_cuts(int w, int h, double t)
{
    double s = A + B;
    fit(w, h, 0, 0, s, s);
    Pt sqb[4] = {pt(0, 0), pt(B, 0), pt(B, B), pt(0, B)}, sqa[4] = {pt(B, 0), pt(s, 0), pt(s, A), pt(B, A)};
    Pt ta[3] = {pt(0, 0), pt(A, 0), pt(0, B)}, tb[3] = {pt(A, 0), pt(s, 0), pt(s, A)};
    Pt r1[3] = {pt(A, 0), pt(s, A), pt(B, A)}, r2[4] = {pt(A, 0), pt(B, A), pt(B, B), pt(0, B)};
    Pt big[4] = {pt(A, 0), pt(s, A), pt(B, s), pt(0, B)};
    double cut = go(t, 4, 6.5), split = go(t, 7, 8.5), ma = go(t, 9.5, 13), mb = go(t, 14, 17.5), done = go(t, 18.5, 20);
    Pt q[3];
    /* first as two squares, then as the pieces the cuts make of them */
    fill(sqb, 4, BLUE, (go(t, 0.8, 1.8) - split) * 0.9); fill(sqa, 4, RED, (go(t, 1.4, 2.4) - split) * 0.9);
    outline(sqb, 4, PAPER, go(t, 0.2, 1.0) - split); outline(sqa, 4, PAPER, go(t, 0.8, 1.6) - split);
    fill(r1, 3, GOLD, split * 0.9); fill(r2, 4, GOLD, split * 0.9);
    stroke(pt(A, 0), pt(0, B), PAPER, cut - done * 0.6, cut); stroke(pt(A, 0), pt(s, A), PAPER, cut - done * 0.6, go(t, 5, 7.5));
    moved(ta, 3, B * ma, A * ma, q); fill(q, 3, done > 0 ? GOLD : GREEN, split * 0.9); outline(q, 3, PAPER, split * (1 - done * 0.6));
    moved(tb, 3, -A * mb, B * mb, q); fill(q, 3, done > 0 ? GOLD : GREEN, split * 0.9); outline(q, 3, PAPER, split * (1 - done * 0.6));
    outline(big, 4, PAPER, done);
    say1 = "BY TWO CUTS";
    say2 = t < 4 ? "THE SQUARES ON THE TWO SHORT SIDES, SET SIDE BY SIDE" :
           t < 9.5 ? "TWO CUTS, EACH AS LONG AS THE LONG SIDE, AND SQUARE TO EACH OTHER" :
           t < 18.5 ? "SLIDE THE TWO TRIANGLES ROUND TO THE TOP" :
           "THE TWO SQUARES HAVE BECOME ONE: THE SQUARE ON THE LONG SIDE";
    pose = t < 3 ? 0 : t < 9.5 ? 1 : t < 18.5 ? 2 : 3;
    proved = done;
}

/* The larger of the two small squares is cut in four through its middle, along the
 * long side and square to it. The four pieces slide, without turning, into the
 * corners of the square on the long side, and the other small square fills the
 * middle. */
static void proof_four_pieces(int w, int h, double t)
{
    double s = A + B, gapx = 0.22;
    Pt cc = pt(s + gapx + s / 2, s / 2), m = pt(B / 2, B / 2);
    fit(w, h, 0, 0, s + gapx + s, s);
    Pt d = pt(B / C, A / C), e = pt(-A / C, B / C);
    Pt k1 = pt(B, (A + B) / 2), k2 = pt((B - A) / 2, B), k3 = pt(0, (B - A) / 2), k4 = pt((B + A) / 2, 0);
    Pt q1[4] = {m, k1, pt(B, B), k2}, q2[4] = {m, k2, pt(0, B), k3}, q3[4] = {m, k3, pt(0, 0), k4}, q4[4] = {m, k4, pt(B, 0), k1};
    Pt *qs[4] = {q1, q2, q3, q4};
    static const double SD[4] = {-1, 1, 1, -1}, SE[4] = {-1, -1, 1, 1}; /* which corner of the big square each goes to */
    Pt big[4], sqa[4] = {pt(B, 0), pt(s, 0), pt(s, A), pt(B, A)}, q[4];
    for (int i = 0; i < 4; i++) {
        static const double X[4] = {-1, 1, 1, -1}, Y[4] = {-1, -1, 1, 1};
        big[i] = pt(cc.x + C / 2 * (X[i] * d.x + Y[i] * e.x), cc.y + C / 2 * (X[i] * d.y + Y[i] * e.y));
    }
    double cut = go(t, 3.5, 6), in = go(t, 0.3, 1.5);
    outline(big, 4, PAPER, go(t, 1.5, 3) * 0.75);
    for (int i = 0; i < 4; i++) {
        double mv = go(t, 7 + i * 3, 9.6 + i * 3);
        double tx = cc.x + C / 2 * (SD[i] * d.x + SE[i] * e.x) - m.x, ty = cc.y + C / 2 * (SD[i] * d.y + SE[i] * e.y) - m.y;
        moved(qs[i], 4, tx * mv, ty * mv, q);
        fill(q, 4, BLUE, in * 0.9);
        if (cut > 0) outline(q, 4, PAPER, cut); /* once cut, each piece has its own edge */
    }
    if (cut < 1) { Pt sqb[4] = {pt(0, 0), pt(B, 0), pt(B, B), pt(0, B)}; outline(sqb, 4, PAPER, in * (1 - cut)); }
    double ma = go(t, 19.3, 22);
    moved(sqa, 4, (cc.x - A / 2 - B) * ma, (cc.y - A / 2) * ma, q);
    piece(q, 4, RED, in);
    say1 = "BY CUTTING ONE SQUARE IN FOUR";
    say2 = t < 3.5 ? "THE SQUARES ON THE TWO SHORT SIDES, AND ROOM FOR THE THIRD" :
           t < 7 ? "CUT THE LARGER THROUGH ITS MIDDLE, ALONG THE LONG SIDE AND ACROSS IT" :
           t < 19.3 ? "SLIDE THE FOUR PIECES OVER WITHOUT TURNING THEM" :
           t < 22.5 ? "THE SMALL SQUARE GOES IN THE MIDDLE" : "BLUE AND RED TOGETHER EXACTLY FILL THE SQUARE ON THE LONG SIDE";
    pose = t < 3 ? 0 : t < 7 ? 1 : t < 22.5 ? 2 : 3;
    proved = go(t, 22, 23.5);
}

/* Each small square is pushed over into a slanting shape with the same base and
 * height, swung round a corner of the triangle, and pushed over again into an
 * upright strip of the big square. Pushing over and turning change no areas. */
static void proof_shear(int w, int h, double t)
{
    fit(w, h, -B, -A, A + B, A + B);
    Pt O = pt(0, 0), P = pt(A, 0), Q = pt(0, B), n = pt(B, A); /* n: across the big square, as long as its side */
    Pt F = pt(A * B * B / (C * C), A * A * B / (C * C));        /* the foot of the height dropped from O */
    figure_squares(go(t, 0.3, 1.6), 0, 0, 0);
    for (int k = 0; k < 2; k++) {
        double t0 = k ? 14 : 3, in = go(t, t0, t0 + 1), s1 = go(t, t0 + 1.5, t0 + 4), turn = go(t, t0 + 4.5, t0 + 7), s2 = go(t, t0 + 7.5, t0 + 10);
        Pt piv = k ? Q : P, q[4];
        if (in <= 0) continue;
        if (turn <= 0) {            /* pushed over, the side at the pivot staying put */
            if (!k) { q[0] = P; q[1] = pt(A, -A); q[2] = pt(0, -A + B * s1); q[3] = pt(0, B * s1); }
            else { q[0] = Q; q[1] = pt(-B, B); q[2] = pt(-B + A * s1, 0); q[3] = pt(A * s1, 0); }
        } else {                    /* swung round the pivot, then pushed over along the big square */
            double ang = (k ? 1 : -1) * M_PI / 2 * turn, c = cos(ang), sn = sin(ang);
            Pt base[4];
            if (!k) { base[0] = P; base[1] = pt(A, -A); base[2] = pt(0, -A + B); base[3] = Q; }
            else { base[0] = Q; base[1] = pt(-B, B); base[2] = pt(-B + A, 0); base[3] = P; }
            for (int i = 0; i < 4; i++) {
                double x = base[i].x - piv.x, y = base[i].y - piv.y;
                q[i] = pt(piv.x + x * c - y * sn, piv.y + x * sn + y * c);
            }
            if (turn >= 1) {        /* now it is piv, O, O + n, piv + n: slide the side through O along to F */
                Pt o2 = pt(O.x + (F.x - O.x) * s2, O.y + (F.y - O.y) * s2);
                q[0] = piv; q[1] = o2; q[2] = pt(o2.x + n.x, o2.y + n.y); q[3] = pt(piv.x + n.x, piv.y + n.y);
            }
        }
        piece(q, 4, k ? BLUE : RED, in);
    }
    Pt tri[3] = {O, P, Q};
    outline(tri, 3, PAPER, go(t, 0.3, 1.6));
    say1 = "BY PUSHING OVER AND TURNING";
    double u = t < 14 ? t - 3 : t - 14;
    say2 = t < 3 ? "THE TRIANGLE AND ITS THREE SQUARES" :
           t >= 24.3 ? "RED AND BLUE TOGETHER EXACTLY FILL THE SQUARE ON THE LONG SIDE" :
           u < 4.2 ? "PUSH A SQUARE OVER. SAME BASE, SAME HEIGHT, SO THE SAME SIZE" :
           u < 7.3 ? "SWING IT ROUND THE CORNER OF THE TRIANGLE" : "AND PUSH IT OVER AGAIN, UPRIGHT IN THE LARGE SQUARE";
    pose = t < 3 ? 0 : t >= 24.3 ? 3 : u < 4.2 ? 1 : 2;
    proved = go(t, 24, 25.3);
}

static void draw_caption(int w, int h, const char *l1, const char *l2)
{
    double s1 = unit * 0.46, s2 = unit * 0.3;
    double widest = fmax(text_width(l1, s1), text_width(l2, s2)), room = w * 0.66;
    if (widest > room) { s1 *= room / widest; s2 *= room / widest; }
    double bw = fmax(text_width(l1, s1), text_width(l2, s2)) + unit * 13, bh = unit * 11.5;
    double bx = w * 0.58 - bw / 2, by = h - unit * 0.8 - bh;
    double cap = 0.135, cw = bh * 1.15 * sprite[SP_BANNER].w * cap / sprite[SP_BANNER].h;
    pic_part(SP_BANNER, bx + cw - 1, by, bw - 2 * cw + 2, bh, cap, 1 - cap, 0, 0);
    pic_part(SP_BANNER, bx, by, cw, bh, 0, cap, 0, 0);
    pic_part(SP_BANNER, bx + bw - cw, by, cw, bh, 1 - cap, 1, 0, 0);
    text(l1, w * 0.58, by + bh * 0.25, s1, 0.95, col(INK, 1));
    text(l2, w * 0.58, by + bh * 0.25 + s1 * 6 + unit * 1.2, s2, 0.9, col(CHORD, 1));
}

static void draw_scene(SDL_Renderer *r, int w, int h)
{
    ren = r; scene_w = w;
    unit = (w < h ? w : h) * 0.0125;
    SDL_SetRenderDrawColor(r, BG.r, BG.g, BG.b, 255);
    SDL_RenderClear(r);
    pic_part(SP_SLATE, 0, 0, w, h, 0, 1, 0, 0);
    say1 = say2 = ""; pose = 0; proved = 0;
    double wipe = 1; /* each figure is rubbed out before the next is begun */
    if (T < TITLE) { title(w, h, T); wipe = 1 - ramp(T, TITLE - 0.8, TITLE); }
    else {
        int k = (int)((T - TITLE) / PROOF);
        double t = T - TITLE - k * PROOF;
        if (k >= NPROOF) k = NPROOF - 1;
        switch (k) {
        case 0: proof_slide(w, h, t); break;
        case 1: proof_two_cuts(w, h, t); break;
        case 2: proof_four_pieces(w, h, t); break;
        default: proof_shear(w, h, t); break;
        }
        wipe = fmin(ramp(t, 0, 0.5), 1 - ramp(t, PROOF - 0.9, PROOF - 0.1));
    }
    flush(r);
    if (wipe < 1) { /* the slate shows through again */
        SDL_Rect clip = {(int)(w * 0.3), (int)(h * 0.1), (int)(w * 0.63), (int)(h * 0.64)};
        SDL_RenderSetClipRect(r, &clip);
        if (cur_tex && cur_tex[SP_SLATE]) {
            SDL_SetTextureAlphaMod(cur_tex[SP_SLATE], (Uint8)((1 - wipe) * 255));
            pic_part(SP_SLATE, 0, 0, w, h, 0, 1, 0, 0);
            SDL_SetTextureAlphaMod(cur_tex[SP_SLATE], 255);
        }
        SDL_RenderSetClipRect(r, NULL);
    }
    draw_key(w, h, T < TITLE ? go(T, 4.5, 6) : 1, proved);
    flush(r);
    static const int POSE[4] = {SP_FRAA_DRAW, SP_FRAA_POINT, SP_FRAA_THINK, SP_FRAA_GLAD};
    double tall = h * 0.5, bob = pose == 3 ? fabs(sin(anim_t * 5)) * unit * 0.6 : sin(anim_t * 1.3) * unit * 0.12;
    pic(POSE[pose], w * 0.15, h * 0.97 - bob, tall, 0.5, 1);
    draw_caption(w, h, say1, say2);
    flush(r);
    double fade = fmin(ramp(T, 0, 1.2), 1 - ramp(T, ROUND - 1.2, ROUND - 0.1));
    if (fade < 1) { rect(0, 0, w, h, col(BG, 1 - fade)); flush(r); }
}

typedef struct { SDL_Window *win; SDL_Renderer *ren; SDL_Texture *scene, *tex[NSPRITE]; int w, h; } View;

static void size_view(View *v)
{
    int ow, oh;
    SDL_RendererInfo info;
    SDL_GetRendererOutputSize(v->ren, &ow, &oh);
    if (ow < 1 || oh < 1) return;
    int ss = 2; /* drawn at twice the size and scaled down, for smooth edges */
    if (SDL_GetRendererInfo(v->ren, &info) == 0) {
        if (!(info.flags & SDL_RENDERER_TARGETTEXTURE) || (info.flags & SDL_RENDERER_SOFTWARE)) ss = 0;
        else if (info.max_texture_width && (ow * 2 > info.max_texture_width || oh * 2 > info.max_texture_height)) ss = 0;
    }
    if (v->w == ow * (ss ? ss : 1) && v->h == oh * (ss ? ss : 1) && (v->scene || !ss)) return;
    if (v->scene) SDL_DestroyTexture(v->scene);
    v->scene = NULL; v->w = ow; v->h = oh;
    if (!ss) return;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    v->scene = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, ow * ss, oh * ss);
    if (v->scene) { v->w = ow * ss; v->h = oh * ss; }
}
static void render(View *v)
{
    if (v->scene) SDL_SetRenderTarget(v->ren, v->scene);
    cur_tex = v->tex;
    draw_scene(v->ren, v->w, v->h);
    if (v->scene) { SDL_SetRenderTarget(v->ren, NULL); SDL_RenderCopy(v->ren, v->scene, NULL, NULL); }
    SDL_RenderPresent(v->ren);
}
static int make_view(View *v, int display)
{
    Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI;
    int x = SDL_WINDOWPOS_CENTERED_DISPLAY(display);
    if (opt_window) flags |= SDL_WINDOW_RESIZABLE;
    else flags |= SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_BORDERLESS;
    memset(v, 0, sizeof *v);
    v->win = SDL_CreateWindow("The Adrakhonic Theorem", x, x, 1200, 750, flags);
    if (!v->win) return 0;
    v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!v->ren) v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_SOFTWARE);
    if (!v->ren) return 0;
    SDL_SetRenderDrawBlendMode(v->ren, SDL_BLENDMODE_BLEND);
    make_textures(v->ren, v->tex);
    return 1;
}

static void start_show(void)
{
    seed_state = opt_seed ? opt_seed * 0x9E3779B97F4A7C15ULL : ((uint64_t)time(NULL) << 20) ^ SDL_GetPerformanceCounter();
    if (!seed_state) seed_state = 1;
    for (int i = 0; i < 6; i++) next_rand();
    anim_t = 0;
    new_round();
}

static int snapshot(void)
{
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, opt_w, opt_h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = surf ? SDL_CreateSoftwareRenderer(surf) : NULL;
    if (!r) { fprintf(stderr, "adrakhonic: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Texture *tex[NSPRITE];
    make_textures(r, tex);
    cur_tex = tex;
    start_show();
    for (double t = 0; t < opt_time; t += 1.0 / 60) update(1.0 / 60);
    draw_scene(r, opt_w, opt_h);
    SDL_RenderPresent(r);
    if (SDL_SaveBMP(surf, opt_snapshot) != 0) { fprintf(stderr, "adrakhonic: %s\n", SDL_GetError()); return 1; }
    return 0;
}

static void usage(void)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "The Adrakhonic Theorem",
        "The Adrakhonic Theorem screensaver\n\n"
        "  --window        run in a window (Esc or Q quits)\n"
        "  --speed X       animation speed, 1 = normal\n"
        "  --seed N        repeatable sequence\n"
        "  --snapshot FILE.bmp [--time SECONDS] [--size WxH]\n"
        "                  write a picture of the screensaver after SECONDS and exit\n", NULL);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if ((a[0] == '/' || a[0] == '-') && (a[1] == 's' || a[1] == 'S') && !a[2]) continue;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'p' || a[1] == 'P') && (!a[2] || a[2] == ':')) return 0;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'c' || a[1] == 'C') && (!a[2] || a[2] == ':')) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "The Adrakhonic Theorem", "The Adrakhonic Theorem screensaver has no settings.", NULL);
            return 0;
        }
        else if (!strcmp(a, "--window")) opt_window = 1;
        else if (!strcmp(a, "--speed") && i + 1 < argc) opt_speed = SDL_atof(argv[++i]);
        else if (!strcmp(a, "--seed") && i + 1 < argc) opt_seed = SDL_strtoull(argv[++i], NULL, 10);
        else if (!strcmp(a, "--snapshot") && i + 1 < argc) opt_snapshot = argv[++i];
        else if (!strcmp(a, "--time") && i + 1 < argc) opt_time = SDL_atof(argv[++i]);
        else if (!strcmp(a, "--size") && i + 1 < argc) {
            const char *x = SDL_strchr(argv[++i], 'x');
            opt_w = SDL_atoi(argv[i]); opt_h = x ? SDL_atoi(x + 1) : 0;
            if (opt_w < 64 || opt_h < 64) { usage(); return 1; }
        }
        else { usage(); return 1; }
    }
    if (opt_speed < 0.05) opt_speed = 0.05;
    load_sprites();

    if (opt_snapshot) return snapshot();
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "adrakhonic: %s\n", SDL_GetError()); return 1; }

    View views[8];
    int nviews = 0, ndisp = opt_window ? 1 : SDL_GetNumVideoDisplays();
    if (ndisp < 1) ndisp = 1;
    for (int d = 0; d < ndisp && nviews < 8; d++)
        if (make_view(&views[nviews], d)) nviews++;
    if (!nviews) { fprintf(stderr, "adrakhonic: %s\n", SDL_GetError()); return 1; }
    if (!opt_window) SDL_ShowCursor(SDL_DISABLE);
    start_show();

    Uint64 freqc = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter(), start = last;
    double moved = 0;
    int running = 1;
    while (running) {
        SDL_Event e;
        double since = (double)(SDL_GetPerformanceCounter() - start) / freqc;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) running = 0;
            else if (e.type == SDL_KEYDOWN) {
                if (!opt_window || e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_q) running = 0;
            } else if (!opt_window && since > 1.0) {
                if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEWHEEL || e.type == SDL_FINGERDOWN) running = 0;
                else if (e.type == SDL_MOUSEMOTION) {
                    moved += abs(e.motion.xrel) + abs(e.motion.yrel);
                    if (moved > 24) running = 0;
                }
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / freqc;
        last = now;
        if (dt > 0.1) dt = 0.1;
        update(dt);
        for (int i = 0; i < nviews; i++) { size_view(&views[i]); render(&views[i]); }
        SDL_Delay(views[0].scene ? 1 : 12);
    }
    SDL_Quit();
    return 0;
}

