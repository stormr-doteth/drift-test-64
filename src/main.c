/*
 * N64 Controller Tester (libdragon)
 *
 *  - Every button shows one of three states:
 *      grey   = not pressed yet (untested)
 *      yellow = being held right now
 *      green  = has been pressed at least once (works)
 *  - Analog stick: live position, coverage trail, per-direction max range
 *    vs a new-OEM reference gate, min/max per axis, return-to-center error
 *    (the classic "loose stick" / drift symptom on N64 optical sticks) and
 *    resting jitter.
 *  - All 4 ports, hot-plug, accessory (pak) detection, rumble test.
 *  - Hold START: menu -> save a printable HTML report to the SD card,
 *    view the report on screen, adjust screen size for CRTs, reset.
 *
 *  Controls:
 *    Hold START         open the menu
 *    Z + B              run the Rumble Pak (if one is inserted)
 *    L + R + START      controller's own hardware re-center
 *    Press any button on another port to switch to it.
 */
#include <libdragon.h>
#include <malloc.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "report.h"

#define NPORTS 4
#define CW 320                 /* layout canvas */
#define CH 240

/* ---------- stick panel geometry ---------- */
#define SX   188
#define SY   34
#define SW   112
#define SCX  (SX + SW / 2)
#define SCY  (SY + SW / 2)

#define MENU_HOLD_FRAMES 40
#define SD_DIR  "sd:/n64padtester"

/* ---------- buttons ---------- */
typedef enum { HUE_GREY, HUE_BLUE, HUE_GREEN, HUE_RED, HUE_YELLOW } hue_t;

typedef struct {
    const char *label;
    int x, y, w, h;
    hue_t hue;
} btn_layout_t;

static const btn_layout_t LAYOUT[NUM_BTN] = {
    [B_A]     = { "A",     134, 116, 18, 14, HUE_BLUE   },
    [B_B]     = { "B",     110, 104, 18, 14, HUE_GREEN  },
    [B_Z]     = { "Z",      72,  36, 28, 13, HUE_GREY   },
    [B_START] = { "START",  64,  78, 44, 13, HUE_RED    },
    [B_DU]    = { "^",      29,  64, 15, 13, HUE_GREY   },
    [B_DD]    = { "v",      29,  92, 15, 13, HUE_GREY   },
    [B_DL]    = { "<",      14,  78, 15, 13, HUE_GREY   },
    [B_DR]    = { ">",      44,  78, 15, 13, HUE_GREY   },
    [B_L]     = { "L",      14,  36, 40, 13, HUE_GREY   },
    [B_R]     = { "R",     130,  36, 40, 13, HUE_GREY   },
    [B_CU]    = { "^",     139,  58, 18, 13, HUE_YELLOW },
    [B_CD]    = { "v",     139,  86, 18, 13, HUE_YELLOW },
    [B_CL]    = { "<",     120,  72, 18, 13, HUE_YELLOW },
    [B_CR]    = { ">",     158,  72, 18, 13, HUE_YELLOW },
};

static bool btn_get(joypad_buttons_t b, int i)
{
    switch (i) {
    case B_A: return b.a;         case B_B: return b.b;
    case B_Z: return b.z;         case B_START: return b.start;
    case B_DU: return b.d_up;     case B_DD: return b.d_down;
    case B_DL: return b.d_left;   case B_DR: return b.d_right;
    case B_L: return b.l;         case B_R: return b.r;
    case B_CU: return b.c_up;     case B_CD: return b.c_down;
    case B_CL: return b.c_left;   case B_CR: return b.c_right;
    }
    return false;
}

/* ---------- per-port test state ---------- */
#define HIST 16

typedef struct {
    bool     passed[NUM_BTN];
    uint16_t count[NUM_BTN];
    int      last_btn;

    float    sector_max[8];
    int      sector_x[8], sector_y[8];
    int      minx, maxx, miny, maxy;
    uint8_t  visited[MAP_N][MAP_N];

    /* return-to-center */
    bool     armed;
    int      settle;
    int      prevx, prevy;
    bool     have_ctr;
    int      ctr_x, ctr_y;
    int      ctr_worst;

    /* resting jitter */
    int      hx[HIST], hy[HIST], hi, hn;
    int      jitter;          /* -1 = not measured yet */

    int      start_hold;
    bool     ignore_start;    /* until START is released */
    bool     rumble_on;
} port_state_t;

static port_state_t ps[NPORTS];

static void port_reset(port_state_t *p)
{
    bool rumble = p->rumble_on;
    memset(p, 0, sizeof(*p));
    p->last_btn = -1;
    p->jitter = -1;
    p->rumble_on = rumble;
}

