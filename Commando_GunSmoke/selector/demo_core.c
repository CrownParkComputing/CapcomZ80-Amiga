/* demo_core.c -- WhittyDemo old-school intro renderer (see demo_core.h). */
#include "demo_core.h"
#include <string.h>
#include "demo_sintab.h"

/* generated picture (demo_pic.c): pens 0..PIC_COLS-1, offset to 96.. here */
extern const int demo_pic_w, demo_pic_h;
extern const unsigned char demo_pic_pal[][3];
extern const unsigned char demo_pic_data[];

#define PIC_PEN_BASE 96
#define PIC_MAX_W    560   /* fits the 550-wide two-square selector composite */
#define PIC_MAX_H    300
#define BLOCK        25
#define MAX_BLOCKS   ((PIC_MAX_W / BLOCK) * (PIC_MAX_H / BLOCK))

/* reveal cycle (frames @50Hz): build 1 block/frame, hold, dissolve, dark */
#define CYC_BUILD    180
#define CYC_HOLD     400
#define CYC_DISSOLVE 60
#define CYC_DARK     80
#define CYC_TOTAL    (CYC_BUILD + CYC_HOLD + CYC_DISSOLVE + CYC_DARK)

unsigned char demo_pal[256][3];

static int W, H, CX, CY;
static unsigned char bgpen[600];               /* per-row backdrop pen        */
static unsigned char pic_pens[PIC_MAX_W * PIC_MAX_H]; /* picture, pre-offset  */
static unsigned short blk_rank[MAX_BLOCKS];    /* block -> reveal order       */
static int nblocks, blk_cols, blk_rows;
static int pic_x, pic_y;
static int text_variant;
static int selector_selection;

/* ---- fixed-point trig: full circle = 65536, amplitude 4096 ---- */
static int isin(int a)
{
    int i = (a >> 8) & 255, f = a & 255;
    int s0 = sintab256[i], s1 = sintab256[(i + 1) & 255];
    return s0 + (((s1 - s0) * f) >> 8);
}
static int icos(int a) { return isin(a + 16384); }

static int isqrt32(unsigned v)
{
    unsigned r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return (int)r;
}

/* ---- 5x7 font (A-Z 0-9 - . like the ArcadeIntro loader) ---- */
static const unsigned char font5x7[][7] = {
    {0,0,0,0,0,0,0},{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,14},
    {17,17,17,31,17,17,17},{14,4,4,4,4,4,14},{1,1,1,1,17,17,14},{17,18,20,24,20,18,17},
    {16,16,16,16,16,16,31},{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},
    {31,4,4,4,4,4,4},{17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},
    {17,17,10,4,10,17,17},{17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14},{14,17,17,15,1,1,14},{0,0,0,31,0,0,0},{0,0,0,0,0,12,12},
};

static int glyph_index(char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return 1 + c - 'A';
    if (c >= '0' && c <= '9') return 27 + c - '0';
    if (c == '-') return 37;
    if (c == '.') return 38;
    return 0;
}

static void fill_rect(unsigned char *fb, int x0, int y0, int w, int h, unsigned char c)
{
    int x1 = x0 + w, y1 = y0 + h, y;
    if (x1 <= 0 || y1 <= 0 || x0 >= W || y0 >= H) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    for (y = y0; y < y1; y++)
        memset(fb + (long)y * W + x0, c, (size_t)(x1 - x0));
}

/* ---- palette ------------------------------------------------------------ */
#define P_RAINBOW 16      /* 16..47: 32-step colour wheel                    */
#define P_BLUE    48      /* 48..59: steel-blue shade ramp (ring)            */
#define P_GOLD    60      /* 60..71: gold ramp (cube)                        */
#define P_RED     72      /* 72..83: red ramp (octahedron)                   */
#define P_GREEN   84      /* 84..95: green ramp (tetrahedron)                */

static void set_rgb(int i, int r, int g, int b)
{
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    demo_pal[i][0] = (unsigned char)r;
    demo_pal[i][1] = (unsigned char)g;
    demo_pal[i][2] = (unsigned char)b;
}

