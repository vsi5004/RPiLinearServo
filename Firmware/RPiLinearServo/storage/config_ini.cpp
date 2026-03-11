#include "config_ini.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>


static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static size_t trim_end(const char *start, size_t len) {
    while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t'
                       || start[len - 1] == '\r' || start[len - 1] == '\n'))
        len--;
    return len;
}

static bool match(const char *a, size_t alen, const char *b) {
    size_t blen = strlen(b);
    if (alen != blen) return false;
    for (size_t i = 0; i < alen; i++) {
        char ca = a[i]; if (ca >= 'A' && ca <= 'Z') ca += 32;
        char cb = b[i]; if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return false;
    }
    return true;
}

static bool parse_bool(const char *val, size_t len, bool &out) {
    if (match(val, len, "true") || match(val, len, "1")
        || match(val, len, "yes") || match(val, len, "on")) {
        out = true; return true;
    }
    if (match(val, len, "false") || match(val, len, "0")
        || match(val, len, "no") || match(val, len, "off")) {
        out = false; return true;
    }
    return false;
}

static bool parse_float(const char *val, size_t len, float &out) {
    char buf[32];
    if (len >= sizeof(buf)) return false;
    memcpy(buf, val, len); buf[len] = '\0';
    char *end;
    float v = strtof(buf, &end);
    if (end == buf) return false;
    out = v;
    return true;
}

static bool parse_uint16(const char *val, size_t len, uint16_t &out) {
    char buf[16];
    if (len >= sizeof(buf)) return false;
    memcpy(buf, val, len); buf[len] = '\0';
    char *end;
    long v = strtol(buf, &end, 10);
    if (end == buf || v < 0 || v > 65535) return false;
    out = (uint16_t)v;
    return true;
}

static bool parse_uint32(const char *val, size_t len, uint32_t &out) {
    char buf[16];
    if (len >= sizeof(buf)) return false;
    memcpy(buf, val, len); buf[len] = '\0';
    char *end;
    unsigned long v = strtoul(buf, &end, 10);
    if (end == buf) return false;
    out = (uint32_t)v;
    return true;
}


enum class Section { NONE, STROKE, DRIVER, MOTION, RC_PWM, LED, SENSOR };

static bool apply_key(Section sec, const char *key, size_t klen,
                      const char *val, size_t vlen, ServoConfig &cfg,
                      char *err, size_t esize) {
    bool ok = false;

    switch (sec) {
    case Section::STROKE:
        if (match(key, klen, "stroke_mm")) {
            float v; ok = parse_float(val, vlen, v);
            if (ok && v >= 1.0f && v <= 100.0f) cfg.stroke_mm = v;
            else if (ok) { snprintf(err, esize, "stroke_mm out of range (1-100)"); return false; }
        } else if (match(key, klen, "full_steps_per_mm")) {
            float v; ok = parse_float(val, vlen, v);
            if (ok && v >= 1.0f && v <= 2000.0f) cfg.full_steps_per_mm = v;
            else if (ok) { snprintf(err, esize, "full_steps_per_mm out of range (1-2000)"); return false; }
        }
        break;

    case Section::DRIVER:
        if (match(key, klen, "dir_invert")) {
            ok = parse_bool(val, vlen, cfg.dir_invert);
        } else if (match(key, klen, "run_current_ma")) {
            uint16_t v; ok = parse_uint16(val, vlen, v);
            if (ok && v >= 50 && v <= 2000) cfg.run_current_ma = v;
            else if (ok) { snprintf(err, esize, "run_current_ma out of range (50-2000)"); return false; }
        } else if (match(key, klen, "hold_current_ma")) {
            uint16_t v; ok = parse_uint16(val, vlen, v);
            if (ok && v <= 2000) cfg.hold_current_ma = v;
            else if (ok) { snprintf(err, esize, "hold_current_ma out of range (0-2000)"); return false; }
        }
        break;

    case Section::MOTION:
        if (match(key, klen, "default_speed_mm_s")) {
            float v; ok = parse_float(val, vlen, v);
            if (ok && v >= 0.1f && v <= 200.0f) cfg.default_speed_mm_s = v;
            else if (ok) { snprintf(err, esize, "default_speed_mm_s out of range (0.1-200)"); return false; }
        } else if (match(key, klen, "max_accel_mm_s2")) {
            float v; ok = parse_float(val, vlen, v);
            if (ok && v >= 1.0f && v <= 1000.0f) cfg.max_accel_mm_s2 = v;
            else if (ok) { snprintf(err, esize, "max_accel_mm_s2 out of range (1-1000)"); return false; }
        } else if (match(key, klen, "auto_disable_ms")) {
            uint32_t v; ok = parse_uint32(val, vlen, v);
            if (ok) cfg.auto_disable_ms = v;
        }
        break;

    case Section::RC_PWM:
        if (match(key, klen, "min_us")) {
            uint32_t v; ok = parse_uint32(val, vlen, v);
            if (ok && v >= 500 && v <= 3000) cfg.pwm_min_us = v;
            else if (ok) { snprintf(err, esize, "min_us out of range (500-3000)"); return false; }
        } else if (match(key, klen, "max_us")) {
            uint32_t v; ok = parse_uint32(val, vlen, v);
            if (ok && v >= 500 && v <= 3000) cfg.pwm_max_us = v;
            else if (ok) { snprintf(err, esize, "max_us out of range (500-3000)"); return false; }
        }
        break;

    case Section::LED:
        if (match(key, klen, "dark_mode")) {
            ok = parse_bool(val, vlen, cfg.led_dark_mode);
        }
        break;

    case Section::SENSOR:
        if (match(key, klen, "use_hall_effect")) {
            ok = parse_bool(val, vlen, cfg.use_hall_effect);
        } else if (match(key, klen, "lost_step_threshold_mv")) {
            ok = parse_float(val, vlen, cfg.lost_step_threshold_mv);
        }
        break;

    default:
        break;
    }

    // Unknown key — silently ignore (forward-compatible)
    if (!ok && err[0] == '\0') {
        // Only flag parse errors, not unknown keys
    }
    return true;
}


