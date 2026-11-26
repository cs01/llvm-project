/*
 * Copyright © 2014 Kosma Moczek <kosma@cloudyourcar.com>
 * This program is free software. It comes without any warranty, to the extent
 * permitted by applicable law. You can redistribute it and/or modify it under
 * the terms of the Do What The Fuck You Want To Public License, Version 2, as
 * published by Sam Hocevar. See the COPYING file for more details.
 *
 * Safe C port: the format-string scanner is replaced by one function per field
 * type, reading the sentence by index. Each parser reads the same fields in the
 * same order as before.
 */

#include "minmea.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define countof(a) (sizeof(a) / sizeof(*(a)))

static int hex2int(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

uint8_t minmea_checksum(const char *sentence)
{
    // Support senteces with or without the starting dollar sign.
    if (*sentence == '$')
        sentence++;

    uint8_t checksum = 0x00;

    // The optional checksum is an XOR of all bytes between "$" and "*".
    while (*sentence && *sentence != '*')
        checksum ^= *sentence++;

    return checksum;
}

bool minmea_check(const char *sentence, bool strict)
{
    uint8_t checksum = 0x00;

    // A valid sentence starts with "$". (Tested before stepping: a string
    // pointer does not go past its terminator.)
    if (*sentence != '$')
        return false;
    sentence++;

    // The optional checksum is an XOR of all bytes between "$" and "*".
    while (*sentence && *sentence != '*' && isprint((unsigned char) *sentence))
        checksum ^= *sentence++;

    // If checksum is present...
    if (*sentence == '*') {
        // Extract checksum.
        sentence++;
        int upper = hex2int(*sentence);
        if (upper == -1)
            return false;
        sentence++;
        int lower = hex2int(*sentence);
        if (lower == -1)
            return false;
        sentence++;
        int expected = upper << 4 | lower;

        // Check for checksum mismatch.
        if (checksum != expected)
            return false;
    } else if (strict) {
        // Discard non-checksummed frames in strict mode.
        return false;
    }

    // The only stuff allowed at this point is a newline.
    while (*sentence == '\r' || *sentence == '\n') {
        sentence++;
    }

    if (*sentence) {
        return false;
    }

    return true;
}

/* ---- Reading fields -------------------------------------------------- */

/* A sentence and where we are in it. 'field' is where the current field
   starts, or 'none' when the input ran out; 'at' is where the scan is. */
struct _View scan {
    const char *__counted_by(len) p;
    size_t len;
    size_t at, field;
    bool none, optional, ok;
};

/* The byte at i, or 0 past the end, as the C string would have. */
static char peek(const struct scan *s, size_t i)
{
    return i < s->len ? s->p[i] : '\0';
}

static struct scan scan_begin(const char *sentence)
{
    size_t n = strlen(sentence);
    struct scan s = {__null_terminated_to_indexable(sentence), n, 0, 0, false, false, true};
    return s;
}

/* Before each field: out of input is an error unless the rest are optional. */
static bool scan_want(struct scan *s)
{
    if (s->ok && s->none && !s->optional)
        s->ok = false;
    return s->ok;
}

static void scan_next(struct scan *s)
{
    while (minmea_isfield(peek(s, s->at)))
        s->at++;
    if (peek(s, s->at) == ',') {
        s->at++;
        s->field = s->at;
        s->none = false;
    } else {
        s->none = true;
    }
}

/* ';' in the old formats: the fields after this may be missing. */
static void scan_optional(struct scan *s)
{
    s->optional = true;
}

static bool has(const struct scan *s)
{
    return !s->none;
}

static char at(const struct scan *s, size_t k)
{
    return peek(s, s->field + k);
}

static void scan_c(struct scan *s, char *out)
{
    if (!scan_want(s))
        return;
    char value = '\0';
    if (has(s) && minmea_isfield(at(s, 0)))
        value = at(s, 0);
    *out = value;
    scan_next(s);
}

static void scan_d(struct scan *s, int *out)
{
    if (!scan_want(s))
        return;
    int value = 0;
    if (has(s) && minmea_isfield(at(s, 0))) {
        switch (at(s, 0)) {
            case 'N':
            case 'E':
                value = 1;
                break;
            case 'S':
            case 'W':
                value = -1;
                break;
            default:
                s->ok = false;
                return;
        }
    }
    *out = value;
    scan_next(s);
}

static void scan_f(struct scan *s, struct minmea_float *out)
{
    if (!scan_want(s))
        return;
    int sign = 0;
    int_least32_t value = -1;
    int_least32_t scale = 0;

    if (has(s)) {
        for (size_t k = 0; minmea_isfield(at(s, k)); k++) {
            char c = at(s, k);
            if (c == '+' && !sign && value == -1) {
                sign = 1;
            } else if (c == '-' && !sign && value == -1) {
                sign = -1;
            } else if (isdigit((unsigned char) c)) {
                int digit = c - '0';
                if (value == -1)
                    value = 0;
                // (The original overflowed the scale on many decimals, as in
                // 0.0000000001; those digits are dropped like any extra precision.)
                if (value > (INT_LEAST32_MAX-digit) / 10 || scale > INT_LEAST32_MAX / 10) {
                    /* we ran out of bits, what do we do? */
                    if (scale) {
                        /* truncate extra precision */
                        break;
                    } else {
                        /* integer overflow. bail out. */
                        s->ok = false;
                        return;
                    }
                }
                value = (10 * value) + digit;
                if (scale)
                    scale *= 10;
            } else if (c == '.' && scale == 0) {
                scale = 1;
            } else if (c == ' ') {
                /* Allow spaces at the start of the field. Not NMEA
                 * conformant, but some modules do this. */
                if (sign != 0 || value != -1 || scale != 0) {
                    s->ok = false;
                    return;
                }
            } else {
                s->ok = false;
                return;
            }
        }
    }

    if ((sign || scale) && value == -1) {
        s->ok = false;
        return;
    }

    if (value == -1) {
        /* No digits were scanned. */
        value = 0;
        scale = 0;
    } else if (scale == 0) {
        /* No decimal point. */
        scale = 1;
    }
    if (sign)
        value *= sign;

    out->value = value;
    out->scale = scale;
    scan_next(s);
}

/* What strtol(field, &end, 10) did: spaces, a sign, digits; past the range
   it stops at LONG_MAX or LONG_MIN. *end is where the number ended, or the
   field's start if there were no digits. */
static long strtol_at(const struct scan *s, size_t *end)
{
    size_t k = 0;
    while (isspace((unsigned char) at(s, k)))
        k++;
    bool minus = false;
    if (at(s, k) == '+' || at(s, k) == '-')
        minus = at(s, k++) == '-';
    size_t digits = k;
    unsigned long v = 0;
    bool big = false;
    for (; isdigit((unsigned char) at(s, k)); k++) {
        unsigned long d = (unsigned long)(at(s, k) - '0');
        if (v > (ULONG_MAX - d) / 10)
            big = true;
        else
            v = v * 10 + d;
    }
    if (k == digits) {
        *end = 0;
        return 0;
    }
    *end = k;
    unsigned long limit = minus ? (unsigned long)LONG_MAX + 1 : (unsigned long)LONG_MAX;
    if (big || v > limit)
        return minus ? LONG_MIN : LONG_MAX;
    return minus ? (long)(0 - v) : (long)v;
}

static void scan_i(struct scan *s, int *out)
{
    if (!scan_want(s))
        return;
    int value = 0;
    if (has(s)) {
        size_t end = 0;
        value = (int)strtol_at(s, &end);
        if (minmea_isfield(at(s, end))) {
            s->ok = false;
            return;
        }
    }
    *out = value;
    scan_next(s);
}

static void scan_t(struct scan *s, union minmea_type *out)
{
    if (!scan_want(s))
        return;
    // This field is always mandatory.
    if (!has(s) || at(s, 0) != '$') {
        s->ok = false;
        return;
    }
    for (size_t f = 0; f < 5; f++)
        if (!minmea_isfield(at(s, 1 + f))) {
            s->ok = false;
            return;
        }
    for (size_t f = 0; f < 5; f++)
        out->buf[f] = at(s, 1 + f);
    out->null_terminator = '\0';
    scan_next(s);
}

/* Two digits at k as a number. */
static int two(const struct scan *s, size_t k)
{
    return (at(s, k) - '0') * 10 + (at(s, k + 1) - '0');
}

static void scan_D(struct scan *s, struct minmea_date *date)
{
    if (!scan_want(s))
        return;
    int d = -1, m = -1, y = -1;

    if (has(s) && minmea_isfield(at(s, 0))) {
        // Always six digits.
        for (size_t f = 0; f < 6; f++)
            if (!isdigit((unsigned char) at(s, f))) {
                s->ok = false;
                return;
            }
        d = two(s, 0);
        m = two(s, 2);
        y = two(s, 4);
    }

    date->day = d;
    date->month = m;
    date->year = y;
    scan_next(s);
}

static void scan_T(struct scan *s, struct minmea_time *time_)
{
    if (!scan_want(s))
        return;
    int h = -1, i = -1, sec = -1, u = -1;

    if (has(s) && minmea_isfield(at(s, 0))) {
        // Minimum required: integer time.
        for (size_t f = 0; f < 6; f++)
            if (!isdigit((unsigned char) at(s, f))) {
                s->ok = false;
                return;
            }
        h = two(s, 0);
        i = two(s, 2);
        sec = two(s, 4);

        // Extra: fractional time. Saved as microseconds.
        if (at(s, 6) == '.') {
            uint32_t value = 0;
            uint32_t scale = 1000000LU;
            for (size_t k = 7; isdigit((unsigned char) at(s, k)) && scale > 1; k++) {
                value = (value * 10) + (uint32_t)(at(s, k) - '0');
                scale /= 10;
            }
            u = (int)(value * scale);
        } else {
            u = 0;
        }
    }

    time_->hours = h;
    time_->minutes = i;
    time_->seconds = sec;
    time_->microseconds = u;
    scan_next(s);
}

/* '_' in the old formats. */
static void scan_skip(struct scan *s)
{
    if (scan_want(s))
        scan_next(s);
}

/* ---- Sentences ------------------------------------------------------- */

static bool type_is(const union minmea_type *type, const char *__counted_by(3) id)
{
    return !memcmp(type->sentence_id, id, sizeof(type->sentence_id));
}

bool minmea_talker_id(char talker[3], const char *sentence)
{
    union minmea_type type;
    struct scan s = scan_begin(sentence);
    scan_t(&s, &type);
    if (!s.ok)
        return false;

    talker[0] = type.talker_id[0];
    talker[1] = type.talker_id[1];
    talker[2] = '\0';

    return true;
}

struct sentence_id_map_entry {
    const char *_Static str;
    enum minmea_sentence_id id;
};

static const struct sentence_id_map_entry sentence_id_map[] = {
    { "INVALID", MINMEA_INVALID },
    { "GBS", MINMEA_SENTENCE_GBS },
    { "GGA", MINMEA_SENTENCE_GGA },
    { "GLL", MINMEA_SENTENCE_GLL },
    { "GSA", MINMEA_SENTENCE_GSA },
    { "GST", MINMEA_SENTENCE_GST },
    { "GSV", MINMEA_SENTENCE_GSV },
    { "RMC", MINMEA_SENTENCE_RMC },
    { "VTG", MINMEA_SENTENCE_VTG },
    { "ZDA", MINMEA_SENTENCE_ZDA },
};

const char *_Static minmea_sentence(enum minmea_sentence_id id) {
    if (id < 0 || id >= (int)countof(sentence_id_map)) {
        return "UNKNOWN";
    }
    return sentence_id_map[id].str;
}

enum minmea_sentence_id minmea_sentence_id(const char *sentence, bool strict)
{
    if (!minmea_check(sentence, strict))
        return MINMEA_INVALID;

    union minmea_type type;
    struct scan s = scan_begin(sentence);
    scan_t(&s, &type);
    if (!s.ok)
        return MINMEA_INVALID;

    for (unsigned int i = 0; i < countof(sentence_id_map); i++) {
        size_t n = strlen(sentence_id_map[i].str);
        if (n >= 3 && !memcmp(type.sentence_id, __null_terminated_to_indexable(sentence_id_map[i].str), sizeof(type.sentence_id))) {
            return sentence_id_map[i].id;
        }
    }

    return MINMEA_UNKNOWN;
}

bool minmea_parse_gbs(struct minmea_sentence_gbs *frame, const char *sentence)
{
    // $GNGBS,170556.00,3.0,2.9,8.3,,,,*5C
    struct scan s = scan_begin(sentence);
    scan_t(&s, &frame->type);
    scan_T(&s, &frame->time);
    scan_f(&s, &frame->err_latitude);
    scan_f(&s, &frame->err_longitude);
    scan_f(&s, &frame->err_altitude);
    scan_i(&s, &frame->svid);
    scan_f(&s, &frame->prob);
    scan_f(&s, &frame->bias);
    scan_f(&s, &frame->stddev);
    return s.ok && type_is(&frame->type, "GBS");
}

bool minmea_parse_rmc(struct minmea_sentence_rmc *frame, const char *sentence)
{
    // $GPRMC,081836,A,3751.65,S,14507.36,E,000.0,360.0,130998,011.3,E*62
    char validity = 0;
    int latitude_direction = 0;
    int longitude_direction = 0;
    int variation_direction = 0;
    struct scan s = scan_begin(sentence);
    scan_t(&s, &frame->type);
    scan_T(&s, &frame->time);
    scan_c(&s, &validity);
    scan_f(&s, &frame->latitude);
    scan_d(&s, &latitude_direction);
    scan_f(&s, &frame->longitude);
    scan_d(&s, &longitude_direction);
    scan_f(&s, &frame->speed);
    scan_f(&s, &frame->course);
    scan_D(&s, &frame->date);
    scan_f(&s, &frame->variation);
    scan_d(&s, &variation_direction);
    if (!s.ok || !type_is(&frame->type, "RMC"))
        return false;

    frame->valid = (validity == 'A');
    frame->latitude.value *= latitude_direction;
    frame->longitude.value *= longitude_direction;
    frame->variation.value *= variation_direction;

    return true;
}

bool minmea_parse_gga(struct minmea_sentence_gga *frame, const char *sentence)
{
    // $GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47
    int latitude_direction = 0;
    int longitude_direction = 0;
    struct scan s = scan_begin(sentence);
    scan_t(&s, &frame->type);
    scan_T(&s, &frame->time);
    scan_f(&s, &frame->latitude);
    scan_d(&s, &latitude_direction);
    scan_f(&s, &frame->longitude);
    scan_d(&s, &longitude_direction);
    scan_i(&s, &frame->fix_quality);
    scan_i(&s, &frame->satellites_tracked);
    scan_f(&s, &frame->hdop);
    scan_f(&s, &frame->altitude);
    scan_c(&s, &frame->altitude_units);
    scan_f(&s, &frame->height);
    scan_c(&s, &frame->height_units);
    scan_f(&s, &frame->dgps_age);
    scan_skip(&s);
    if (!s.ok || !type_is(&frame->type, "GGA"))
        return false;

    frame->latitude.value *= latitude_direction;
    frame->longitude.value *= longitude_direction;

    return true;
}

bool minmea_parse_gsa(struct minmea_sentence_gsa *frame, const char *sentence)
{
    // $GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1*39
    struct scan s = scan_begin(sentence);
    scan_t(&s, &frame->type);
    scan_c(&s, &frame->mode);
    scan_i(&s, &frame->fix_type);
    for (size_t k = 0; k < 12; k++)
        scan_i(&s, &frame->sats[k]);
    scan_f(&s, &frame->pdop);
    scan_f(&s, &frame->hdop);
    scan_f(&s, &frame->vdop);
    return s.ok && type_is(&frame->type, "GSA");
}

bool minmea_parse_gll(struct minmea_sentence_gll *frame, const char *sentence)
{
    // $GPGLL,3723.2475,N,12158.3416,W,161229.487,A,A*41$;
    int latitude_direction = 0;
    int longitude_direction = 0;
    struct scan s = scan_begin(sentence);
    scan_t(&s, &frame->type);
    scan_f(&s, &frame->latitude);
    scan_d(&s, &latitude_direction);
    scan_f(&s, &frame->longitude);
    scan_d(&s, &longitude_direction);
    scan_T(&s, &frame->time);
    scan_c(&s, &frame->status);
    scan_optional(&s);
    scan_c(&s, &frame->mode);
    if (!s.ok || !type_is(&frame->type, "GLL"))
        return false;

    frame->latitude.value *= latitude_direction;
    frame->longitude.value *= longitude_direction;

    return true;
}

bool minmea_parse_gst(struct minmea_sentence_gst *frame, const char *sentence)
{
    // $GPGST,024603.00,3.2,6.6,4.7,47.3,5.8,5.6,22.0*58
    struct scan s = scan_begin(sentence);
    scan_t(&s, &frame->type);
    scan_T(&s, &frame->time);
    scan_f(&s, &frame->rms_deviation);
    scan_f(&s, &frame->semi_major_deviation);
    scan_f(&s, &frame->semi_minor_deviation);
    scan_f(&s, &frame->semi_major_orientation);
    scan_f(&s, &frame->latitude_error_deviation);
    scan_f(&s, &frame->longitude_error_deviation);
    scan_f(&s, &frame->altitude_error_deviation);
    return s.ok && type_is(&frame->type, "GST");
}

bool minmea_parse_gsv(struct minmea_sentence_gsv *frame, const char *sentence)
{
    // $GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00*74
    // $GPGSV,4,4,13*7B
    struct scan s = scan_begin(sentence);
    scan_t(&s, &frame->type);
    scan_i(&s, &frame->total_msgs);
    scan_i(&s, &frame->msg_nr);
    scan_i(&s, &frame->total_sats);
    scan_optional(&s);
    for (size_t k = 0; k < 4; k++) {
        struct minmea_sat_info *sat = &frame->sats[k];
        scan_i(&s, &sat->nr);
        scan_i(&s, &sat->elevation);
        scan_i(&s, &sat->azimuth);
        scan_f(&s, &sat->snr);
    }
    return s.ok && type_is(&frame->type, "GSV");
}

bool minmea_parse_vtg(struct minmea_sentence_vtg *frame, const char *sentence)
{
    // $GPVTG,054.7,T,034.4,M,005.5,N,010.2,K*48
    // $GPVTG,188.36,T,,M,0.820,N,1.519,K,A*3F
    char c_true = 0, c_magnetic = 0, c_knots = 0, c_kph = 0, c_faa_mode = 0;
    struct scan s = scan_begin(sentence);
    scan_t(&s, &frame->type);
    scan_optional(&s);
    scan_f(&s, &frame->true_track_degrees);
    scan_c(&s, &c_true);
    scan_f(&s, &frame->magnetic_track_degrees);
    scan_c(&s, &c_magnetic);
    scan_f(&s, &frame->speed_knots);
    scan_c(&s, &c_knots);
    scan_f(&s, &frame->speed_kph);
    scan_c(&s, &c_kph);
    scan_c(&s, &c_faa_mode);
    if (!s.ok || !type_is(&frame->type, "VTG"))
        return false;
    // values are only valid with the accompanying characters
    if (c_true != 'T')
        frame->true_track_degrees.scale = 0;
    if (c_magnetic != 'M')
        frame->magnetic_track_degrees.scale = 0;
    if (c_knots != 'N')
        frame->speed_knots.scale = 0;
    if (c_kph != 'K')
        frame->speed_kph.scale = 0;
    frame->faa_mode = (enum minmea_faa_mode)c_faa_mode;

    return true;
}

bool minmea_parse_zda(struct minmea_sentence_zda *frame, const char *sentence)
{
    // $GPZDA,201530.00,04,07,2002,00,00*60
    struct scan s = scan_begin(sentence);
    scan_t(&s, &frame->type);
    scan_T(&s, &frame->time);
    scan_i(&s, &frame->date.day);
    scan_i(&s, &frame->date.month);
    scan_i(&s, &frame->date.year);
    scan_i(&s, &frame->hour_offset);
    scan_i(&s, &frame->minute_offset);
    if (!s.ok || !type_is(&frame->type, "ZDA"))
        return false;

    // check offsets (not with abs(): abs(INT_MIN) is undefined, and the
    // original accepted or refused 2147483648 depending on the compiler)
    if (frame->hour_offset > 13 || frame->hour_offset < -13 ||
        frame->minute_offset > 59 ||
        frame->minute_offset < 0)
        return false;

    return true;
}

int minmea_getdatetime(struct tm *tm, const struct minmea_date *date, const struct minmea_time *time_)
{
    if (date->year == -1 || time_->hours == -1)
        return -1;

    *tm = (struct tm){0};
    if (date->year < 80) {
        tm->tm_year = 2000 + date->year - 1900; // 2000-2079
    } else if (date->year >= 1900) {
        tm->tm_year = date->year - 1900;        // 4 digit year, use directly
    } else {
        tm->tm_year = date->year;               // 1980-1999
    }
    tm->tm_mon = date->month - 1;
    tm->tm_mday = date->day;
    tm->tm_hour = time_->hours;
    tm->tm_min = time_->minutes;
    tm->tm_sec = time_->seconds;

    return 0;
}

int minmea_gettime(struct timespec *ts, const struct minmea_date *date, const struct minmea_time *time_)
{
    struct tm tm;
    if (minmea_getdatetime(&tm, date, time_))
        return -1;

    time_t timestamp = timegm(&tm); /* See README.md if your system lacks timegm(). */
    if (timestamp != (time_t)-1) {
        ts->tv_sec = timestamp;
        ts->tv_nsec = time_->microseconds * 1000;
        return 0;
    } else {
        return -1;
    }
}