static void build_palette(void)
{
    int i;
    /* 0..15 base: black, backdrop gradient pens, grey, white */
    set_rgb(0, 0, 0, 0);
    for (i = 1; i <= 13; i++) {
        /* navy -> purple horizon -> near black */
        int t = (i - 1) * 4096 / 12;                    /* 0..4096 */
        int peak = isin(t / 2 * 8);                     /* half sine bump */
        set_rgb(i, 8 + (peak * 52 >> 12), 6 + (peak * 18 >> 12), 26 + (peak * 84 >> 12));
    }
    set_rgb(14, 140, 150, 160);
    set_rgb(15, 245, 245, 250);

    /* 16..47 rainbow wheel */
    for (i = 0; i < 32; i++) {
        int h = i * 6 * 256 / 32;                       /* hue 0..1535 */
        int sec = h >> 8, f = h & 255;
        int r = 0, g = 0, b = 0;
        switch (sec) {
        case 0: r = 255;     g = f;       b = 0;       break;
        case 1: r = 255 - f; g = 255;     b = 0;       break;
        case 2: r = 0;       g = 255;     b = f;       break;
        case 3: r = 0;       g = 255 - f; b = 255;     break;
        case 4: r = f;       g = 0;       b = 255;     break;
        default:r = 255;     g = 0;       b = 255 - f; break;
        }
        set_rgb(P_RAINBOW + i, r, g, b);
    }

    /* 48..95 four 12-step shading ramps */
    for (i = 0; i < 12; i++) {
        int v = 44 + i * 19;                            /* 44..253 */
        set_rgb(P_BLUE  + i, v / 3, v / 2, v);
        set_rgb(P_GOLD  + i, v, v * 3 / 4, v / 6);
        set_rgb(P_RED   + i, v, v / 5, v / 3);
        set_rgb(P_GREEN + i, v / 4, v, v / 3);
    }

    /* 96..255 picture palette */
    for (i = 0; i < 160; i++)
        set_rgb(PIC_PEN_BASE + i, demo_pic_pal[i][0], demo_pic_pal[i][1], demo_pic_pal[i][2]);
}

/* ---- 3D filled vectors --------------------------------------------------- */
#define RING_SEGA 16
#define RING_SEGB 6
#define RING_R    255
#define RING_r    22
#define ORBIT_R   (RING_R + RING_r + 34)

typedef struct {
    int z;                        /* painter depth (bigger = farther)        */
    int n;                        /* 3 or 4 vertices                         */
    short px[4], py[4];
    unsigned char pen;
} dface;

#define MAX_FACES 160
static dface faces[MAX_FACES];
static int nfaces;

/* Emit one face from transformed 3D verts (x right, y up, z into screen).
 * Culls back faces, flat-shades into ramp_base + 0..11.
 *
 * Conventions (derived from the projection py = CY - vy, y-down screen):
 * front faces of consistently-wound meshes project with NEGATIVE screen
 * area2, and the numeric cross e1 x e2 is the INWARD normal. Shade with a
 * light at the viewer's top-left: d = outward . S = 1400*nx-2000*ny+3200*nz
 * over the inward normal components. */
static void emit_face(const int *vx, const int *vy, const int *vz,
                      const int *idx, int n, int ramp_base)
{
    int px[4], py[4], i, zsum = 0;
    long area2;
    int e1x, e1y, e1z, e2x, e2y, e2z;
    long nx, ny, nz;
    int len, d, sidx;
    dface *f;

    if (nfaces >= MAX_FACES) return;
    for (i = 0; i < n; i++) {
        px[i] = CX + vx[idx[i]];
        py[i] = CY - vy[idx[i]];
        zsum += vz[idx[i]];
    }
    area2 = (long)(px[1] - px[0]) * (py[2] - py[0]) -
            (long)(px[2] - px[0]) * (py[1] - py[0]);
    if (area2 >= 0) return;                       /* back face */

    e1x = vx[idx[1]] - vx[idx[0]];
    e1y = vy[idx[1]] - vy[idx[0]];
    e1z = vz[idx[1]] - vz[idx[0]];
    e2x = vx[idx[2]] - vx[idx[0]];
    e2y = vy[idx[2]] - vy[idx[0]];
    e2z = vz[idx[2]] - vz[idx[0]];
    nx = (long)e1y * e2z - (long)e1z * e2y;
    ny = (long)e1z * e2x - (long)e1x * e2z;
    nz = (long)e1x * e2y - (long)e1y * e2x;
    while (nx > 25000 || nx < -25000 || ny > 25000 || ny < -25000 ||
           nz > 25000 || nz < -25000) {
        nx >>= 2; ny >>= 2; nz >>= 2;
    }
    len = isqrt32((unsigned)(nx * nx + ny * ny + nz * nz));
    if (len < 1) len = 1;
    d = (int)((1400L * nx - 2000L * ny + 3200L * nz) / len);
    sidx = (d + 4096) * 12 / 8500;
    if (sidx < 0) sidx = 0;
    if (sidx > 11) sidx = 11;

    f = &faces[nfaces++];
    f->z = zsum / n;
    f->n = n;
    for (i = 0; i < n; i++) { f->px[i] = (short)px[i]; f->py[i] = (short)py[i]; }
    f->pen = (unsigned char)(ramp_base + sidx);
}

