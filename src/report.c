#include "report.h"
#include <math.h>

const char *const BTN_NAMES[NUM_BTN] = {
    [B_A] = "A", [B_B] = "B", [B_Z] = "Z", [B_START] = "START",
    [B_DU] = "D-Pad Up", [B_DD] = "D-Pad Down", [B_DL] = "D-Pad Left", [B_DR] = "D-Pad Right",
    [B_L] = "L", [B_R] = "R",
    [B_CU] = "C-Up", [B_CD] = "C-Down", [B_CL] = "C-Left", [B_CR] = "C-Right",
};

/* order of rows in the printed button table */
static const int BTN_ORDER[NUM_BTN] = {
    B_A, B_B, B_Z, B_START, B_L, B_R,
    B_DU, B_DD, B_DL, B_DR, B_CU, B_CD, B_CL, B_CR,
};

static const char *const DIR_NAMES[8] = {
    "Right", "Up-Right", "Up", "Up-Left", "Left", "Down-Left", "Down", "Down-Right"
};

float sector_ref(int k) { return (k & 1) ? REF_DIAG : REF_CARD; }

int range_pct(const float sector_max[8])
{
    float sum = 0;
    for (int k = 0; k < 8; k++) {
        float r = sector_max[k] / sector_ref(k);
        sum += r > 1.0f ? 1.0f : r;
    }
    return (int)lroundf(sum / 8.0f * 100.0f);
}

int dirs_reached(const float sector_max[8])
{
    int n = 0;
    for (int k = 0; k < 8; k++)
        if (sector_max[k] >= sector_ref(k) * 0.70f) n++;
    return n;
}

/* the stick was pushed out in every direction at least a little */
bool stick_fully_moved(const float sector_max[8])
{
    for (int k = 0; k < 8; k++)
        if (sector_max[k] < 40.0f) return false;
    return true;
}

int buttons_ok(const report_t *r)
{
    int n = 0;
    for (int i = 0; i < NUM_BTN; i++) n += r->passed[i];
    return n;
}

int grade_range(int pct)  { return pct >= 90 ? 0 : pct >= 75 ? 1 : 2; }
int grade_ctr(int worst)  { return worst <= 3 ? 0 : worst <= 8 ? 1 : 2; }
int grade_jitter(int j)   { return j <= 1 ? 0 : j <= 3 ? 1 : 2; }
const char *word_range(int g)  { return g == 0 ? "GOOD" : g == 1 ? "WORN" : "WEAK"; }
const char *word_ctr(int g)    { return g == 0 ? "GOOD" : g == 1 ? "LOOSE" : "BAD"; }
const char *word_jitter(int g) { return g == 0 ? "GOOD" : g == 1 ? "NOISY" : "BAD"; }

int report_verdict(const report_t *r, const char **headline)
{
    if (buttons_ok(r) < NUM_BTN || !stick_fully_moved(r->sector_max)) {
        *headline = "Test incomplete";
        return -1;
    }
    int worst = grade_range(range_pct(r->sector_max));
    if (r->have_ctr && grade_ctr(r->ctr_worst) > worst) worst = grade_ctr(r->ctr_worst);
    if (r->jitter >= 0 && grade_jitter(r->jitter) > worst) worst = grade_jitter(r->jitter);
    *headline = worst == 0 ? "Fully working"
              : worst == 1 ? "Working - analog stick shows wear"
              : "Buttons work - analog stick needs repair";
    return worst;
}

static const char *gcls(int g) { return g == 0 ? "good" : g == 1 ? "warn" : "bad"; }

