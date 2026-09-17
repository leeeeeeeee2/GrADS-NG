/*
 * ng/src/time/ng_time.c
 * GrADS time subsystem (see time.h for the reference-derived rules).
 *
 * Civil-date math uses Howard Hinnant's days_from_civil / civil_from_days
 * (public domain), which are exact over the proleptic Gregorian calendar.
 */

#include "ng_time.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int is_leap(long long y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int month_len(long long y, int m) {
    static const int days[12] = {31, 28, 31, 30, 31, 30,
                                 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) return 0;
    if (m == 2 && is_leap(y)) return 29;
    return days[m - 1];
}

/* days since 1970-01-01 (Hinnant). */
static long long days_from_civil(long long y, int m, int d) {
    long long era;
    long long yoe;
    long long doy;
    long long doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (long long)(y - era * 400);
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static ng_time_t civil_from_days(long long z) {
    ng_time_t t;
    long long era, doe, yoe, doy, mp;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    t.yr = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    t.dy = (int)(doy - (153 * mp + 2) / 5 + 1);
    t.mo = (int)(mp + (mp < 10 ? 3 : -9));
    t.yr += (t.mo <= 2);
    t.hr = 0;
    t.mn = 0;
    return t;
}

long long ng_time_absmin(ng_time_t t) {
    return days_from_civil(t.yr, t.mo, t.dy) * 1440LL +
           (long long)t.hr * 60 + t.mn;
}

static ng_time_t time_from_absmin(long long mins) {
    ng_time_t t;
    long long days = mins / 1440;
    long long rem = mins % 1440;
    if (rem < 0) {
        rem += 1440;
        days--;
    }
    t = civil_from_days(days);
    t.hr = (int)(rem / 60);
    t.mn = (int)(rem % 60);
    return t;
}

int ng_time_cmp(ng_time_t a, ng_time_t b) {
    long long x = ng_time_absmin(a), y = ng_time_absmin(b);
    return (x > y) - (x < y);
}

static int month_name(const char *s, char *canon) {
    static const char *names[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    char up[4];
    int i;
    for (i = 0; i < 3; i++) {
        char c = s[i];
        if (c >= 'a' && c <= 'z') c += 'A' - 'a';
        up[i] = c;
    }
    up[3] = '\0';
    for (i = 0; i < 12; i++) {
        if (memcmp(up, names[i], 3) == 0) {
            if (canon) memcpy(canon, names[i], 4);
            return i + 1;
        }
    }
    return 0;
}

int ng_time_parse(const char *s, ng_time_t *out) {
    ng_time_t t;
    const char *p;
    long yr;
    int day, mo;

    if (!s || !out) return -1;
    memset(&t, 0, sizeof(t));
    p = s;

    /* Optional HH[MM]Z prefix (case-insensitive Z). Two leading digits
     * are an hour form only with a Z right after (HHZ) or two more digits
     * plus Z (HHMMZ); otherwise they start the day ("03JAN1987"). */
    if (isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1])) {
        if (p[2] == 'Z' || p[2] == 'z') {
            int hh = (p[0] - '0') * 10 + (p[1] - '0');
            if (hh > 23) return -1;
            t.hr = hh;
            p += 3;
        } else if (isdigit((unsigned char)p[2]) &&
                   isdigit((unsigned char)p[3]) &&
                   (p[4] == 'Z' || p[4] == 'z')) {
            int hh = (p[0] - '0') * 10 + (p[1] - '0');
            int mm = (p[2] - '0') * 10 + (p[3] - '0');
            if (hh > 23 || mm > 59) return -1;
            t.hr = hh;
            t.mn = mm;
            p += 5;
        }
    }

    /* Day: 1-2 digits. */
    if (!isdigit((unsigned char)*p)) return -1;
    day = *p - '0';
    p++;
    if (isdigit((unsigned char)*p)) {
        day = day * 10 + (*p - '0');
        p++;
    }
    if (day < 1 || day > 31) return -1;
    t.dy = day;

    /* Month name. */
    mo = month_name(p, NULL);
    if (!mo) return -1;
    t.mo = mo;
    p += 3;

    /* Year: 2 or 4 digits (fixed pivot 50, per reference probes). */
    if (!isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1]))
        return -1;
    yr = (p[0] - '0') * 10 + (p[1] - '0');
    p += 2;
    if (isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1])) {
        yr = yr * 100 + (p[0] - '0') * 10 + (p[1] - '0');
        p += 2;
    } else {
        yr += (yr < 50) ? 2000 : 1900;
    }
    if (*p != '\0') return -1;
    t.yr = yr;

    /* Real calendar date (catches Feb 29 on common years, Apr 31, ...). */
    if (t.dy > month_len(t.yr, t.mo)) return -1;
    *out = t;
    return 0;
}