/* rotate about X then Y (angles in 65536-per-circle units) */
static void rot_xy(int *x, int *y, int *z, int ax, int ay)
{
    int cx_ = icos(ax), sx_ = isin(ax);
    int cy_ = icos(ay), sy_ = isin(ay);
    int y1 = ((*y) * cx_ - (*z) * sx_) >> 12;
    int z1 = ((*y) * sx_ + (*z) * cx_) >> 12;
    int x1 = ((*x) * cy_ + z1 * sy_) >> 12;
    *z = (z1 * cy_ - (*x) * sy_) >> 12;
    *x = x1;
    *y = y1;
}

static void fill_face(unsigned char *fb, const dface *f)
{
    int ymin = 32767, ymax = -32768, i, y;
    static short exl[600], exr[600];
    for (i = 0; i < f->n; i++) {
        if (f->py[i] < ymin) ymin = f->py[i];
        if (f->py[i] > ymax) ymax = f->py[i];
    }
    if (ymax < 0 || ymin >= H) return;
    if (ymin < 0) ymin = 0;
    if (ymax >= H) ymax = H - 1;
    for (y = ymin; y <= ymax; y++) { exl[y] = 32767; exr[y] = -32768; }

    for (i = 0; i < f->n; i++) {
        int j = (i + 1 == f->n) ? 0 : i + 1;
        int x0 = f->px[i], y0 = f->py[i], x1 = f->px[j], y1 = f->py[j];
        int dy, sy0, sy1;
        if (y0 == y1) {
            if (y0 >= 0 && y0 < H) {
                if (x0 < exl[y0]) exl[y0] = (short)x0;
                if (x1 < exl[y0]) exl[y0] = (short)x1;
                if (x0 > exr[y0]) exr[y0] = (short)x0;
                if (x1 > exr[y0]) exr[y0] = (short)x1;
            }
            continue;
        }
        if (y0 > y1) { int t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
        dy = y1 - y0;
        sy0 = y0 < 0 ? 0 : y0;
        sy1 = y1 >= H ? H - 1 : y1;
        for (y = sy0; y <= sy1; y++) {
            int x = x0 + (int)((long)(x1 - x0) * (y - y0) / dy);
            if (x < exl[y]) exl[y] = (short)x;
            if (x > exr[y]) exr[y] = (short)x;
        }
    }
    for (y = ymin; y <= ymax; y++) {
        int xl = exl[y], xr = exr[y];
        if (xl > xr) continue;
        if (xl < 0) xl = 0;
        if (xr >= W) xr = W - 1;
        if (xl <= xr)
            memset(fb + (long)y * W + xl, f->pen, (size_t)(xr - xl + 1));
    }
}

static void sort_faces(void)
{
    int i, j;
    for (i = 1; i < nfaces; i++) {          /* far (big z) first */
        dface t = faces[i];
        for (j = i - 1; j >= 0 && faces[j].z < t.z; j--)
            faces[j + 1] = faces[j];
        faces[j + 1] = t;
    }
}

/* solid shape meshes (local units, scaled at build) */
static const signed char cube_v[8][3] = {
    {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}
};
static const unsigned char cube_f[6][4] = {
    {0,1,2,3},{5,4,7,6},{4,5,1,0},{3,2,6,7},{1,5,6,2},{4,0,3,7}
};
static const signed char octa_v[6][3] = {
    {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
};
static const unsigned char octa_f[8][3] = {
    {0,2,5},{2,1,5},{1,3,5},{3,0,5},{2,0,4},{1,2,4},{3,1,4},{0,3,4}
};
static const signed char tetra_v[4][3] = {
    {1,1,1},{1,-1,-1},{-1,1,-1},{-1,-1,1}
};
static const unsigned char tetra_f[4][3] = {
    {0,2,1},{0,1,3},{0,3,2},{1,2,3}
};

static void build_solid(const signed char (*sv)[3], int nv,
                        const unsigned char *sf, int fstride, int fverts, int nf,
                        int scale, int spin_x, int spin_y,
                        int orbit_cx, int orbit_cy, int orbit_cz,
                        int tilt_ax, int tilt_ay, int ramp)
{
    int vx[8], vy[8], vz[8], i, f;
    for (i = 0; i < nv; i++) {
        int x = sv[i][0] * scale, y = sv[i][1] * scale, z = sv[i][2] * scale;
        rot_xy(&x, &y, &z, spin_x, spin_y);       /* self spin */
        x += orbit_cx; y += orbit_cy; z += orbit_cz;
        rot_xy(&x, &y, &z, tilt_ax, tilt_ay);     /* ring tilt */
        vx[i] = x; vy[i] = y; vz[i] = z;
    }
    for (f = 0; f < nf; f++) {
        int idx[4];
        for (i = 0; i < fverts; i++) idx[i] = sf[f * fstride + i];
        emit_face(vx, vy, vz, idx, fverts, ramp);
    }
}

static void build_scene(int tick)
{
    /* ring tilt: mostly-in-screen-plane ellipse, wobbling so top passes
     * behind the picture and bottom in front, never fully edge-on */
    int ax = 10012 + (isin(tick * 110) * 3640 >> 12);   /* ~55 +- 20 deg */
    int ay = (isin(tick * 90 + 21000) * 5460 >> 12);    /* +- 30 deg     */
    int i, j, k;
    static int vx[RING_SEGA * RING_SEGB], vy[RING_SEGA * RING_SEGB], vz[RING_SEGA * RING_SEGB];

    nfaces = 0;

    /* torus ring */
    for (i = 0; i < RING_SEGA; i++) {
        int A = i * 65536 / RING_SEGA + tick * 40;      /* slow in-plane creep */
        int ca = icos(A), sa = isin(A);
        for (j = 0; j < RING_SEGB; j++) {
            int B = j * 65536 / RING_SEGB;
            int rad = RING_R + ((RING_r * icos(B)) >> 12);
            int x = (rad * ca) >> 12;
            int y = (rad * sa) >> 12;
            int z = (RING_r * isin(B)) >> 12;
            rot_xy(&x, &y, &z, ax, ay);
            k = i * RING_SEGB + j;
            vx[k] = x; vy[k] = y; vz[k] = z;
        }
    }
    for (i = 0; i < RING_SEGA; i++) {
        int i1 = (i + 1) % RING_SEGA;
        for (j = 0; j < RING_SEGB; j++) {
            int j1 = (j + 1) % RING_SEGB;
            int idx[4];
            idx[0] = i * RING_SEGB + j;
            idx[1] = i1 * RING_SEGB + j;
            idx[2] = i1 * RING_SEGB + j1;
            idx[3] = i * RING_SEGB + j1;
            emit_face(vx, vy, vz, idx, 4, P_BLUE);
        }
    }

    /* three solid shapes orbiting the ring */
    for (k = 0; k < 3; k++) {
        int phi = tick * 230 + k * 21845;
        int ocx = (ORBIT_R * icos(phi)) >> 12;
        int ocy = (ORBIT_R * isin(phi)) >> 12;
        if (k == 0)
            build_solid(cube_v, 8, &cube_f[0][0], 4, 4, 6,
                        26, tick * 700, tick * 500 + 9000,
                        ocx, ocy, 0, ax, ay, P_GOLD);
        else if (k == 1)
            build_solid(octa_v, 6, &octa_f[0][0], 3, 3, 8,
                        34, tick * 620 + 30000, tick * 840,
                        ocx, ocy, 0, ax, ay, P_RED);
        else
            build_solid(tetra_v, 4, &tetra_f[0][0], 3, 3, 4,
                        30, tick * 900, tick * 640 + 17000,
                        ocx, ocy, 0, ax, ay, P_GREEN);
    }

    sort_faces();
}

/* ---- backdrop / stars / raster bars ------------------------------------- */
static void draw_backdrop(unsigned char *fb)
{
    int y;
    for (y = 0; y < H; y++)
        memset(fb + (long)y * W, bgpen[y], (size_t)W);
}

static void draw_raster_bar(unsigned char *fb, int cy, int base)
{
    int r;
    for (r = -11; r <= 11; r++) {
        int y = cy + r;
        int d = r < 0 ? -r : r;
        unsigned char pen = (unsigned char)(P_RAINBOW + ((base + (11 - d)) & 31));
        if (y >= 0 && y < H)
            memset(fb + (long)y * W, pen, (size_t)W);
    }
}

static void draw_bars(unsigned char *fb, int tick)
{
    draw_raster_bar(fb, CY + (isin(tick * 240) * (CY - 40) >> 12), tick >> 1);
    draw_raster_bar(fb, CY + (isin(tick * 170 + 30000) * (CY - 40) >> 12), (tick >> 1) + 16);
}

static void draw_stars(unsigned char *fb, int tick)
{
    int i;
    for (i = 0; i < 140; i++) {
        int depth = 1 + (i & 3);
        int x = (i * 97 + 131 * (i & 7) + tick * depth) % W;
        int y = (i * 211 + ((i * i) & 127)) % H;
        unsigned char c = depth == 4 ? 15 : (depth == 3 ? 14 : (unsigned char)(P_RAINBOW + ((i + (tick >> 3)) & 31)));
        x = W - 1 - x;
        fb[(long)y * W + x] = c;
        if (depth == 4 && x + 1 < W)
            fb[(long)y * W + x + 1] = c;
    }
}

/* ---- picture painted piece by piece -------------------------------------- */
static void draw_picture(unsigned char *fb, int tick)
{
    int t = tick % CYC_TOTAL;
    int b, r;
    int frame_pen;

    /* rainbow frame, always visible */
    for (r = 0; r < 4; r++) {
        frame_pen = P_RAINBOW + ((tick + r * 3) & 31);
        fill_rect(fb, pic_x - 6 + r, pic_y - 6 + r,
                  demo_pic_w + 12 - r * 2, 1, (unsigned char)frame_pen);
        fill_rect(fb, pic_x - 6 + r, pic_y + demo_pic_h + 5 - r,
                  demo_pic_w + 12 - r * 2, 1, (unsigned char)frame_pen);
        fill_rect(fb, pic_x - 6 + r, pic_y - 6 + r, 1,
                  demo_pic_h + 12 - r * 2, (unsigned char)frame_pen);
        fill_rect(fb, pic_x + demo_pic_w + 5 - r, pic_y - 6 + r, 1,
                  demo_pic_h + 12 - r * 2, (unsigned char)frame_pen);
    }

    fill_rect(fb, pic_x, pic_y, demo_pic_w, demo_pic_h, 0);

    for (b = 0; b < nblocks; b++) {
        int rank = blk_rank[b];
        int bx = (b % blk_cols) * BLOCK, by = (b / blk_cols) * BLOCK;
        int visible = 0, flash = 0;
        if (t < CYC_BUILD) {
            visible = rank < t;
            flash = visible && (t - rank) <= 2;
        } else if (t < CYC_BUILD + CYC_HOLD) {
            visible = 1;
        } else if (t < CYC_BUILD + CYC_HOLD + CYC_DISSOLVE) {
            int removed = (t - CYC_BUILD - CYC_HOLD) * 3;
            visible = (nblocks - 1 - rank) >= removed;
        }
        if (!visible) continue;
        if (flash) {
            fill_rect(fb, pic_x + bx, pic_y + by, BLOCK, BLOCK, 15);
        } else {
            int row;
            unsigned char *dst = fb + (long)(pic_y + by) * W + pic_x + bx;
            const unsigned char *src = pic_pens + (long)by * demo_pic_w + bx;
            for (row = 0; row < BLOCK; row++) {
                memcpy(dst, src, BLOCK);
                dst += W;
                src += demo_pic_w;
            }
        }
    }
}

/* ---- rainbow text -------------------------------------------------------- */
static int text_width(const char *s, int scale)
{
    int w = 0;
    while (*s++) w += 6 * scale;
    return w - scale;
}

/* one glyph, rainbow-striped per scanline, with a drop shadow */
static void draw_glyph_rainbow(unsigned char *fb, char ch, int x, int y,
                               int scale, int tick)
{
    const unsigned char *g = font5x7[glyph_index(ch)];
    int gy, gx, sub;
    if (ch == ' ') return;
    for (gy = 0; gy < 7; gy++) {                     /* shadow first */
        for (gx = 0; gx < 5; gx++)
            if (g[gy] & (1 << (4 - gx)))
                fill_rect(fb, x + gx * scale + 4, y + gy * scale + 4, scale, scale, 0);
    }
    for (gy = 0; gy < 7; gy++) {
        for (sub = 0; sub < scale; sub++) {
            int sy = y + gy * scale + sub;
            unsigned char pen;
            if (sy < 0 || sy >= H) continue;
            pen = (unsigned char)(P_RAINBOW + (((sy >> 2) - tick) & 31));
            for (gx = 0; gx < 5; gx++)
                if (g[gy] & (1 << (4 - gx)))
                    fill_rect(fb, x + gx * scale, sy, scale, 1, pen);
        }
    }
}

static const char scroll_text[] =
    "                WHITTY ARCADE PRESENTS A DEMO INTRO IN THE STYLE OF THE "
    "GOLDEN AGE ...   RED SECTOR - KEFRENS - FAIRLIGHT - SANITY - SPACEBALLS "
    "- WE SALUTE YOU ...   WATCH THE FILLED VECTOR RING SPIN WHILE THE "
    "PICTURE PAINTS ITSELF PIECE BY PIECE ...   PAULA IS SINGING OUR MOD "
    "TUNE ON ALL FOUR CHANNELS ...   GREETINGS TO ALL AMIGA ARCADE FANS "
    "EVERYWHERE ...   CODED BY CLAUDE FOR JON IN 2026 ...   "
    "PRESS ESC OR FIRE TO EXIT ...   AND NOW WE WRAP AROUND       ";

/* variant 1: boot intro in front of a game HDF. Which game is named is chosen
 * at build time (-DGAME_PDRIFT -> Power Drift, default -> Chase H.Q.); the
 * WhittyDemo binary is rebuilt per game anyway because the picture is baked in. */
#ifdef GAME_PDRIFT
static const char scroll_text_game[] =
    "                WHITTY ARCADE PRESENTS ...   POWER DRIFT BY SEGA 1988 "
    "...   SEGA Y-BOARD - 68000 CPUS PLUS Z80 PLUS YM2151 AND SEGA PCM - "
    "ALL EMULATED LIVE ON YOUR AMIGA ...   GET READY TO RACE ...   FLOOR IT "
    "THROUGH THE COURSES ...   PRESS FIRE TO START THE GAME ...   ESC QUITS "
    "...   PAULA IS SINGING OUR MOD TUNE ON ALL FOUR CHANNELS "
    "...   GREETINGS TO ALL AMIGA ARCADE FANS EVERYWHERE ...   "
    "AND NOW WE WRAP AROUND       ";
#else
static const char scroll_text_game[] =
    "                WHITTY ARCADE PRESENTS ...   CHASE H.Q. BY TAITO 1988 "
    "...   DUAL 68000 PLUS Z80 PLUS YM2610 - ALL EMULATED LIVE ON YOUR "
    "AMIGA ...   NANCY HERE - WE GOT AN EMERGENCY ...   GET THE SUSPECT "
    "VEHICLE ...   PRESS FIRE TO START THE GAME ...   ESC QUITS "
    "...   PAULA IS SINGING OUR MOD TUNE ON ALL FOUR CHANNELS "
    "...   GREETINGS TO ALL AMIGA ARCADE FANS EVERYWHERE ...   "
    "AND NOW WE WRAP AROUND       ";
#endif

void demo_set_text_variant(int v) { text_variant = v; }
void demo_set_selector_selection(int sel) { selector_selection = sel ? 1 : 0; }

static void draw_scroller(unsigned char *fb, int tick)
{
    const int scale = 6;
    static const char scroll_text_selector[] =
        "                WHITTY ARCADE PRESENTS THE CAPCOM RUN AND GUN DOUBLE BILL "
        "...   COMMANDO BY CAPCOM 1985 "
        "...   GUN.SMOKE BY CAPCOM 1985 "
        "...   BOTH RUN THE CAPCOM Z80 ARCADE HARDWARE LIVE ON YOUR AMIGA "
        "...   AND NOW WE WRAP AROUND       ";
    const char *text = text_variant == 2 ? scroll_text_selector :
                       (text_variant ? scroll_text_game : scroll_text);
    int textlen = text_variant == 2 ? (int)(sizeof(scroll_text_selector) - 1) :
                  (text_variant ? (int)(sizeof(scroll_text_game) - 1)
                                : (int)(sizeof(scroll_text) - 1));
    int tw = textlen * 6 * scale;
    int x = W - ((tick * 4) % (tw + W));
    const char *s = text;
    for (; *s; s++, x += 6 * scale) {
        int wave;
        if (x < -5 * scale || x >= W) continue;
        wave = (isin(tick * 700 + x * 60) * 14) >> 12;
        draw_glyph_rainbow(fb, *s, x, 408 + wave, scale, tick);
    }
}

static void draw_title(unsigned char *fb, int tick)
{
    static const char title[] = "WHITTY ARCADE";
    const int scale = 5;
    int x = (W - text_width(title, scale)) / 2;
    int i;
    for (i = 0; title[i]; i++, x += 6 * scale) {
        const unsigned char *g = font5x7[glyph_index(title[i])];
        int wave = (isin(tick * 500 + i * 5000) * 8) >> 12;
        unsigned char pen = (unsigned char)(P_RAINBOW + ((i * 3 - (tick >> 1)) & 31));
        int gy, gx;
        if (title[i] == ' ') continue;
        for (gy = 0; gy < 7; gy++)
            for (gx = 0; gx < 5; gx++)
                if (g[gy] & (1 << (4 - gx))) {
                    fill_rect(fb, x + gx * scale + 3, 16 + wave + gy * scale + 3, scale, scale, 0);
                    fill_rect(fb, x + gx * scale, 16 + wave + gy * scale, scale, scale, pen);
                }
    }
}

/* wavy thick rainbow text, horizontally centred (the chosen game's name) */
static void draw_wavy_center(unsigned char *fb, const char *s, int y, int scale, int tick)
{
    int x = (W - text_width(s, scale)) / 2;
    for (; *s; s++, x += 6 * scale) {
        int wave = (isin(tick * 600 + x * 90) * 9) >> 12;
        if (*s != ' ') draw_glyph_rainbow(fb, *s, x, y + wave, scale, tick);
    }
}

/* thick pulsing/solid frame of thickness b around an x,y,w,h box */
static void frame_box(unsigned char *fb, int x, int y, int w, int h,
                      int b, unsigned char pen)
{
    fill_rect(fb, x - b, y - b, w + 2 * b, b, pen);
    fill_rect(fb, x - b, y + h, w + 2 * b, b, pen);
    fill_rect(fb, x - b, y - b, b, h + 2 * b, pen);
    fill_rect(fb, x + w, y - b, b, h + 2 * b, pen);
}

/* blit one revealed BLOCKxBLOCK tile of the composite (or a white flash) */
static void blit_block(unsigned char *fb, int dx, int dy, int sx, int sy,
                       int flash)
{
    int row;
    unsigned char *dst = fb + (long)dy * W + dx;
    const unsigned char *src = pic_pens + (long)sy * demo_pic_w + sx;
    for (row = 0; row < BLOCK; row++) {
        if (flash) memset(dst, 15, BLOCK);
        else       memcpy(dst, src, BLOCK);
        dst += W;
        src += demo_pic_w;
    }
}

/* Two-game selector. The baked picture is a 2S x S composite: left square =
 * Commando art, right square = GunSmoke art. Two square panels are drawn side by
 * side; only the selected game's panel is unveiled, replaying when switched. */
static void draw_selector(unsigned char *fb, int tick)
{
    static int sel_prev = -1, sel_change_tick;
    const int GAP = 30, b = 5;
    int S = demo_pic_h;
    int bps = S / BLOCK;
    int ox = (W - (2 * S + GAP)) / 2;
    int oy = CY - S / 2;
    int lpx = ox, rpx = ox + S + GAP;
    int selpx = selector_selection ? rpx : lpx;
    int src_col0 = selector_selection ? S : 0;
    int cy = oy + S / 2, i, c, r, diag, maxdiag;
    unsigned char hl = (unsigned char)(P_RAINBOW + ((tick >> 1) & 31));
    unsigned char lp = selector_selection ? 14 : hl;
    unsigned char rp = selector_selection ? hl : 14;

    if (selector_selection != sel_prev) {
        sel_prev = selector_selection;
        sel_change_tick = tick;
    }
    diag = (tick - sel_change_tick) / 3;
    maxdiag = 2 * (bps - 1);

    fill_rect(fb, lpx, oy, S, S, 0);
    fill_rect(fb, rpx, oy, S, S, 0);
    for (r = 0; r < bps; r++)
        for (c = 0; c < bps; c++) {
            int d = c + r;
            if (d > diag) continue;
            blit_block(fb, selpx + c * BLOCK, oy + r * BLOCK,
                       src_col0 + c * BLOCK, r * BLOCK,
                       d == diag && diag <= maxdiag);
        }

    frame_box(fb, selector_selection ? lpx : rpx, oy, S, S, b, 14);
    frame_box(fb, selpx, oy, S, S, b, hl);

    for (i = 0; i < 22; i++) {
        fill_rect(fb, lpx - 44 + i, cy - i, 6, 5, lp);
        fill_rect(fb, lpx - 44 + i, cy + i, 6, 5, lp);
        fill_rect(fb, rpx + S + 38 - i, cy - i, 6, 5, rp);
        fill_rect(fb, rpx + S + 38 - i, cy + i, 6, 5, rp);
    }

    draw_wavy_center(fb, selector_selection ? "GUN.SMOKE" : "COMMANDO", 20, 8, tick);
}

/* ---- init / frame -------------------------------------------------------- */
void demo_init(int w, int h)
{
    long i;
    unsigned r = 0x57a7e5ee;
    W = w; H = h; CX = w / 2; CY = h / 2 - 11;

    build_palette();

    /* per-row backdrop pen: dark navy sky over a purple glow band */
    for (i = 0; i < H && i < 600; i++) {
        int t = (int)(i * 25 / H);                     /* 0..24 */
        int pen;
        if (t <= 12) pen = 1 + t;                      /* darken down to horizon */
        else pen = 13 - (t - 12) / 2;                  /* and back out */
        if (pen < 1) pen = 1;
        if (pen > 13) pen = 13;
        bgpen[i] = (unsigned char)pen;
    }

    /* pre-offset picture pens once so block blits are straight memcpys */
    for (i = 0; i < (long)demo_pic_w * demo_pic_h; i++)
        pic_pens[i] = (unsigned char)(demo_pic_data[i] + PIC_PEN_BASE);

    pic_x = CX - demo_pic_w / 2;
    pic_y = CY - demo_pic_h / 2;

    /* deterministic shuffle: block -> reveal rank */
    blk_cols = demo_pic_w / BLOCK;
    blk_rows = demo_pic_h / BLOCK;
    nblocks = blk_cols * blk_rows;
    {
        static unsigned short order[MAX_BLOCKS];
        int n;
        for (n = 0; n < nblocks; n++) order[n] = (unsigned short)n;
        for (n = nblocks - 1; n > 0; n--) {
            int j;
            r = r * 1103515245u + 12345u;
            j = (int)((r >> 16) % (unsigned)(n + 1));
            {
                unsigned short tmp = order[n];
                order[n] = order[j];
                order[j] = tmp;
            }
        }
        for (n = 0; n < nblocks; n++)
            blk_rank[order[n]] = (unsigned short)n;
    }
}

void demo_frame(unsigned char *fb, int tick)
{
    int i;

    draw_backdrop(fb);
    draw_bars(fb, tick);
    draw_stars(fb, tick);

    if (text_variant == 0) {
        build_scene(tick);

        for (i = 0; i < nfaces; i++)          /* ring behind the picture */
            if (faces[i].z > 0)
                fill_face(fb, &faces[i]);
    }

    if (text_variant != 2)
        draw_picture(fb, tick);

    if (text_variant == 0) {
        for (i = 0; i < nfaces; i++)          /* ring in front */
            if (faces[i].z <= 0)
                fill_face(fb, &faces[i]);
    }

    if (text_variant != 2)
        draw_title(fb, tick);
    if (text_variant == 2)
        draw_selector(fb, tick);
    draw_scroller(fb, tick);
}
