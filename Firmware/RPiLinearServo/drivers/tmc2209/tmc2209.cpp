// TMC2209 SilentStepStick register driver via hardware UART1.
//
// PCB wiring (RP2040-Zero):
//   GP4 → [R1 1K] → TMC2209 PDN/UART bus   (UART1_TX)
//   GP5 ──────────→ TMC2209 PDN/UART bus   (UART1_RX, loopback)
//
// Protocol: 8N1, 115200 baud, LSB first.
// Write datagram:  [0x05, addr, reg|0x80, d3, d2, d1, d0, CRC]  (8 bytes TX)
// Read access:     [0x05, addr, reg,      CRC]                   (4 bytes TX)
//                  [0x05, 0xFF, reg, d3, d2, d1, d0, CRC]       (8 bytes RX)
// Note: TX bytes echo back on RX bus — echo bytes are drained automatically.

#include "tmc2209.h"
#include "pins.h"

#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include <cstdio>
#include <cstring>

static constexpr uint32_t TMC2209_BAUD       = 115200;
static constexpr uint8_t  TMC2209_SLAVE_ADDR  = 0x00;   // MS1=0, MS2=0 on SilentStepStick
static constexpr uint8_t  TMC2209_SYNC        = 0x05;

#define TMC_UART  uart1   // GP4=UART1_TX, GP5=UART1_RX (SDK pointer macro)


static uint s_tx_pin   = 0;

// CRC-8 (TMC2209 polynomial 0x07)
static uint8_t crc8(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        uint8_t b = data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if ((crc >> 7) ^ (b & 0x01)) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
            b >>= 1;
        }
    }
    return crc;
}

// Low-level byte TX/RX

// Read one byte from UART1 with timeout.  Returns -1 on timeout.
static int uart_getc_timeout_us(uint32_t timeout_us) {
    uint32_t start = time_us_32();
    while (!uart_is_readable(TMC_UART)) {
        if ((uint32_t)(time_us_32() - start) >= timeout_us) return -1;
        tight_loop_contents();
    }
    return (int)(uint8_t)uart_getc(TMC_UART);
}

// Drain all pending RX bytes (echo and stray)
static void uart_drain() {
    while (uart_is_readable(TMC_UART)) {
        uart_getc(TMC_UART);
    }
}

// Datagram helpers

// Release the TX pin (set as GPIO input) so the TMC2209 can drive the bus.
static void tx_tristate() {
    gpio_set_function(s_tx_pin, GPIO_FUNC_SIO);
    gpio_set_dir(s_tx_pin, GPIO_IN);
}

// Restore the TX pin to UART function.
static void tx_enable() {
    gpio_set_function(s_tx_pin, GPIO_FUNC_UART);
}

// Send N bytes and consume the N echo bytes that appear on the RX bus.
static void send_bytes(const uint8_t *buf, uint8_t n) {
    uart_write_blocking(TMC_UART, buf, n);
    // Wait for all TX bytes to be clocked out of the UART shift register.
    // At 115200 baud, 1 byte ≈ 87 µs; n bytes + some margin.
    sleep_us(n * 100 + 200);
    // Drain exactly n echo bytes
    for (uint8_t i = 0; i < n; i++) {
        uart_getc_timeout_us(3000);
    }
    // Extra drain: clear any lingering bytes
    sleep_us(100);
    uart_drain();
}

// Public API

void tmc2209_write_reg(uint8_t reg, uint32_t value) {
    uint8_t buf[8];
    buf[0] = TMC2209_SYNC;
    buf[1] = TMC2209_SLAVE_ADDR;
    buf[2] = reg | 0x80;          // write bit
    buf[3] = (value >> 24) & 0xFF;
    buf[4] = (value >> 16) & 0xFF;
    buf[5] = (value >>  8) & 0xFF;
    buf[6] = (value      ) & 0xFF;
    buf[7] = crc8(buf, 7);

    uart_drain();
    send_bytes(buf, 8);
}

