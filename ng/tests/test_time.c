/* Unit tests for the GrADS time subsystem (ng/src/time/).
 * Reference rules probed against 2.2.1.oga.1; see NG_BASELINE.md. */
#include <stdio.h>
#include <string.h>

#include "ng_time.h"

static int checks = 0;
static int failures = 0;

#define CHECK(cond, ...) do { \
        checks++; \
        if (!(cond)) { \
            failures++; \
            printf("FAIL "); \
            printf(__VA_ARGS__); \
            printf("\n"); \
        } \
    } while (0)

static int same(ng_time_t t, long long yr, int mo, int dy, int hr, int mn) {
    return t.yr == yr && t.mo == mo && t.dy == dy && t.hr == hr &&
           t.mn == mn;
}

int main(void) {
    ng_time_t t;
    int count, k;
    ng_time_unit_t unit;
    char buf[32];

    /* Datetime parsing. */
    CHECK(ng_time_parse("00Z03JAN1987", &t) == 0 &&
          same(t, 1987, 1, 3, 0, 0), "full form");
    CHECK(ng_time_parse("03JAN1987", &t) == 0 &&
          same(t, 1987, 1, 3, 0, 0), "hour omitted");
    CHECK(ng_time_parse("0030Z02JAN1987", &t) == 0 &&
          same(t, 1987, 1, 2, 0, 30), "minutes form");
    CHECK(ng_time_parse("18z01jan2000", &t) == 0 &&
          same(t, 2000, 1, 1, 18, 0), "lowercase");
    CHECK(ng_time_parse("00Z03JAN87", &t) == 0 && t.yr == 1987,
          "2-digit year 87 -> 1987");
    CHECK(ng_time_parse("00Z01JAN00", &t) == 0 && t.yr == 2000,
          "2-digit year 00 -> 2000");
    CHECK(ng_time_parse("00Z01JAN49", &t) == 0 && t.yr == 2049,
          "pivot 49 -> 2049");
    CHECK(ng_time_parse("00Z01JAN50", &t) == 0 && t.yr == 1950,
          "pivot 50 -> 1950");
    CHECK(ng_time_parse("00Z29FEB1988", &t) == 0, "leap day exists");
    CHECK(ng_time_parse("00Z29FEB1987", &t) != 0, "no Feb 29 off-leap");
    CHECK(ng_time_parse("00Z31APR2000", &t) != 0, "Apr 31 rejected");
    CHECK(ng_time_parse("24Z02JAN1987", &t) != 0, "hour 24 rejected");
    CHECK(ng_time_parse("0060Z02JAN1987", &t) != 0, "minute 60 rejected");
    CHECK(ng_time_parse("garbage", &t) != 0, "garbage rejected");
    CHECK(ng_time_parse("00Z03FOO1987", &t) != 0, "bad month rejected");
    CHECK(ng_time_parse("00Z03JAN1987x", &t) != 0, "trailing junk rejected");
    CHECK(ng_time_parse("", &t) != 0, "empty rejected");
    CHECK(ng_time_parse(NULL, &t) != 0, "NULL rejected");

    /* Increment parsing. */
    CHECK(ng_incr_parse("1DY", &count, &unit) == 0 && count == 1 &&
          unit == NG_TUNIT_DY, "1DY");
    CHECK(ng_incr_parse("6HR", &count, &unit) == 0 && count == 6 &&
          unit == NG_TUNIT_HR, "6HR");
    CHECK(ng_incr_parse("30MN", &count, &unit) == 0 && count == 30 &&
          unit == NG_TUNIT_MN, "30MN");
    CHECK(ng_incr_parse("1MO", &count, &unit) == 0 && unit == NG_TUNIT_MO,
          "1MO");
    CHECK(ng_incr_parse("2YR", &count, &unit) == 0 && count == 2 &&
          unit == NG_TUNIT_YR, "2YR");
    CHECK(ng_incr_parse("1dy", &count, &unit) == 0, "lowercase unit");
    CHECK(ng_incr_parse("0DY", &count, &unit) != 0, "zero count rejected");
    CHECK(ng_incr_parse("-1DY", &count, &unit) != 0, "negative rejected");
    CHECK(ng_incr_parse("1XX", &count, &unit) != 0, "bad unit rejected");
    CHECK(ng_incr_parse("DY", &count, &unit) != 0, "bare unit rejected");

    /* Stepping: daily chain, month spill (start-anchored), leap year. */
    ng_time_parse("00Z31JAN1987", &t);
    CHECK(same(ng_time_step(t, 1, NG_TUNIT_DY, 1), 1987, 2, 1, 0, 0),
          "Jan31 + 1DY = Feb1");
    k = 1;
    CHECK(same(ng_time_step(t, 1, NG_TUNIT_MO, k), 1987, 3, 3, 0, 0),
          "31JAN + 1MO spills to Mar3");
    CHECK(same(ng_time_step(t, 1, NG_TUNIT_MO, 2), 1987, 3, 31, 0, 0),
          "31JAN + 2MO anchors Mar31");
    ng_time_parse("00Z29FEB1988", &t);
    CHECK(same(ng_time_step(t, 1, NG_TUNIT_YR, 1), 1989, 3, 1, 0, 0),
          "29FEB1988 + 1YR spills to Mar1");
    ng_time_parse("12Z01JAN2000", &t);
    CHECK(same(ng_time_step(t, 6, NG_TUNIT_HR, 1), 2000, 1, 1, 18, 0),
          "12Z + 6HR = 18Z");
    CHECK(same(ng_time_step(t, 6, NG_TUNIT_HR, 3), 2000, 1, 2, 6, 0),
          "hourly crosses midnight");
    CHECK(same(ng_time_step(t, 30, NG_TUNIT_MN, 2), 2000, 1, 1, 13, 0),
          "30MN steps");
    ng_time_parse("00Z15NOV2001", &t);
    CHECK(same(ng_time_step(t, 1, NG_TUNIT_MO, 3), 2002, 2, 15, 0, 0),
          "month stepping wraps the year");

    /* Ordering and absolute minutes are exact. */
    {
        ng_time_t a, b;
        ng_time_parse("00Z02JAN1987", &a);
        ng_time_parse("00Z03JAN1987", &b);
        CHECK(ng_time_cmp(a, b) < 0 && ng_time_cmp(b, a) > 0 &&
              ng_time_cmp(a, a) == 0, "ordering");
        CHECK(ng_time_absmin(b) - ng_time_absmin(a) == 1440,
              "one day is 1440 minutes");
    }

    /* Canonical rendering. */
    ng_time_parse("00Z03JAN1987", &t);
    ng_time_format(t, buf, sizeof(buf));
    CHECK(strcmp(buf, "00Z03JAN1987") == 0, "format round-trips (%s)", buf);
    ng_time_parse("18Z01JAN2000", &t);
    ng_time_format(t, buf, sizeof(buf));
    CHECK(strcmp(buf, "18Z01JAN2000") == 0, "hour renders (%s)", buf);

    /* Nearest step, ties up (daily axis from sample.ctl). */
    {
        ng_time_t start, tgt;
        ng_time_parse("00Z02JAN1987", &start);
        ng_time_parse("00Z03JAN1987", &tgt);
        CHECK(ng_time_nearest(start, 1, NG_TUNIT_DY, 2, tgt) == 1,
              "exact hit");
        ng_time_parse("12Z02JAN1987", &tgt);
        CHECK(ng_time_nearest(start, 1, NG_TUNIT_DY, 2, tgt) == 1,
              "midpoint tie goes up");
        ng_time_parse("06Z02JAN1987", &tgt);
        CHECK(ng_time_nearest(start, 1, NG_TUNIT_DY, 2, tgt) == 0,
              "early time stays low");
        ng_time_parse("00Z01JAN2000", &tgt);
        CHECK(ng_time_nearest(start, 1, NG_TUNIT_DY, 2, tgt) == 1,
              "far future clamps to last (core range-checks)");
        CHECK(ng_time_nearest(start, 1, NG_TUNIT_DY, 1, tgt) == 0,
              "single-step axis always 0");
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