/* ---------- colours ---------- */
static uint32_t C_BG, C_PANEL, C_GRID, C_TEXT, C_DIM, C_TITLE;
static uint32_t C_HELD, C_OK, C_NONE, C_WARN, C_BAD, C_BLACK, C_WHITE;
static uint32_t C_REF, C_COV, C_POLY, C_CUR;
static uint32_t HUE[5];

static void init_colors(void)
{
    C_BG    = graphics_make_color(12, 14, 24, 255);
    C_PANEL = graphics_make_color(24, 28, 44, 255);
    C_GRID  = graphics_make_color(50, 56, 80, 255);
    C_TEXT  = graphics_make_color(225, 230, 240, 255);
    C_DIM   = graphics_make_color(120, 126, 150, 255);
    C_TITLE = graphics_make_color(120, 190, 255, 255);
    C_HELD  = graphics_make_color(255, 210, 40, 255);
    C_OK    = graphics_make_color(40, 190, 80, 255);
    C_NONE  = graphics_make_color(44, 46, 58, 255);
    C_WARN  = graphics_make_color(255, 150, 40, 255);
    C_BAD   = graphics_make_color(240, 60, 60, 255);
    C_BLACK = graphics_make_color(0, 0, 0, 255);
    C_WHITE = graphics_make_color(255, 255, 255, 255);
    C_REF   = graphics_make_color(70, 90, 160, 255);
    C_COV   = graphics_make_color(60, 120, 90, 255);
    C_POLY  = graphics_make_color(120, 255, 150, 255);
    C_CUR   = graphics_make_color(255, 255, 255, 255);
    HUE[HUE_GREY]   = graphics_make_color(150, 150, 160, 255);
    HUE[HUE_BLUE]   = graphics_make_color(60, 110, 255, 255);
    HUE[HUE_GREEN]  = graphics_make_color(40, 200, 90, 255);
    HUE[HUE_RED]    = graphics_make_color(230, 50, 50, 255);
    HUE[HUE_YELLOW] = graphics_make_color(250, 210, 40, 255);
}