uint32_t tmc2209_read_reg(uint8_t reg) {
    // Build 4-byte read request
    uint8_t req[4];
    req[0] = TMC2209_SYNC;
    req[1] = TMC2209_SLAVE_ADDR;
    req[2] = reg;                  // no write bit
    req[3] = crc8(req, 3);

    // Step 1: Clear any stale RX data
    uart_drain();

    // Step 2: Send read request
    uart_write_blocking(TMC_UART, req, 4);

    // Wait for TX shift register to fully clock out all 4 bytes.
    // Pico SDK: uart_tx_wait_blocking waits until TX FIFO + shift register empty.
    uart_tx_wait_blocking(TMC_UART);

    // Step 3: Read back 4 echo bytes (our own TX reflected on the bus)
    for (int i = 0; i < 4; i++) {
        int b = uart_getc_timeout_us(5000);
        (void)b;   // discard echo
    }

    // Step 4: Release TX pin so TMC2209 can drive the bus
    tx_tristate();

    // Step 5: Read 8-byte reply
    // TMC2209 replies after ~4-8 byte-times.  We already consumed time
    // draining the echo, so the reply may already be arriving.
    uint8_t reply[8] = {0};
    bool ok = true;
    for (int i = 0; i < 8; i++) {
        int b = uart_getc_timeout_us(20000);   // 20 ms per byte timeout
        if (b < 0) { ok = false; break; }
        reply[i] = (uint8_t)b;
    }

    // Step 6: Restore TX pin
    tx_enable();
    sleep_us(100);   // let UART TX re-stabilize

    if (!ok) {
        printf("[tmc2209] read reg 0x%02x: timeout\n", reg);
        return 0xFFFFFFFF;
    }

    // Verify reply CRC
    uint8_t expected_crc = crc8(reply, 7);
    if (reply[7] != expected_crc) {
        printf("[tmc2209] read reg 0x%02x: CRC error (got 0x%02x, expected 0x%02x)\n",
               reg, reply[7], expected_crc);
        return 0xFFFFFFFF;
    }

    return ((uint32_t)reply[3] << 24) |
           ((uint32_t)reply[4] << 16) |
           ((uint32_t)reply[5] <<  8) |
           ((uint32_t)reply[6]      );
}


// Helpers for IHOLD_IRUN and MRES

// Convert mA to TMC2209 current scale (CS) value.
// I_RMS = (CS+1)/32 × V_FS/(√2 × R_sense)
// V_FS = 0.325 V, R_sense = 0.11 Ω (standard SilentStepStick)
// CS = I_mA × 32 × √2 × R_sense / (V_FS × 1000) - 1
static uint8_t ma_to_cs(uint16_t ma) {
    float cs_f = (float)ma * 32.0f * 1.4142f * 0.11f / (0.325f * 1000.0f) - 1.0f;
    if (cs_f < 0.0f) cs_f = 0.0f;
    if (cs_f > 31.0f) cs_f = 31.0f;
    return (uint8_t)(cs_f + 0.5f);
}

// Convert microsteps integer to MRES field (CHOPCONF bits 27:24).
static uint8_t microsteps_to_mres(uint16_t ms) {
    if (ms >= 256) return 0;
    if (ms >= 128) return 1;
    if (ms >=  64) return 2;
    if (ms >=  32) return 3;
    if (ms >=  16) return 4;
    if (ms >=   8) return 5;
    if (ms >=   4) return 6;
    if (ms >=   2) return 7;
    return 8;  // full step
}

bool tmc2209_init(uint tx_pin, uint rx_pin) {
    s_tx_pin   = tx_pin;

    // Initialise hardware UART1 at 115200 8N1
    uart_init(TMC_UART, TMC2209_BAUD);
    gpio_set_function(tx_pin, GPIO_FUNC_UART);
    gpio_set_function(rx_pin, GPIO_FUNC_UART);

    // Disable hardware flow control (not used)
    uart_set_hw_flow(TMC_UART, false, false);
    uart_set_format(TMC_UART, 8, 1, UART_PARITY_NONE);

    printf("[tmc2209] init OK: TX=GP%u RX=GP%u baud=%u (UART1)\n",
           tx_pin, rx_pin, TMC2209_BAUD);
    return true;
}