static const char CSS[] =
    "@page{size:auto;margin:12mm}"
    "*{box-sizing:border-box}"
    "body{font-family:Arial,Helvetica,sans-serif;color:#111;margin:0;background:#fff;"
    "-webkit-print-color-adjust:exact;print-color-adjust:exact}"
    ".page{max-width:780px;margin:0 auto;padding:24px}"
    "header{display:flex;justify-content:space-between;align-items:flex-end;"
    "border-bottom:3px solid #111;padding-bottom:8px;gap:12px}"
    "h1{font-size:24px;margin:0}"
    ".sub{font-size:13px;color:#555;margin-top:2px}"
    ".meta{font-size:13px;text-align:right;line-height:1.5;white-space:nowrap}"
    ".verdict{margin:14px 0 4px;padding:12px 16px;border-radius:8px;color:#fff}"
    ".verdict b{font-size:22px;display:block}"
    ".verdict span{font-size:13px;opacity:.95}"
    ".v0{background:#1e8e3e}.v1{background:#d56e00}.v2{background:#c5221f}.vi{background:#5f6368}"
    ".cols{display:flex;gap:22px;flex-wrap:wrap}"
    ".col{flex:1;min-width:300px}"
    "h2{font-size:13px;text-transform:uppercase;letter-spacing:.06em;margin:16px 0 6px;"
    "border-bottom:1px solid #bbb;padding-bottom:3px}"
    "table{width:100%;border-collapse:collapse;font-size:13px}"
    "td,th{padding:3px 6px;border-bottom:1px solid #e6e6e6;text-align:left}"
    "th{font-size:11px;color:#555;text-transform:uppercase;font-weight:600}"
    "td.n{text-align:right;font-variant-numeric:tabular-nums}"
    ".good{color:#1e8e3e;font-weight:700}.warn{color:#d56e00;font-weight:700}"
    ".bad{color:#c5221f;font-weight:700}.na{color:#888}"
    "svg{width:100%;max-width:300px;display:block;margin:4px auto 0}"
    ".legend{font-size:11px;color:#444;text-align:center;margin-top:4px}"
    ".sw{display:inline-block;width:14px;height:3px;margin:0 4px 0 10px;vertical-align:3px}"
    ".explain{font-size:11px;color:#444;line-height:1.45;margin-top:8px}"
    ".notes{margin-top:18px;font-size:13px}"
    ".line{border-bottom:1px solid #999;height:24px}"
    "footer{margin-top:16px;font-size:11px;color:#555;border-top:1px solid #ccc;padding-top:6px}"
    "@media print{.page{padding:0;max-width:none}}";

static void write_stick_svg(FILE *f, const report_t *r)
{
    fputs("<svg viewBox=\"-112 -112 224 224\" xmlns=\"http://www.w3.org/2000/svg\">"
          "<rect x=\"-112\" y=\"-112\" width=\"224\" height=\"224\" fill=\"#f5f6fa\" stroke=\"#c8cad4\"/>"
          "<line x1=\"-112\" y1=\"0\" x2=\"112\" y2=\"0\" stroke=\"#dcdee6\"/>"
          "<line x1=\"0\" y1=\"-112\" x2=\"0\" y2=\"112\" stroke=\"#dcdee6\"/>"
          "<g transform=\"scale(1,-1)\">", f);

    /* coverage: every position the stick visited */
    if (r->visited) {
        float c = 1.0f / STICK_SCALE;
        fputs("<path fill=\"#a8dcb9\" d=\"", f);
        for (int j = 0; j < MAP_N; j++)
            for (int i = 0; i < MAP_N; i++)
                if (r->visited[j][i])
                    fprintf(f, "M%.1f %.1fh%.1fv%.1fh-%.1fz",
                            (i - MAP_R) * c - c / 2, (j - MAP_R) * c - c / 2, c, c, c);
        fputs("\"/>", f);
    }

    /* reference gate of a healthy stick */
    fputs("<polygon fill=\"none\" stroke=\"#4a6fd1\" stroke-width=\"1.3\" stroke-dasharray=\"4 3\" points=\"", f);
    for (int k = 0; k < 8; k++) {
        float a = k * (float)(M_PI / 4);
        fprintf(f, "%.1f,%.1f ", cosf(a) * sector_ref(k), sinf(a) * sector_ref(k));
    }
    fputs("\"/>", f);

    /* measured reach in each direction */
    fputs("<polygon fill=\"rgba(30,142,62,.12)\" stroke=\"#1e8e3e\" stroke-width=\"2\" points=\"", f);
    for (int k = 0; k < 8; k++)
        fprintf(f, "%d,%d ", r->sector_max[k] > 0 ? r->sector_x[k] : 0,
                             r->sector_max[k] > 0 ? r->sector_y[k] : 0);
    fputs("\"/>", f);
    for (int k = 0; k < 8; k++) {
        if (r->sector_max[k] <= 0) continue;
        float q = r->sector_max[k] / sector_ref(k);
        const char *col = q >= 0.90f ? "#1e8e3e" : q >= 0.75f ? "#d56e00" : "#c5221f";
        fprintf(f, "<circle cx=\"%d\" cy=\"%d\" r=\"3.5\" fill=\"%s\"/>", r->sector_x[k], r->sector_y[k], col);
    }
    fputs("</g><g paint-order=\"stroke\" stroke=\"#f5f6fa\" stroke-width=\"3\" stroke-linejoin=\"round\">"
          "<text x=\"0\" y=\"-100\" font-size=\"9\" text-anchor=\"middle\" fill=\"#777\">UP</text>"
          "<text x=\"0\" y=\"107\" font-size=\"9\" text-anchor=\"middle\" fill=\"#777\">DOWN</text>"
          "<text x=\"-108\" y=\"-4\" font-size=\"9\" fill=\"#777\">LEFT</text>"
          "<text x=\"108\" y=\"-4\" font-size=\"9\" text-anchor=\"end\" fill=\"#777\">RIGHT</text>"
          "</g></svg>", f);
}