bool config_ini_parse(const char *ini_text, size_t len,
                      ServoConfig &cfg, char *err_buf, size_t err_size) {
    if (err_buf && err_size > 0) err_buf[0] = '\0';

    Section section = Section::NONE;
    const char *p = ini_text;
    const char *end = ini_text + len;
    int line_num = 0;

    while (p < end) {
        // Find end of line
        const char *eol = p;
        while (eol < end && *eol != '\n') eol++;

        size_t line_len = (size_t)(eol - p);
        line_num++;

        const char *lp = skip_ws(p);
        size_t content_len = trim_end(lp, (size_t)(eol - lp));

        // Skip blank lines and comments
        if (content_len == 0 || *lp == ';' || *lp == '#') {
            p = (eol < end) ? eol + 1 : end;
            continue;
        }

        // Section header: [name]
        if (*lp == '[') {
            const char *close = (const char *)memchr(lp, ']', content_len);
            if (close) {
                const char *sname = lp + 1;
                size_t slen = (size_t)(close - sname);
                if (match(sname, slen, "stroke"))        section = Section::STROKE;
                else if (match(sname, slen, "driver"))   section = Section::DRIVER;
                else if (match(sname, slen, "motion"))   section = Section::MOTION;
                else if (match(sname, slen, "rc_pwm"))   section = Section::RC_PWM;
                else if (match(sname, slen, "led"))      section = Section::LED;
                else if (match(sname, slen, "sensor"))   section = Section::SENSOR;
                else section = Section::NONE;
            }
            p = (eol < end) ? eol + 1 : end;
            continue;
        }

        // Key = value
        const char *eq = (const char *)memchr(lp, '=', content_len);
        if (eq) {
            const char *key = lp;
            size_t klen = trim_end(key, (size_t)(eq - key));
            const char *val = skip_ws(eq + 1);
            size_t vlen = trim_end(val, content_len - (size_t)(val - lp));

            if (!apply_key(section, key, klen, val, vlen, cfg,
                           err_buf, err_size)) {
                // Error already set in err_buf
                return false;
            }
        }

        p = (eol < end) ? eol + 1 : end;
    }

    // Validation: min_us < max_us
    if (cfg.pwm_min_us >= cfg.pwm_max_us) {
        if (err_buf) snprintf(err_buf, err_size,
            "min_us (%lu) must be less than max_us (%lu)",
            (unsigned long)cfg.pwm_min_us, (unsigned long)cfg.pwm_max_us);
        return false;
    }

    return true;
}