bool tmc2209_configure(const ServoConfig &cfg) {
    printf("[tmc2209] configuring...\n");

    // Read IFCNT before to track accepted writes
    uint32_t ifcnt_before = tmc2209_read_reg(TMC2209_REG_IFCNT);
    printf("[tmc2209] IFCNT before = %lu\n", (unsigned long)ifcnt_before);
    sleep_ms(10);

    // Clear GSTAT (write 0x07 to clear all flags)
    tmc2209_write_reg(TMC2209_REG_GSTAT, 0x07);
    sleep_ms(10);

    // GCONF
    // bit 0 (I_scale_analog): 0 = use internal 5VOUT ref (ignore VREF pot on SilentStepStick)
    // bit 2 (en_SpreadCycle): 0 = StealthChop, 1 = SpreadCycle
    // bit 6 (pdn_disable): MUST be 1 when using UART
    // bit 7 (mstep_reg_select): use MRES from CHOPCONF register
    // bit 8 (multistep_filt): step pulse filter (reset default)
    uint32_t gconf = (1u << 6) | (1u << 7) | (1u << 8);
    if (!cfg.stealthchop_en) gconf |= (1u << 2);

    tmc2209_write_reg(TMC2209_REG_GCONF, gconf);
    sleep_ms(20);
    uint32_t rb_gconf = tmc2209_read_reg(TMC2209_REG_GCONF);
    printf("[tmc2209] GCONF write 0x%03lX -> readback 0x%03lX%s\n",
           (unsigned long)gconf, (unsigned long)rb_gconf,
           (rb_gconf != gconf) ? " (mismatch, accepting)" : " OK");
    sleep_ms(10);

    // IHOLD_IRUN
    uint8_t cs_run  = ma_to_cs(cfg.run_current_ma);
    uint8_t cs_hold = ma_to_cs(cfg.hold_current_ma);
    uint32_t ihold_irun = ((uint32_t)6       << 16)   // IHOLDDELAY
                        | ((uint32_t)cs_run  <<  8)   // IRUN
                        | ((uint32_t)cs_hold <<  0);  // IHOLD
    tmc2209_write_reg(TMC2209_REG_IHOLD_IRUN, ihold_irun);
    sleep_ms(10);

    // CHOPCONF
    // Read-modify-write: only change MRES and intpol, preserve OTP defaults
    // for TOFF/HSTRT/HEND/TBL/vsense so StealthChop auto-tuning stays intact.
    uint8_t  mres = microsteps_to_mres(cfg.microsteps);
    uint32_t rb_chop = tmc2209_read_reg(TMC2209_REG_CHOPCONF);
    sleep_ms(5);
    uint32_t chopconf;
    if (rb_chop != 0xFFFFFFFF) {
        chopconf = (rb_chop & ~(0xFu << 24))   // clear old MRES
                 | ((uint32_t)mres << 24)       // set new MRES
                 | (1u << 28);                  // intpol=1 (256 microstep interp)
    } else {
        // Fallback if read failed: use known-good base
        chopconf = 0x10018053u | ((uint32_t)mres << 24);
    }
    tmc2209_write_reg(TMC2209_REG_CHOPCONF, chopconf);
    printf("[tmc2209] CHOPCONF: read 0x%08lX -> write 0x%08lX\n",
           (unsigned long)rb_chop, (unsigned long)chopconf);
    sleep_ms(10);

    // TPOWERDOWN
    tmc2209_write_reg(TMC2209_REG_TPOWERDOWN, cfg.tpowerdown);
    sleep_ms(10);


    // Verify & DRV_STATUS
    sleep_ms(20);
    uint32_t rb_gconf2   = tmc2209_read_reg(TMC2209_REG_GCONF);
    sleep_ms(5);
    uint32_t rb_chopconf = tmc2209_read_reg(TMC2209_REG_CHOPCONF);
    sleep_ms(5);
    uint32_t rb_drv      = tmc2209_read_reg(TMC2209_REG_DRV_STATUS);
    sleep_ms(5);
    uint32_t ifcnt_after = tmc2209_read_reg(TMC2209_REG_IFCNT);

    printf("[tmc2209] configured: IRUN=%u (%umA) IHOLD=%u (%umA) mres=%u (%ux)\n",
           cs_run, cfg.run_current_ma, cs_hold, cfg.hold_current_ma,
           mres, cfg.microsteps);
    printf("[tmc2209]   IHOLD_IRUN=0x%06lX  (IHOLDDELAY=%u IRUN=%u IHOLD=%u)\n",
           (unsigned long)ihold_irun, 6, cs_run, cs_hold);
    printf("[tmc2209]   GCONF=0x%08lX CHOPCONF=0x%08lX IFCNT=%lu\n",
           (unsigned long)rb_gconf2, (unsigned long)rb_chopconf,
           (unsigned long)ifcnt_after);
    if (rb_chopconf != 0xFFFFFFFF) {
        bool vsense_rb = (rb_chopconf >> 17) & 1;
        uint8_t tbl_rb = (rb_chopconf >> 15) & 3;
        float vfs = vsense_rb ? 0.180f : 0.325f;
        float i_run_mA  = (cs_run  + 1) / 32.0f * vfs / (1.4142f * 0.11f) * 1000.0f;
        float i_hold_mA = (cs_hold + 1) / 32.0f * vfs / (1.4142f * 0.11f) * 1000.0f;
        printf("[tmc2209]   CHOPCONF decode: vsense=%d TBL=%u TOFF=%u\n",
               vsense_rb, tbl_rb, (unsigned)(rb_chopconf & 0xF));
        printf("[tmc2209]   Expected current: IRUN~%.0fmA IHOLD~%.0fmA (Rsense=0.11, vsense=%d)\n",
               i_run_mA, i_hold_mA, vsense_rb);
    }
    printf("[tmc2209]   DRV_STATUS=0x%08lX", (unsigned long)rb_drv);
    if (rb_drv != 0xFFFFFFFF) {
        // TMC2209 DRV_STATUS bit positions (datasheet 5.5.3):
        //   [31]=stst [30]=stealth [20:16]=CS_ACTUAL
        //   [11]=t157 [10]=t150 [9]=t143 [8]=t120
        //   [7]=olb [6]=ola [5]=s2vsb [4]=s2vsa [3]=s2gb [2]=s2ga
        //   [1]=ot [0]=otpw
        printf(" [");
        if (rb_drv & (1u<<31)) printf("stst ");
        if (rb_drv & (1u<<30)) printf("stealth ");
        if (rb_drv & (1u<< 1)) printf("ot ");
        if (rb_drv & (1u<< 0)) printf("otpw ");
        if (rb_drv & (1u<< 6)) printf("ola ");
        if (rb_drv & (1u<< 7)) printf("olb ");
        if (rb_drv & (1u<< 4)) printf("s2vsa ");
        if (rb_drv & (1u<< 5)) printf("s2vsb ");
        if (rb_drv & (1u<< 2)) printf("s2ga ");
        if (rb_drv & (1u<< 3)) printf("s2gb ");
        if (rb_drv & (1u<< 8)) printf("t120 ");
        if (rb_drv & (1u<< 9)) printf("t143 ");
        if (rb_drv & (1u<<10)) printf("t150 ");
        if (rb_drv & (1u<<11)) printf("t157 ");
        printf("CS=%lu]",
               (unsigned long)((rb_drv >> 16) & 0x1F));
    }
    printf("\n");

    return true;
}