void report_write_html(FILE *f, const report_t *r)
{
    const char *headline;
    int v = report_verdict(r, &headline);
    int ok = buttons_ok(r);
    int pct = range_pct(r->sector_max);
    bool moved = stick_fully_moved(r->sector_max);

    fprintf(f, "<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">"
               "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
               "<title>N64 Controller Test Report #%03d</title><style>%s</style></head>\n<body><div class=\"page\">\n",
               r->report_no, CSS);

    fprintf(f, "<header><div><h1>N64 Controller Test Report</h1>"
               "<div class=\"sub\">Tested on original Nintendo 64 hardware</div></div>"
               "<div class=\"meta\">Report #%03d<br>Date: %s<br>Port %d &middot; %s<br>Accessory: %s</div></header>\n",
               r->report_no, r->date[0] ? r->date : "____________", r->port, r->ctrl_type, r->pak);

    fprintf(f, "<div class=\"verdict %s\"><b>%s</b><span>Buttons %d/%d working",
            v < 0 ? "vi" : v == 0 ? "v0" : v == 1 ? "v1" : "v2", headline, ok, NUM_BTN);
    if (moved) fprintf(f, " &middot; Stick range %d%%", pct);
    if (r->have_ctr) fprintf(f, " &middot; Returns to center: %s", word_ctr(grade_ctr(r->ctr_worst)));
    fputs("</span></div>\n<div class=\"cols\">\n", f);

    /* ---- buttons ---- */
    fputs("<div class=\"col\"><h2>Buttons</h2><table><tr><th>Button</th><th>Result</th><th style=\"text-align:right\">Presses</th></tr>", f);
    for (int n = 0; n < NUM_BTN; n++) {
        int i = BTN_ORDER[n];
        fprintf(f, "<tr><td>%s</td><td class=\"%s\">%s</td><td class=\"n\">%d</td></tr>",
                BTN_NAMES[i], r->passed[i] ? "good" : "bad",
                r->passed[i] ? "WORKS" : "NOT DETECTED", r->count[i]);
    }
    fputs("</table>", f);

    /* ---- reach by direction ---- */
    fputs("<h2>Stick reach by direction</h2><table><tr><th>Direction</th>"
          "<th style=\"text-align:right\">Reached</th><th style=\"text-align:right\">Healthy</th>"
          "<th style=\"text-align:right\">%</th></tr>", f);
    static const int DIR_ORDER[8] = { 2, 6, 4, 0, 3, 1, 5, 7 };   /* U D L R UL UR DL DR */
    for (int n = 0; n < 8; n++) {
        int k = DIR_ORDER[n];
        if (r->sector_max[k] <= 0) {
            fprintf(f, "<tr><td>%s</td><td class=\"n na\">-</td><td class=\"n\">%.0f</td><td class=\"n na\">-</td></tr>",
                    DIR_NAMES[k], sector_ref(k));
            continue;
        }
        float q = r->sector_max[k] / sector_ref(k);
        int g = q >= 0.90f ? 0 : q >= 0.75f ? 1 : 2;
        fprintf(f, "<tr><td>%s</td><td class=\"n\">%.0f</td><td class=\"n\">%.0f</td><td class=\"n %s\">%.0f%%</td></tr>",
                DIR_NAMES[k], r->sector_max[k], sector_ref(k), gcls(g), q * 100.0f);
    }
    fputs("</table></div>\n", f);

    /* ---- stick ---- */
    fputs("<div class=\"col\"><h2>Analog stick</h2>", f);
    write_stick_svg(f, r);
    fputs("<div class=\"legend\"><span class=\"sw\" style=\"background:#1e8e3e\"></span>This controller"
          "<span class=\"sw\" style=\"background:#4a6fd1\"></span>New stick (reference)"
          "<span class=\"sw\" style=\"background:#a8dcb9;height:10px;vertical-align:-1px\"></span>Positions reached</div>", f);

    fputs("<table style=\"margin-top:10px\">", f);
    if (moved)
        fprintf(f, "<tr><td>Overall range</td><td class=\"n\">%d%%</td><td class=\"%s\">%s</td></tr>",
                pct, gcls(grade_range(pct)), word_range(grade_range(pct)));
    else
        fputs("<tr><td>Overall range</td><td class=\"n na\">-</td><td class=\"na\">not fully tested</td></tr>", f);
    fprintf(f, "<tr><td>X axis (left..right)</td><td class=\"n\">%+d .. %+d</td><td></td></tr>", r->minx, r->maxx);
    fprintf(f, "<tr><td>Y axis (down..up)</td><td class=\"n\">%+d .. %+d</td><td></td></tr>", r->miny, r->maxy);
    if (r->have_ctr)
        fprintf(f, "<tr><td>Return to center</td><td class=\"n\">off by %d</td><td class=\"%s\">%s</td></tr>",
                r->ctr_worst, gcls(grade_ctr(r->ctr_worst)), word_ctr(grade_ctr(r->ctr_worst)));
    else
        fputs("<tr><td>Return to center</td><td class=\"n na\">-</td><td class=\"na\">not measured</td></tr>", f);
    if (r->jitter >= 0)
        fprintf(f, "<tr><td>Resting jitter</td><td class=\"n\">%d</td><td class=\"%s\">%s</td></tr>",
                r->jitter, gcls(grade_jitter(r->jitter)), word_jitter(grade_jitter(r->jitter)));
    else
        fputs("<tr><td>Resting jitter</td><td class=\"n na\">-</td><td class=\"na\">not measured</td></tr>", f);
    fputs("</table>", f);

    fputs("<div class=\"explain\"><b>How to read this:</b> a new N64 stick reaches about 80 straight out "
          "and about 70,70 on diagonals. <b>Range</b> compares this stick to that. "
          "<b>Return to center</b> is how far from the middle the stick settles after being pushed "
          "and let go (0-3 is normal; a higher number means a loose stick that drifts). "
          "<b>Jitter</b> is how much the reading wobbles while the stick sits still.</div>", f);
    fputs("</div>\n</div>\n", f);

    fputs("<div class=\"notes\"><b>Item / listing #:</b><div class=\"line\"></div>"
          "<b>Notes:</b><div class=\"line\"></div><div class=\"line\"></div></div>\n", f);
    fputs("<footer>Generated on the console by Drift Test 64 (homebrew, libdragon). "
          "Results describe the controller's condition at the time of testing.</footer>\n"
          "</div></body></html>\n", f);
}