int ng_incr_parse(const char *s, int *count, ng_time_unit_t *unit) {
    const char *p;
    long n = 0;
    int digits = 0;

    if (!s || !count || !unit) return -1;
    p = s;
    while (isdigit((unsigned char)*p)) {
        n = n * 10 + (*p - '0');
        if (n > 2000000000L) return -1;
        digits++;
        p++;
    }
    if (digits == 0 || n < 1) return -1;
    if ((p[0] == 'M' || p[0] == 'm') && (p[1] == 'N' || p[1] == 'n') &&
        p[2] == '\0')
        *unit = NG_TUNIT_MN;
    else if ((p[0] == 'H' || p[0] == 'h') && (p[1] == 'R' || p[1] == 'r') &&
             p[2] == '\0')
        *unit = NG_TUNIT_HR;
    else if ((p[0] == 'D' || p[0] == 'd') && (p[1] == 'Y' || p[1] == 'y') &&
             p[2] == '\0')
        *unit = NG_TUNIT_DY;
    else if ((p[0] == 'M' || p[0] == 'm') && (p[1] == 'O' || p[1] == 'o') &&
             p[2] == '\0')
        *unit = NG_TUNIT_MO;
    else if ((p[0] == 'Y' || p[0] == 'y') && (p[1] == 'R' || p[1] == 'r') &&
             p[2] == '\0')
        *unit = NG_TUNIT_YR;
    else
        return -1;
    *count = (int)n;
    return 0;
}

ng_time_t ng_time_step(ng_time_t start, int count, ng_time_unit_t unit,
                       int k) {
    ng_time_t t = start;
    if (k <= 0) return start;
    switch (unit) {
        case NG_TUNIT_MN:
        case NG_TUNIT_HR:
        case NG_TUNIT_DY: {
            long long per = (long long)count *
                            (unit == NG_TUNIT_MN ? 1 :
                             unit == NG_TUNIT_HR ? 60 : 1440);
            return time_from_absmin(ng_time_absmin(start) + per * k);
        }
        case NG_TUNIT_MO:
        case NG_TUNIT_YR: {
            long long months = (long long)start.mo - 1 +
                               (long long)k * count *
                               (unit == NG_TUNIT_MO ? 1 : 12);
            long long y = start.yr + months / 12;
            int m = (int)(months % 12) + 1;
            int d = start.dy, dim;
            t.yr = y;
            t.mo = m;
            /* Day overflow spills forward (31JAN + 1MO = 03MAR). */
            for (;;) {
                dim = month_len(t.yr, t.mo);
                if (dim <= 0 || d <= dim) break;
                d -= dim;
                t.mo++;
                if (t.mo > 12) {
                    t.mo = 1;
                    t.yr++;
                }
            }
            t.dy = d;
            return t;
        }
    }
    return t;
}

void ng_time_format(ng_time_t t, char *buf, size_t len) {
    static const char *names[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    const char *mo = (t.mo >= 1 && t.mo <= 12) ? names[t.mo - 1] : "???";
    if (!buf || len == 0) return;
    snprintf(buf, len, "%02dZ%02d%s%04lld", t.hr, t.dy, mo, t.yr);
}

int ng_time_nearest(ng_time_t start, int count, ng_time_unit_t unit,
                    int nt, ng_time_t target) {
    long long tgt = ng_time_absmin(target);
    int lo = 0, hi, best = 0;
    long long bestd;

    if (nt <= 1) return 0;
    hi = nt - 1;
    /* Monotone steps: binary search the last step <= target. */
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        long long m = ng_time_absmin(ng_time_step(start, count, unit, mid));
        if (m <= tgt) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    /* Nearest of best/best+1, ties to the higher step. */
    bestd = tgt - ng_time_absmin(ng_time_step(start, count, unit, best));
    if (best + 1 < nt) {
        long long up =
            ng_time_absmin(ng_time_step(start, count, unit, best + 1)) - tgt;
        if (up <= bestd) return best + 1;
    }
    return best;
}
