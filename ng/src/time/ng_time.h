#ifndef NG_TIME_H
#define NG_TIME_H

/*
 * ng/src/time/time.h
 * GrADS time subsystem (slice 1: TDEF axis + `set time`).
 *
 * Reference rules, probed against 2.2.1.oga.1 (see NG_BASELINE.md):
 * - Real Gregorian calendar: 29FEB1988 exists; no 365-day shortcut.
 * - Datetimes: [HH[MM]Z]DDMMMYYYY, case-insensitive; HH 00-23, MM 00-59;
 *   2-digit years pivot at 50 (00-49 -> 2000s, 50-99 -> 1900s).
 * - Increments: N(MN|HR|DY|MO|YR), N >= 1, case-insensitive. Zero,
 *   negative, and unknown units fail the open, like the reference.
 * - Month/year stepping is start-anchored: step k adds k increments to the
 *   start month and pins the start day, with day overflow spilling forward
 *   (31JAN1987 + 1MO = 03MAR1987; + 2MO = 31MAR1987; 29FEB1988 + 1YR =
 *   01MAR1989).
 * - `set time` snaps to the nearest step, ties up (12Z02JAN1987 on daily
 *   steps sticks at step 2).
 *
 * Proleptic Gregorian throughout; years are unbounded internally and print
 * at least 4 digits wide. No leap seconds, no time zones (GrADS times are
 * UTC by convention).
 */

#include <stddef.h>

typedef struct {
    long long yr;
    int mo;   /* 1-12 */
    int dy;   /* 1-31 */
    int hr;   /* 0-23 */
    int mn;   /* 0-59 */
} ng_time_t;

typedef enum {
    NG_TUNIT_MN = 0,
    NG_TUNIT_HR,
    NG_TUNIT_DY,
    NG_TUNIT_MO,
    NG_TUNIT_YR
} ng_time_unit_t;

/* Parse a GrADS datetime. Returns 0 with *out set, -1 on any malformed or
 * non-calendar input (bad month name, day out of range, Feb 29 on a common
 * year, hour/minute out of range). */
int ng_time_parse(const char *s, ng_time_t *out);

/* Parse a GrADS increment. Returns 0 with *count (>= 1) and *unit set,
 * -1 otherwise. */
int ng_incr_parse(const char *s, int *count, ng_time_unit_t *unit);

/* Step k (0-based, k >= 0) from start by count*unit. Start-anchored with
 * forward spill, per above. */
ng_time_t ng_time_step(ng_time_t start, int count, ng_time_unit_t unit,
                       int k);

/* Total order over the proleptic Gregorian timeline. */
int ng_time_cmp(ng_time_t a, ng_time_t b);

/* Absolute minutes since 1970-01-01 00:00 (exact for every valid time). */
long long ng_time_absmin(ng_time_t t);

/* Canonical GrADS rendering "00Z03JAN1987" (buf of 16+ bytes). */
void ng_time_format(ng_time_t t, char *buf, size_t len);

/* Nearest step index (0-based) for target on an nt-step axis starting at
 * start, ties to the higher step. No validation: callers check the
 * half-step window for the strict out-of-range error. */
int ng_time_nearest(ng_time_t start, int count, ng_time_unit_t unit,
                    int nt, ng_time_t target);

#endif /* NG_TIME_H */
