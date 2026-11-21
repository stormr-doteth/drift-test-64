/* Test results + printable HTML report. Plain C (no libdragon), so it can
 * also be compiled on a PC to preview a sample report. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum { B_A, B_B, B_Z, B_START, B_DU, B_DD, B_DL, B_DR,
       B_L, B_R, B_CU, B_CD, B_CL, B_CR, NUM_BTN };
extern const char *const BTN_NAMES[NUM_BTN];

/* Reference "healthy" reach (magnitude) per direction. New OEM sticks
 * reach about 80-85 on cardinals and about 70,70 on diagonals. */
#define REF_CARD 80.0f
#define REF_DIAG 95.0f

/* stick coverage map */
#define MAP_R 56
#define MAP_N (MAP_R * 2 + 1)
#define STICK_SCALE ((float)MAP_R / 100.0f)

typedef struct {
    int         report_no;
    char        date[32];        /* "" if no clock */
    int         port;            /* 1-4 */
    const char *ctrl_type;
    const char *pak;

    bool        passed[NUM_BTN];
    int         count[NUM_BTN];

    float       sector_max[8];   /* 0 = right, counter-clockwise */
    int         sector_x[8], sector_y[8];
    int         minx, maxx, miny, maxy;

    bool        have_ctr;
    int         ctr_x, ctr_y, ctr_worst;
    int         jitter;          /* -1 = not measured */

    const uint8_t (*visited)[MAP_N];   /* may be NULL */
} report_t;

float       sector_ref(int k);
int         range_pct(const float sector_max[8]);
int         dirs_reached(const float sector_max[8]);
bool        stick_fully_moved(const float sector_max[8]);
int         buttons_ok(const report_t *r);

/* grades: 0 = good, 1 = worn/loose/noisy, 2 = bad */
int         grade_range(int pct);
int         grade_ctr(int worst);
int         grade_jitter(int j);
const char *word_range(int g);
const char *word_ctr(int g);
const char *word_jitter(int g);

/* -1 = incomplete, 0 = fully working, 1 = stick wear, 2 = needs repair */
int         report_verdict(const report_t *r, const char **headline);

void        report_write_html(FILE *f, const report_t *r);
