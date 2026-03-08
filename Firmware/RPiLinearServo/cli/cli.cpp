// ── cli.cpp ─────────────────────────────────────────────────────────────
// Interactive command-line interface over USB CDC.

#include "cli.h"
#include "stepgen.h"
#include "homing.h"
#include "tmc2209.h"
#include "config.h"
#include "pins.h"
#include "status_led.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>

// ── Line buffer ────────────────────────────────────────────────────────
static constexpr size_t CLI_BUF_SIZE = 128;
static char     s_buf[CLI_BUF_SIZE];
static size_t   s_buf_pos = 0;
static uint32_t s_default_speed_hz = 0;   // initialised in cli_init()

// ── Forward declarations ───────────────────────────────────────────────
static void cli_process_line(char *line);
static void cli_prompt();

// ── Helpers ────────────────────────────────────────────────────────────

// Skip leading whitespace, return pointer to first non-space or '\0'.
static char *skip_ws(char *p) {
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

// Return next whitespace-delimited token; advance *pp past it.
// Returns nullptr if nothing left.
static char *next_token(char **pp) {
    char *p = skip_ws(*pp);
    if (*p == '\0') return nullptr;
    char *start = p;
    while (*p && !isspace((unsigned char)*p)) p++;
    if (*p) *p++ = '\0';   // null-terminate the token
    *pp = p;
    return start;
}

// ── Public API ─────────────────────────────────────────────────────────

void cli_init() {
    s_buf_pos = 0;
    s_default_speed_hz = g_config.default_speed_hz();
    cli_prompt();
}

void cli_poll() {
    int c = getchar_timeout_us(0);
    if (c == PICO_ERROR_TIMEOUT) return;

    // Echo the character
    if (c == '\r' || c == '\n') {
        printf("\r\n");
        s_buf[s_buf_pos] = '\0';
        if (s_buf_pos > 0) {
            cli_process_line(s_buf);
        }
        s_buf_pos = 0;
        cli_prompt();
    } else if (c == '\b' || c == 127) {
        // Backspace
        if (s_buf_pos > 0) {
            s_buf_pos--;
            printf("\b \b");
        }
    } else if (s_buf_pos < CLI_BUF_SIZE - 1) {
        s_buf[s_buf_pos++] = (char)c;
        putchar(c);
    }
}

// ── Command handlers ───────────────────────────────────────────────────

static void cmd_help() {
    printf("Commands:\n");
    printf("  move <steps> [speed_hz]  Move relative steps (negative = reverse)\n");
    printf("  run [speed_hz]           Continuous stepping\n");
    printf("  stop                     Stop immediately\n");
    printf("  home                     Run homing sequence\n");
    printf("  enable                   Enable motor driver\n");
    printf("  disable                  Disable motor driver\n");
    printf("  dir <fwd|rev>            Set direction for 'run'\n");
    printf("  speed <hz>               Set default speed\n");
    printf("  ramp <from> <to> <steps> Linear speed ramp\n");
    printf("  pos                      Print position\n");
    printf("  status                   Print full status\n");
    printf("  help                     This message\n");
}

static void cmd_move(char *args) {
    char *tok = next_token(&args);
    if (!tok) {
        printf("usage: move <steps> [speed_hz]\n");
        return;
    }
    int32_t steps = atoi(tok);

    tok = next_token(&args);
    uint32_t hz = tok ? (uint32_t)atoi(tok) : s_default_speed_hz;

    if (hz == 0) hz = s_default_speed_hz;
    printf("move %ld steps @ %lu Hz  accel=%lu Hz/s\n",
           (long)steps, (unsigned long)hz,
           (unsigned long)g_config.accel_hz_per_s());
    gpio_put(PIN_EN, g_config.en_active_low ? 0 : 1);
    status_led_set(LedStatus::MOVING);
    stepgen_move_accel(steps, hz, g_config.accel_hz_per_s());
}

static void cmd_run(char *args) {
    char *tok = next_token(&args);
    uint32_t hz = tok ? (uint32_t)atoi(tok) : s_default_speed_hz;
    if (hz == 0) hz = s_default_speed_hz;
    printf("run @ %lu Hz\n", (unsigned long)hz);
    gpio_put(PIN_EN, g_config.en_active_low ? 0 : 1);
    status_led_set(LedStatus::MOVING);
    stepgen_run(hz);
}

static void cmd_stop() {
    stepgen_stop();
    printf("stopped at pos %ld\n", (long)stepgen_get_position());
    status_led_set(LedStatus::HOLDING);
}

static void cmd_home() {
    if (stepgen_is_busy()) {
        printf("error: motor busy — stop first\n");
        return;
    }    gpio_put(PIN_EN, g_config.en_active_low ? 0 : 1);    homing_run();
}

static void cmd_enable() {
    gpio_put(PIN_EN, g_config.en_active_low ? 0 : 1);
    printf("driver enabled\n");
    status_led_set(LedStatus::HOLDING);
}

static void cmd_disable() {
    stepgen_stop();
    gpio_put(PIN_EN, g_config.en_active_low ? 1 : 0);
    printf("driver disabled\n");
    status_led_set(LedStatus::OFF);
}

static void cmd_dir(char *args) {
    char *tok = next_token(&args);
    if (!tok) {
        printf("direction: %s\n", stepgen_get_dir() ? "fwd" : "rev");
        return;
    }
    if (strcmp(tok, "fwd") == 0 || strcmp(tok, "1") == 0) {
        stepgen_set_dir(true);
        printf("direction: fwd\n");
    } else if (strcmp(tok, "rev") == 0 || strcmp(tok, "0") == 0) {
        stepgen_set_dir(false);
        printf("direction: rev\n");
    } else {
        printf("usage: dir <fwd|rev>\n");
    }
}

static void cmd_speed(char *args) {
    char *tok = next_token(&args);
    if (!tok) {
        printf("default speed: %lu Hz\n", (unsigned long)s_default_speed_hz);
        return;
    }
    s_default_speed_hz = (uint32_t)atoi(tok);
    printf("default speed set to %lu Hz\n", (unsigned long)s_default_speed_hz);
}

static void cmd_ramp(char *args) {
    char *tok1 = next_token(&args);
    char *tok2 = next_token(&args);
    char *tok3 = next_token(&args);
    if (!tok1 || !tok2 || !tok3) {
        printf("usage: ramp <from_hz> <to_hz> <steps>\n");
        return;
    }
    uint32_t from_hz   = (uint32_t)atoi(tok1);
    uint32_t to_hz     = (uint32_t)atoi(tok2);
    int32_t  steps     = atoi(tok3);
    if (steps == 0 || from_hz == 0 || to_hz == 0) {
        printf("error: all values must be non-zero\n");
        return;
    }

    int32_t  abs_steps  = steps > 0 ? steps : -steps;
    printf("ramp %lu → %lu Hz over %ld steps\n",
           (unsigned long)from_hz, (unsigned long)to_hz, (long)steps);

    // Start continuous stepping at initial speed
    stepgen_set_dir(steps > 0);
    stepgen_set_speed_hz(from_hz);
    gpio_put(PIN_EN, g_config.en_active_low ? 0 : 1);
    status_led_set(LedStatus::MOVING);
    stepgen_run(from_hz);

    // Ramp speed linearly based on position
    int32_t start_pos = stepgen_get_position();
    while (true) {
        int32_t elapsed = stepgen_get_position() - start_pos;
        if (steps > 0 && elapsed < 0) elapsed = -elapsed;
        if (steps < 0) elapsed = -elapsed;
        if (elapsed >= abs_steps) break;

        // Linear interpolation
        float t = (float)elapsed / (float)abs_steps;
        uint32_t hz = from_hz + (uint32_t)(t * (float)((int32_t)to_hz - (int32_t)from_hz));
        stepgen_set_speed_hz(hz);
        sleep_ms(2);
    }

    stepgen_stop();
    printf("ramp complete at pos %ld\n", (long)stepgen_get_position());
    status_led_set(LedStatus::HOLDING);
}

static void cmd_pos() {
    int32_t pos = stepgen_get_position();
    float mm = (float)pos / g_config.steps_per_mm();
    printf("pos: %ld steps  (%.3f mm)\n", (long)pos, mm);
}

static void cmd_status() {
    bool en_pin = gpio_get(PIN_EN);
    bool driver_on = g_config.en_active_low ? !en_pin : en_pin;

    printf("── status ──\n");
    printf("  driver:    %s\n", driver_on ? "ENABLED" : "disabled");
    printf("  busy:      %s\n", stepgen_is_busy() ? "yes" : "no");
    printf("  direction: %s\n", stepgen_get_dir() ? "fwd" : "rev");
    printf("  speed:     %lu Hz\n", (unsigned long)stepgen_get_speed_hz());
    printf("  default:   %lu Hz\n", (unsigned long)s_default_speed_hz);
    int32_t pos = stepgen_get_position();
    printf("  position:  %ld steps (%.3f mm)\n",
           (long)pos, (float)pos / g_config.steps_per_mm());
    printf("  config:    %.0f steps/mm, stroke %.1f mm\n",
           g_config.steps_per_mm(), g_config.stroke_mm);
}

static void cmd_diag() {
    // Only read registers that are readable (R or RW) in the TMC2209.
    // IHOLD_IRUN, TPWMTHRS, TPOWERDOWN are write-only.
    printf("── TMC2209 registers ──\n");

    uint32_t gconf = tmc2209_read_reg(TMC2209_REG_GCONF);
    sleep_ms(5);
    uint32_t gstat = tmc2209_read_reg(TMC2209_REG_GSTAT);
    sleep_ms(5);
    uint32_t ifcnt = tmc2209_read_reg(TMC2209_REG_IFCNT);
    sleep_ms(5);
    uint32_t chopconf = tmc2209_read_reg(TMC2209_REG_CHOPCONF);
    sleep_ms(5);
    uint32_t tstep = tmc2209_read_reg(TMC2209_REG_TSTEP);
    sleep_ms(5);
    uint32_t drv_stat = tmc2209_read_reg(TMC2209_REG_DRV_STATUS);

    printf("  GCONF      = 0x%08lX", (unsigned long)gconf);
    if (gconf != 0xFFFFFFFF) {
        printf("  en_spread=%d pdn_dis=%d mstep_reg=%d multistep=%d",
               (int)((gconf >> 2) & 1), (int)((gconf >> 6) & 1),
               (int)((gconf >> 7) & 1), (int)((gconf >> 8) & 1));
    }
    printf("\n");
    printf("  GSTAT      = 0x%08lX\n", (unsigned long)gstat);
    printf("  IFCNT      = %lu\n",     (unsigned long)ifcnt);
    printf("  CHOPCONF   = 0x%08lX", (unsigned long)chopconf);
    if (chopconf != 0xFFFFFFFF) {
        bool vs = (chopconf >> 17) & 1;
        printf("  vsense=%d TBL=%u TOFF=%u mres=%u intpol=%d",
               vs, (unsigned)((chopconf >> 15) & 3),
               (unsigned)(chopconf & 0xF),
               (unsigned)((chopconf >> 24) & 0xF),
               (int)((chopconf >> 28) & 1));
    }
    printf("\n");
    printf("  TSTEP      = %lu\n",     (unsigned long)tstep);
    printf("  DRV_STATUS = 0x%08lX\n", (unsigned long)drv_stat);
    // Show current estimate from CS_ACTUAL
    if (drv_stat != 0xFFFFFFFF && chopconf != 0xFFFFFFFF) {
        uint32_t cs_act = (drv_stat >> 16) & 0x1F;
        bool vs = (chopconf >> 17) & 1;
        float vfs = vs ? 0.180f : 0.325f;
        float i_mA = (cs_act + 1) / 32.0f * vfs / (1.4142f * 0.11f) * 1000.0f;
        printf("  CS_ACTUAL=%lu → ~%.0fmA (vsense=%d, Rsense=0.11)\n",
               (unsigned long)cs_act, i_mA, vs);
    }
    if (drv_stat != 0xFFFFFFFF) {
        // TMC2209 DRV_STATUS bit map (datasheet 5.5.3):
        //   [31]=stst [30]=stealth [20:16]=CS_ACTUAL
        //   [11]=t157 [10]=t150 [9]=t143 [8]=t120
        //   [7]=olb [6]=ola [5]=s2vsb [4]=s2vsa [3]=s2gb [2]=s2ga
        //   [1]=ot [0]=otpw
        printf("  \u2514\u2500 stealth=%d stst=%d CS=%lu ot=%d otpw=%d ola=%d olb=%d t120=%d t143=%d t150=%d t157=%d\n",
               (int)((drv_stat >> 30) & 1),
               (int)((drv_stat >> 31) & 1),
               (unsigned long)((drv_stat >> 16) & 0x1F),
               (int)((drv_stat >>  1) & 1),
               (int)((drv_stat >>  0) & 1),
               (int)((drv_stat >>  6) & 1),
               (int)((drv_stat >>  7) & 1),
               (int)((drv_stat >>  8) & 1),
               (int)((drv_stat >>  9) & 1),
               (int)((drv_stat >> 10) & 1),
               (int)((drv_stat >> 11) & 1));
    }
}

static void cmd_wreg(char *args) {
    char *tok1 = next_token(&args);
    char *tok2 = next_token(&args);
    if (!tok1 || !tok2) {
        printf("usage: wreg <reg_hex> <value_hex>\n");
        return;
    }
    uint8_t  reg = (uint8_t)strtoul(tok1, nullptr, 16);
    uint32_t val = strtoul(tok2, nullptr, 16);
    printf("write reg 0x%02X = 0x%08lX\n", reg, (unsigned long)val);
    tmc2209_write_reg(reg, val);
    sleep_ms(20);
    uint32_t rb = tmc2209_read_reg(reg);
    printf("readback   0x%02X = 0x%08lX\n", reg, (unsigned long)rb);
}

static void cmd_rreg(char *args) {
    char *tok = next_token(&args);
    if (!tok) {
        printf("usage: rreg <reg_hex>\n");
        return;
    }
    uint8_t reg = (uint8_t)strtoul(tok, nullptr, 16);
    uint32_t val = tmc2209_read_reg(reg);
    printf("reg 0x%02X = 0x%08lX (%lu)\n", reg, (unsigned long)val, (unsigned long)val);
}

// ── Dispatcher ─────────────────────────────────────────────────────────

static void cli_process_line(char *line) {
    char *cmd = next_token(&line);
    if (!cmd) return;

    if      (strcmp(cmd, "help")    == 0) cmd_help();
    else if (strcmp(cmd, "move")    == 0) cmd_move(line);
    else if (strcmp(cmd, "run")     == 0) cmd_run(line);
    else if (strcmp(cmd, "stop")    == 0) cmd_stop();
    else if (strcmp(cmd, "home")    == 0) cmd_home();
    else if (strcmp(cmd, "enable")  == 0) cmd_enable();
    else if (strcmp(cmd, "disable") == 0) cmd_disable();
    else if (strcmp(cmd, "dir")     == 0) cmd_dir(line);
    else if (strcmp(cmd, "speed")   == 0) cmd_speed(line);
    else if (strcmp(cmd, "ramp")    == 0) cmd_ramp(line);
    else if (strcmp(cmd, "pos")     == 0) cmd_pos();
    else if (strcmp(cmd, "status")  == 0) cmd_status();
    else if (strcmp(cmd, "diag")    == 0) cmd_diag();
    else if (strcmp(cmd, "wreg")    == 0) cmd_wreg(line);
    else if (strcmp(cmd, "rreg")    == 0) cmd_rreg(line);
    else printf("unknown command: %s  (type 'help')\n", cmd);
}

static void cli_prompt() {
    printf("> ");
}
