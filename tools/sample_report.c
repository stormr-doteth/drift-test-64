/* Builds on a PC: writes a sample report with made-up results to stdout,
 * so the printable layout can be previewed without a console. */
#include <math.h>
#include <string.h>
#include "report.h"

static uint8_t visited[MAP_N][MAP_N];

static void visit(int x, int y)
{
    int mx = (int)lroundf(x * STICK_SCALE), my = (int)lroundf(y * STICK_SCALE);
    if (mx < -MAP_R || mx > MAP_R || my < -MAP_R || my > MAP_R) return;
    visited[my + MAP_R][mx + MAP_R] = 1;
}

int main(void)
{
    report_t r;
    memset(&r, 0, sizeof(r));
    r.report_no = 7;
    strcpy(r.date, "2026-11-20 16:42");
    r.port = 1;
    r.ctrl_type = "N64 CONTROLLER";
    r.pak = "RUMBLE PAK";
    for (int i = 0; i < NUM_BTN; i++) { r.passed[i] = true; r.count[i] = 2 + (i * 7) % 9; }

    /* a slightly worn stick: good cardinals, weaker down-left */
    static const float reach[8] = { 83, 92, 81, 90, 79, 74, 77, 91 };
    for (int k = 0; k < 8; k++) {
        float a = k * (float)(M_PI / 4);
        r.sector_max[k] = reach[k];
        r.sector_x[k] = (int)lroundf(cosf(a) * reach[k]);
        r.sector_y[k] = (int)lroundf(sinf(a) * reach[k]);
    }
    /* coverage: spins along the measured gate plus some inner wiggles */
    for (int s = 0; s < 3600; s++) {
        float a = s * (float)(2 * M_PI / 3600);
        int k = (int)lroundf(a / (float)(M_PI / 4)) & 7;
        for (float f = 0.15f; f <= 1.0f; f += 0.17f) {
            visit((int)lroundf(cosf(a) * reach[k] * f), (int)lroundf(sinf(a) * reach[k] * f));
        }
        visit((int)lroundf(cosf(a) * reach[k]), (int)lroundf(sinf(a) * reach[k]));
    }
    r.minx = -79; r.maxx = 83; r.miny = -77; r.maxy = 81;
    r.have_ctr = true; r.ctr_x = 1; r.ctr_y = -2; r.ctr_worst = 2;
    r.jitter = 0;
    r.visited = (const uint8_t (*)[MAP_N])visited;

    report_write_html(stdout, &r);
    return 0;
}