/* ---------- drawing helpers ---------- */
static void text(surface_t *d, int x, int y, uint32_t col, const char *fmt, ...)
{
    char buf[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    graphics_set_color(col, 0);   /* 0 = transparent background */
    graphics_draw_text(d, x, y, buf);
}

static void rect_outline(surface_t *d, int x, int y, int w, int h, uint32_t c)
{
    graphics_draw_box(d, x, y, w, 1, c);
    graphics_draw_box(d, x, y + h - 1, w, 1, c);
    graphics_draw_box(d, x, y, 1, h, c);
    graphics_draw_box(d, x + w - 1, y, 1, h, c);
}

static int stick_px(int v) { return SCX + (int)lroundf(v * STICK_SCALE); }
static int stick_py(int v) { return SCY - (int)lroundf(v * STICK_SCALE); }

/* sector 0 = right, then counter-clockwise in 45 degree steps */
static int sector_of(int x, int y)
{
    float a = atan2f((float)y, (float)x);
    int k = (int)lroundf(a / (float)(M_PI / 4.0));
    return (k + 8) & 7;
}

static uint32_t grade_color(int g) { return g == 0 ? C_OK : g == 1 ? C_WARN : C_BAD; }

/* ---------- per-frame update ---------- */
static void update_port(int port, port_state_t *p)
{
    joypad_inputs_t in = joypad_get_inputs(port);
    joypad_buttons_t pressed = joypad_get_buttons_pressed(port);

    for (int i = 0; i < NUM_BTN; i++) {
        if (i == B_START && p->ignore_start) continue;
        if (btn_get(pressed, i)) {
            p->passed[i] = true;
            if (p->count[i] < 9999) p->count[i]++;
            p->last_btn = i;
        }
    }
    if (p->ignore_start && !in.btn.start) p->ignore_start = false;

    /* rumble while Z+B held */
    bool want = in.btn.z && in.btn.b && joypad_get_rumble_supported(port);
    if (want != p->rumble_on) {
        joypad_set_rumble_active(port, want);
        p->rumble_on = want;
    }

    /* stick */
    int x = in.stick_x, y = in.stick_y;
    if (x < p->minx) p->minx = x;
    if (x > p->maxx) p->maxx = x;
    if (y < p->miny) p->miny = y;
    if (y > p->maxy) p->maxy = y;

    int mx = (int)lroundf(x * STICK_SCALE), my = (int)lroundf(y * STICK_SCALE);
    if (mx < -MAP_R) mx = -MAP_R;
    if (mx >  MAP_R) mx =  MAP_R;
    if (my < -MAP_R) my = -MAP_R;
    if (my >  MAP_R) my =  MAP_R;
    p->visited[my + MAP_R][mx + MAP_R] = 1;

    float mag = sqrtf((float)(x * x + y * y));
    if (mag > 20.0f) {
        int k = sector_of(x, y);
        if (mag > p->sector_max[k]) {
            p->sector_max[k] = mag;
            p->sector_x[k] = x;
            p->sector_y[k] = y;
        }
    }

    /* return-to-center: after a big push, wait for the stick to settle,
     * then record where it came to rest. */
    if (mag > 50.0f) {
        p->armed = true;
        p->settle = 0;
    } else if (p->armed && mag < 25.0f) {
        if (abs(x - p->prevx) <= 1 && abs(y - p->prevy) <= 1) p->settle++;
        else p->settle = 0;
        if (p->settle >= 20) {
            p->armed = false;
            p->have_ctr = true;
            p->ctr_x = x;
            p->ctr_y = y;
            int err = abs(x) > abs(y) ? abs(x) : abs(y);
            if (err > p->ctr_worst) p->ctr_worst = err;
        }
    }
    p->prevx = x;
    p->prevy = y;

    /* resting jitter: spread of the last HIST samples while near center */
    p->hx[p->hi] = x;
    p->hy[p->hi] = y;
    p->hi = (p->hi + 1) % HIST;
    if (p->hn < HIST) p->hn++;
    if (p->hn == HIST) {
        int x0 = 127, x1 = -128, y0 = 127, y1 = -128;
        bool near = true;
        for (int i = 0; i < HIST; i++) {
            if (abs(p->hx[i]) > 20 || abs(p->hy[i]) > 20) { near = false; break; }
            if (p->hx[i] < x0) x0 = p->hx[i];
            if (p->hx[i] > x1) x1 = p->hx[i];
            if (p->hy[i] < y0) y0 = p->hy[i];
            if (p->hy[i] > y1) y1 = p->hy[i];
        }
        if (near) p->jitter = (x1 - x0) > (y1 - y0) ? (x1 - x0) : (y1 - y0);
    }
}

/* ---------- names ---------- */
static const char *style_name(int port)
{
    if (port < 0) return "NONE";
    switch (joypad_get_style(port)) {
    case JOYPAD_STYLE_N64:   return "N64 CONTROLLER";
    case JOYPAD_STYLE_GCN:   return "GAMECUBE PAD";
    case JOYPAD_STYLE_MOUSE: return "N64 MOUSE";
    default:                 return "UNKNOWN";
    }
}

static const char *pak_name(int port)
{
    if (port < 0) return "NONE";
    switch (joypad_get_accessory_type(port)) {
    case JOYPAD_ACCESSORY_TYPE_RUMBLE_PAK:     return "RUMBLE PAK";
    case JOYPAD_ACCESSORY_TYPE_CONTROLLER_PAK: return "CONTROLLER PAK";
    case JOYPAD_ACCESSORY_TYPE_TRANSFER_PAK:   return "TRANSFER PAK";
    case JOYPAD_ACCESSORY_TYPE_BIO_SENSOR:     return "BIO SENSOR";
    case JOYPAD_ACCESSORY_TYPE_SNAP_STATION:   return "SNAP STATION";
    case JOYPAD_ACCESSORY_TYPE_NONE:           return "NONE";
    default:                                   return "UNKNOWN";
    }
}

/* ---------- report ---------- */
static bool sd_ok = false;
static bool rtc_ok = false;

static void fill_report(report_t *r, int port, const port_state_t *p)
{
    memset(r, 0, sizeof(*r));
    r->port = port + 1;
    r->ctrl_type = style_name(port);
    r->pak = pak_name(port);
    for (int i = 0; i < NUM_BTN; i++) { r->passed[i] = p->passed[i]; r->count[i] = p->count[i]; }
    for (int k = 0; k < 8; k++) {
        r->sector_max[k] = p->sector_max[k];
        r->sector_x[k] = p->sector_x[k];
        r->sector_y[k] = p->sector_y[k];
    }
    r->minx = p->minx; r->maxx = p->maxx; r->miny = p->miny; r->maxy = p->maxy;
    r->have_ctr = p->have_ctr; r->ctr_x = p->ctr_x; r->ctr_y = p->ctr_y; r->ctr_worst = p->ctr_worst;
    r->jitter = p->jitter;
    r->visited = (const uint8_t (*)[MAP_N])p->visited;

    if (rtc_ok) {
        rtc_time_t t;
        if (rtc_get(&t))
            snprintf(r->date, sizeof(r->date), "%04d-%02d-%02d %02d:%02d",
                     t.year, t.month + 1, t.day, t.hour, t.min);
    }
}

/* returns report number, or -1 on failure */
static int save_report(int port, const port_state_t *p)
{
    if (!sd_ok) return -1;
    mkdir(SD_DIR, 0777);
    char path[64];
    int n;
    for (n = 1; n < 1000; n++) {
        snprintf(path, sizeof(path), SD_DIR "/report_%03d.html", n);
        FILE *t = fopen(path, "r");
        if (!t) break;
        fclose(t);
    }
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    static report_t r;
    fill_report(&r, port, p);
    r.report_no = n;
    report_write_html(f, &r);
    bool err = ferror(f);
    fclose(f);
    debugf("[pad-tester] saved %s (%s)\n", path, err ? "error" : "ok");
    return err ? -1 : n;
}

/* ---------- screen size (CRT overscan) ---------- */
static const int SIZES[] = { 100, 96, 92, 88, 84, 80 };
#define NSIZES ((int)(sizeof(SIZES) / sizeof(SIZES[0])))
static int size_idx = 2;          /* default 92%: safe on most CRTs */
static int fb_w, fb_h;

static void apply_screen_size(bool first)
{
    int pct = SIZES[size_idx];
    /* A bigger framebuffer with the 320x240 picture centered in it makes
     * the VI scale the picture down, leaving a black border. */
    fb_w = ((CW * 100 / pct) + 3) & ~3;
    fb_h = ((CH * 100 / pct) + 1) & ~1;
    if (!first) display_close();
    display_init((resolution_t){ .width = fb_w, .height = fb_h, .interlaced = false },
                 DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
}

static void load_settings(void)
{
    if (!sd_ok) return;
    FILE *f = fopen(SD_DIR "/settings.txt", "r");
    if (!f) return;
    int pct = 0;
    if (fscanf(f, "screen_size=%d", &pct) == 1)
        for (int i = 0; i < NSIZES; i++) if (SIZES[i] == pct) size_idx = i;
    fclose(f);
}

static void save_settings(void)
{
    if (!sd_ok) return;
    mkdir(SD_DIR, 0777);
    FILE *f = fopen(SD_DIR "/settings.txt", "w");
    if (!f) return;
    fprintf(f, "screen_size=%d\n", SIZES[size_idx]);
    fclose(f);
}

/* ---------- rendering ---------- */
static void draw_buttons(surface_t *d, int port, const port_state_t *p, joypad_buttons_t held)
{
    graphics_draw_box(d, 29, 78, 15, 13, C_NONE);
    text(d, 144, 75, C_DIM, "C");

    for (int i = 0; i < NUM_BTN; i++) {
        const btn_layout_t *b = &LAYOUT[i];
        bool h = port >= 0 && btn_get(held, i);
        uint32_t fill, tc;
        if (h)                 { fill = C_HELD; tc = C_BLACK; }
        else if (p->passed[i]) { fill = C_OK;   tc = C_WHITE; }
        else                   { fill = C_NONE; tc = C_DIM;   }

        rect_outline(d, b->x, b->y, b->w, b->h, HUE[b->hue]);
        graphics_draw_box(d, b->x + 1, b->y + 1, b->w - 2, b->h - 2, fill);
        int len = strlen(b->label);
        text(d, b->x + (b->w - len * 8) / 2, b->y + (b->h - 8) / 2, tc, "%s", b->label);
    }
}

static void draw_stick(surface_t *d, const port_state_t *p, int cx, int cy, bool live)
{
    graphics_draw_box(d, SX, SY, SW + 1, SW + 1, C_PANEL);
    rect_outline(d, SX - 1, SY - 1, SW + 3, SW + 3, C_GRID);
    graphics_draw_line(d, SX, SCY, SX + SW, SCY, C_GRID);
    graphics_draw_line(d, SCX, SY, SCX, SY + SW, C_GRID);

    int dz = (int)lroundf(8 * STICK_SCALE);
    rect_outline(d, SCX - dz, SCY - dz, dz * 2 + 1, dz * 2 + 1, C_GRID);

    for (int k = 0; k < 8; k++) {
        int k2 = (k + 1) & 7;
        float a1 = k * (float)(M_PI / 4), a2 = k2 * (float)(M_PI / 4);
        int x1 = (int)lroundf(cosf(a1) * sector_ref(k));
        int y1 = (int)lroundf(sinf(a1) * sector_ref(k));
        int x2 = (int)lroundf(cosf(a2) * sector_ref(k2));
        int y2 = (int)lroundf(sinf(a2) * sector_ref(k2));
        graphics_draw_line(d, stick_px(x1), stick_py(y1), stick_px(x2), stick_py(y2), C_REF);
    }

    for (int j = 0; j < MAP_N; j++)
        for (int i = 0; i < MAP_N; i++)
            if (p->visited[j][i])
                graphics_draw_pixel(d, SCX + i - MAP_R, SCY - (j - MAP_R), C_COV);

    for (int k = 0; k < 8; k++) {
        int k2 = (k + 1) & 7;
        if (p->sector_max[k] > 0 && p->sector_max[k2] > 0)
            graphics_draw_line(d, stick_px(p->sector_x[k]), stick_py(p->sector_y[k]),
                               stick_px(p->sector_x[k2]), stick_py(p->sector_y[k2]), C_POLY);
    }
    for (int k = 0; k < 8; k++) {
        if (p->sector_max[k] <= 0) continue;
        float r = p->sector_max[k] / sector_ref(k);
        uint32_t c = r >= 0.90f ? C_OK : r >= 0.75f ? C_WARN : C_BAD;
        graphics_draw_box(d, stick_px(p->sector_x[k]) - 1, stick_py(p->sector_y[k]) - 1, 3, 3, c);
    }

    if (live) {
        int px = stick_px(cx), py = stick_py(cy);
        graphics_draw_line(d, SCX, SCY, px, py, C_DIM);
        graphics_draw_box(d, px - 2, py - 2, 5, 5, C_CUR);
    }
}

static void draw_header(surface_t *d, int active, const char *tv)
{
    text(d, 196, 25, C_DIM, "%s", tv);
    for (int port = 0; port < NPORTS; port++) {
        int bx = 236 + port * 18;
        bool conn = joypad_is_connected(port);
        uint32_t c = port == active ? C_HELD : conn ? C_OK : C_NONE;
        graphics_draw_box(d, bx, 23, 14, 11, c);
        text(d, bx + 4, 25, port == active ? C_BLACK : conn ? C_WHITE : C_DIM, "%d", port + 1);
    }
}

static void draw_test_screen(surface_t *d, int active, const port_state_t *p, joypad_inputs_t in, const char *tv)
{
    draw_header(d, active, tv);
    if (active >= 0)
        text(d, 16, 25, C_TITLE, "P%d %s", active + 1, style_name(active));
    else
        text(d, 16, 25, C_BAD, "NO CONTROLLER");

    draw_buttons(d, active, p, in.btn);
    draw_stick(d, p, in.stick_x, in.stick_y, active >= 0);

    int ok = 0;
    for (int i = 0; i < NUM_BTN; i++) ok += p->passed[i];
    text(d, 16, 152, ok == NUM_BTN ? C_OK : C_TEXT, "BUTTONS %2d/%d%s", ok, NUM_BTN, ok == NUM_BTN ? " ALL OK" : "");
    int dirs = dirs_reached(p->sector_max);
    text(d, 16, 162, dirs == 8 ? C_OK : C_TEXT, "STICK DIRS %d/8", dirs);
    if (p->last_btn >= 0)
        text(d, 16, 172, C_TEXT, "LAST %s x%d", BTN_NAMES[p->last_btn], p->count[p->last_btn]);
    else
        text(d, 16, 172, C_DIM, "LAST -");
    if (active >= 0) {
        text(d, 16, 182, C_TEXT, "PAK %s", pak_name(active));
        if (joypad_get_rumble_supported(active))
            text(d, 16, 192, p->rumble_on ? C_HELD : C_DIM, p->rumble_on ? "RUMBLING!" : "Z+B = RUMBLE");
    }

    int rx = SX;
    text(d, rx, 152, C_TEXT, "X%+04d  Y%+04d", in.stick_x, in.stick_y);
    bool any = false;
    for (int k = 0; k < 8; k++) any |= p->sector_max[k] > 0;
    if (any) {
        int pct = range_pct(p->sector_max), g = grade_range(pct);
        text(d, rx, 162, C_TEXT, "RANGE %3d%%", pct);
        text(d, rx + 88, 162, grade_color(g), "%s", word_range(g));
    } else {
        text(d, rx, 162, C_DIM, "RANGE  spin it");
    }
    text(d, rx, 172, C_TEXT, "X %+04d..%+04d", p->minx, p->maxx);
    text(d, rx, 182, C_TEXT, "Y %+04d..%+04d", p->miny, p->maxy);
    if (p->have_ctr) {
        int g = grade_ctr(p->ctr_worst);
        text(d, rx, 192, C_TEXT, "CTR %+d,%+d", p->ctr_x, p->ctr_y);
        text(d, rx + 88, 192, grade_color(g), "%.4s", word_ctr(g));
    } else {
        text(d, rx, 192, C_DIM, "CTR flick+drop");
    }
    if (p->jitter >= 0) {
        int g = grade_jitter(p->jitter);
        text(d, rx, 202, C_TEXT, "JITTER %d", p->jitter);
        text(d, rx + 88, 202, grade_color(g), "%.4s", word_jitter(g));
    } else {
        text(d, rx, 202, C_DIM, "JITTER -");
    }

    graphics_draw_box(d, 16, 214, 8, 8, C_NONE);
    text(d, 27, 214, C_DIM, "UNTESTED");
    graphics_draw_box(d, 100, 214, 8, 8, C_HELD);
    text(d, 111, 214, C_DIM, "HELD");
    graphics_draw_box(d, 152, 214, 8, 8, C_OK);
    text(d, 163, 214, C_DIM, "WORKS");

    if (p->start_hold > 10 && !p->ignore_start) {
        int w = (p->start_hold - 10) * 288 / (MENU_HOLD_FRAMES - 10);
        graphics_draw_box(d, 16, 224, 288, 10, C_NONE);
        graphics_draw_box(d, 16, 224, w, 10, C_TITLE);
        text(d, 112, 225, C_WHITE, "OPENING MENU");
    } else {
        text(d, 16, 225, C_DIM, "HOLD START: MENU / SAVE REPORT");
    }
}

static void draw_report_screen(surface_t *d, int active, const port_state_t *p, const char *tv)
{
    static report_t r;
    fill_report(&r, active, p);
    const char *headline;
    int v = report_verdict(&r, &headline);
    uint32_t vc = v < 0 ? C_DIM : grade_color(v);

    draw_header(d, active, tv);
    text(d, 16, 25, C_TITLE, "TEST REPORT  P%d", active + 1);

    /* verdict banner */
    graphics_draw_box(d, 14, 36, 166, 26, vc);
    const char *v1 = v < 0 ? "INCOMPLETE" : v == 0 ? "FULLY WORKING" : v == 1 ? "WORKING" : "STICK FAULTY";
    const char *v2 = v < 0 ? "keep testing" : v == 0 ? "all checks good" : v == 1 ? "stick shows wear" : "buttons work";
    text(d, 20, 40, C_BLACK, "%s", v1);
    text(d, 20, 51, C_BLACK, "%s", v2);

    int ok = buttons_ok(&r), y = 70;
    text(d, 16, y, ok == NUM_BTN ? C_OK : C_BAD, "BUTTONS %d/%d", ok, NUM_BTN);
    y += 10;
    for (int i = 0; i < NUM_BTN && y < 120; i++)
        if (!r.passed[i]) { text(d, 24, y, C_BAD, "NO %s", BTN_NAMES[i]); y += 10; }
    y += 4;
    if (stick_fully_moved(r.sector_max)) {
        int pct = range_pct(r.sector_max), g = grade_range(pct);
        text(d, 16, y, C_TEXT, "RANGE  %3d%%", pct);
        text(d, 120, y, grade_color(g), "%s", word_range(g));
    } else {
        text(d, 16, y, C_DIM, "RANGE  move stick all ways");
    }
    y += 10;
    if (r.have_ctr) {
        int g = grade_ctr(r.ctr_worst);
        text(d, 16, y, C_TEXT, "CENTER off %d", r.ctr_worst);
        text(d, 120, y, grade_color(g), "%s", word_ctr(g));
    } else text(d, 16, y, C_DIM, "CENTER not measured");
    y += 10;
    if (r.jitter >= 0) {
        int g = grade_jitter(r.jitter);
        text(d, 16, y, C_TEXT, "JITTER %d", r.jitter);
        text(d, 120, y, grade_color(g), "%s", word_jitter(g));
    } else text(d, 16, y, C_DIM, "JITTER not measured");
    y += 10;
    text(d, 16, y, C_TEXT, "PAK %s", r.pak);

    draw_stick(d, p, 0, 0, false);
    if (r.date[0]) text(d, SX, 152, C_DIM, "%s", r.date);

    text(d, 16, 214, C_TEXT, "A: SAVE TO SD CARD");
    text(d, 16, 225, C_DIM, "B: BACK TO TESTING");
}

enum { MI_SAVE, MI_VIEW, MI_SIZE, MI_RESET, MI_BACK, MI_COUNT };

static void draw_menu(surface_t *d, int sel)
{
    static const char *const items[MI_COUNT] = {
        "SAVE REPORT TO SD", "VIEW REPORT", "SCREEN SIZE", "RESET THIS PORT", "BACK",
    };
    int x = 64, y = 62, w = 192, h = 112;
    graphics_draw_box(d, x, y, w, h, C_PANEL);
    rect_outline(d, x, y, w, h, C_TITLE);
    text(d, x + 12, y + 8, C_TITLE, "MENU");
    for (int i = 0; i < MI_COUNT; i++) {
        int iy = y + 26 + i * 14;
        uint32_t c = i == sel ? C_HELD : C_TEXT;
        if (i == sel) text(d, x + 8, iy, C_HELD, ">");
        if (i == MI_SIZE) text(d, x + 20, iy, c, "SIZE  < %3d%% >", SIZES[size_idx]);
        else              text(d, x + 20, iy, c, "%s", items[i]);
    }
    text(d, x + 8, y + h - 12, C_DIM, "A:OK B:BACK <>:SIZE");
}

/* ---------- boot helpers ---------- */
static int boot_step = 0;
static void stage(const char *what)
{
    boot_step++;
    debugf("[pad-tester] step %d: %s\n", boot_step, what);
#ifdef DEBUG_BOOT
    static char lines[12][40];
    if (boot_step <= 12) snprintf(lines[boot_step - 1], 40, "%d. %s", boot_step, what);
    surface_t *d = display_try_get();
    if (d) {
        graphics_fill_screen(d, graphics_make_color(0, 0, 80, 255));
        graphics_set_color(graphics_make_color(255, 255, 255, 255), 0);
        graphics_draw_text(d, 24, 24, "PAD TESTER - DEBUG BOOT");
        for (int i = 0; i < boot_step && i < 12; i++)
            graphics_draw_text(d, 24, 44 + i * 12, lines[i]);
        display_show(d);
    }
    wait_ms(400);
#endif
}

/* Some flashcart menus (N64FlashcartMenu 0.3.4, used on SummerCart64) hand
 * over with the VI switched on but its timing registers zeroed. libdragon's
 * display_init() then waits for a vblank that never comes and the console
 * hangs with no video. If the VI is "on" with no sane V_SYNC, switch it off
 * first so display_init() programs it from scratch. */
static void vi_boot_guard(void)
{
    volatile uint32_t *vi = (volatile uint32_t *)0xA4400000;
    uint32_t ctrl  = vi[0];   /* VI_CTRL   */
    uint32_t vsync = vi[6];   /* VI_V_SYNC (NTSC 0x20D, PAL 0x271) */
    if ((ctrl & 3) != 0 && vsync < 0x100) {
        vi[0] = 0;
        MEMORY_BARRIER();
    }
}

/* copy the finished 320x240 canvas into the (possibly larger) framebuffer */
static void present(const surface_t *back)
{
    surface_t *fb = display_get();
    int ox = (fb_w - CW) / 2, oy = (fb_h - CH) / 2;
    uint8_t *dst = fb->buffer;
    const uint8_t *src = back->buffer;
    if (oy > 0) {
        memset(dst, 0, oy * fb->stride);
        memset(dst + (oy + CH) * fb->stride, 0, (fb_h - oy - CH) * fb->stride);
    }
    for (int y = 0; y < CH; y++) {
        uint8_t *row = dst + (oy + y) * fb->stride;
        if (ox > 0) {
            memset(row, 0, ox * 2);
            memset(row + (ox + CW) * 2, 0, fb->stride - (ox + CW) * 2);
        }
        memcpy(row + ox * 2, src + y * back->stride, CW * 2);
    }
    display_show(fb);
}

int main(void)
{
    vi_boot_guard();
    debug_init_isviewer();
    debug_init_usblog();
    debugf("[pad-tester] main() reached\n");

    sd_ok = debug_init_sdfs("sd:/", -1);
    load_settings();
    apply_screen_size(true);
    stage(sd_ok ? "video started, SD card found" : "video started, no SD card");
    timer_init();
    rtc_ok = rtc_init();
    joypad_init();
    stage("controllers started");
    init_colors();
    for (int i = 0; i < NPORTS; i++) port_reset(&ps[i]);

    void *backmem = memalign(64, CW * CH * 2);
    assertf(backmem, "out of memory for back buffer");
    surface_t back = surface_make(backmem, FMT_RGBA16, CW, CH, CW * 2);
    stage("starting tester");

    enum { MODE_TEST, MODE_MENU, MODE_REPORT } mode = MODE_TEST;
    int menu_sel = 0;
    char toast[40] = "";
    int toast_frames = 0;
    uint32_t toast_col = 0;
    int active = -1;
    uint32_t frame = 0;
    const char *tv = get_tv_type() == TV_PAL ? "PAL" : get_tv_type() == TV_MPAL ? "MPAL" : "NTSC";

    static port_state_t empty;
    port_reset(&empty);

    while (1) {
        joypad_poll();

        /* port bookkeeping (and test updates only while on the test screen) */
        for (int port = 0; port < NPORTS; port++) {
            if (!joypad_is_connected(port)) { ps[port].rumble_on = false; continue; }
            if (mode == MODE_TEST) {
                update_port(port, &ps[port]);
                if (joypad_get_buttons_pressed(port).raw != 0) active = port;
            }
        }
        if (active < 0 || !joypad_is_connected(active)) {
            active = -1;
            for (int port = 0; port < NPORTS; port++)
                if (joypad_is_connected(port)) { active = port; break; }
            if (active < 0) mode = MODE_TEST;
        }

        port_state_t *p = active >= 0 ? &ps[active] : &empty;
        joypad_inputs_t in = {0};
        joypad_buttons_t pr = {0};
        if (active >= 0) {
            in = joypad_get_inputs(active);
            pr = joypad_get_buttons_pressed(active);
        }

        /* hold START (alone) to open the menu */
        if (mode == MODE_TEST && active >= 0) {
            if (in.btn.start && !p->ignore_start && !in.btn.l && !in.btn.r) {
                if (++p->start_hold >= MENU_HOLD_FRAMES) {
                    mode = MODE_MENU;
                    menu_sel = 0;
                    p->start_hold = 0;
                    p->ignore_start = true;
                    if (p->rumble_on) { joypad_set_rumble_active(active, false); p->rumble_on = false; }
                }
            } else {
                p->start_hold = 0;
            }
        } else if (mode != MODE_TEST && active >= 0) {
            if (p->ignore_start && !in.btn.start) p->ignore_start = false;
        }

        int saved = 0;   /* 0 = no save this frame */
        if (mode == MODE_MENU) {
            if (pr.d_up || pr.c_up)     menu_sel = (menu_sel + MI_COUNT - 1) % MI_COUNT;
            if (pr.d_down || pr.c_down) menu_sel = (menu_sel + 1) % MI_COUNT;
            bool left = pr.d_left || pr.c_left, right = pr.d_right || pr.c_right;
            if (menu_sel == MI_SIZE && (left || right)) {
                int ni = size_idx + (left ? -1 : 1);
                if (ni >= 0 && ni < NSIZES) {
                    size_idx = ni;
                    apply_screen_size(false);
                    save_settings();
                }
            }
            if (pr.b || (pr.start && !p->ignore_start)) {
                mode = MODE_TEST;
                p->ignore_start = true;
            } else if (pr.a) {
                switch (menu_sel) {
                case MI_SAVE:  saved = 1; mode = MODE_TEST; break;
                case MI_VIEW:  mode = MODE_REPORT; break;
                case MI_SIZE:  break;
                case MI_RESET:
                    port_reset(p);
                    p->ignore_start = true;
                    mode = MODE_TEST;
                    snprintf(toast, sizeof(toast), "RESULTS RESET FOR PORT %d", active + 1);
                    toast_col = C_HELD; toast_frames = 120;
                    break;
                case MI_BACK:  mode = MODE_TEST; p->ignore_start = true; break;
                }
            }
        } else if (mode == MODE_REPORT) {
            if (pr.a) saved = 1;
            else if (pr.b || (pr.start && !p->ignore_start)) { mode = MODE_TEST; p->ignore_start = true; }
        }

        if (saved) {
            /* show a "saving" frame first: SD writes take a moment */
            graphics_fill_screen(&back, C_BG);
            text(&back, 100, 116, C_TEXT, "SAVING REPORT...");
            present(&back);
            int n = save_report(active, p);
            if (n > 0) {
                snprintf(toast, sizeof(toast), "SAVED REPORT_%03d.HTML", n);
                toast_col = C_OK;
            } else {
                snprintf(toast, sizeof(toast), sd_ok ? "SAVE FAILED" : "NO SD CARD FOUND");
                toast_col = C_BAD;
            }
            toast_frames = 180;
        }

        /* ---- draw ---- */
        surface_t *d = &back;
        graphics_fill_screen(d, C_BG);
        if (mode == MODE_REPORT) {
            draw_report_screen(d, active, p, tv);
        } else {
            draw_test_screen(d, active, p, in, tv);
            if (mode == MODE_MENU) draw_menu(d, menu_sel);
        }
        if (toast_frames > 0) {
            toast_frames--;
            graphics_draw_box(d, 14, 222, 292, 12, C_BLACK);
            text(d, 16, 225, toast_col, "%s", toast);
        }

        frame++;
#ifdef DEBUG_BOOT
        text(d, 248, 214, C_WARN, "F%06lu", (unsigned long)frame);
#endif
        if (frame == 1 || frame % 600 == 0) debugf("[pad-tester] frame %lu\n", (unsigned long)frame);

        present(&back);
    }
}
