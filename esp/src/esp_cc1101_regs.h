/* esp_cc1101_regs.h — CC1101 register PRESETS + carrier tuning math (Esp §2, §3), pure C.
 *
 * A bare ESP32 drives the CC1101 directly (no Flipper subghz_devices abstraction), so it needs
 * the register byte tables the Flipper firmware ships as presets. These preset->register tables
 * and the FREQ-word tuning math are HARDWARE-INDEPENDENT artifacts (mirroring the stock Flipper
 * CC1101 async presets) and are unit-tested off-device; the actual SPI burst-writes that push
 * them into the radio are the firmware's job (TODO(hw) — the radio proves the physics).
 *
 * Presets mirror the capture presets the shared model uses (Esp §3, Zero §3): OOK 650/270 kHz
 * RX bandwidth for ASK/OOK, and 2-FSK at narrow / wide deviation.
 */
#ifndef ESP_CC1101_REGS_H
#define ESP_CC1101_REGS_H

#include <stddef.h>
#include <stdint.h>

/* CC1101 config-register addresses (datasheet §29) used by the presets below. */
#define CC1101_IOCFG0 0x02
#define CC1101_FIFOTHR 0x03
#define CC1101_PKTCTRL0 0x08
#define CC1101_FSCTRL1 0x0B
#define CC1101_FREQ2 0x0D
#define CC1101_FREQ1 0x0E
#define CC1101_FREQ0 0x0F
#define CC1101_MDMCFG4 0x10
#define CC1101_MDMCFG3 0x11
#define CC1101_MDMCFG2 0x12
#define CC1101_DEVIATN 0x15
#define CC1101_MCSM0 0x18
#define CC1101_FOCCFG 0x19
#define CC1101_AGCCTRL2 0x1B
#define CC1101_AGCCTRL1 0x1C
#define CC1101_AGCCTRL0 0x1D
#define CC1101_WORCTRL 0x21
#define CC1101_FREND0 0x22
#define CC1101_FSCAL3 0x23
#define CC1101_FSCAL2 0x24
#define CC1101_FSCAL1 0x25
#define CC1101_FSCAL0 0x26
#define CC1101_TEST2 0x2C
#define CC1101_TEST1 0x2D
#define CC1101_TEST0 0x2E

/* CC1101 crystal (26 MHz on the common Elechouse/E07 modules). */
#define ESP_CC1101_XTAL_HZ 26000000u
#define ESP_CC1101_PATABLE_LEN 8

typedef struct {
    uint8_t addr;
    uint8_t value;
} EspCc1101Reg;

typedef enum {
    ESP_CC1101_PRESET_OOK650 = 0,  /* ASK/OOK, 650 kHz RX bandwidth */
    ESP_CC1101_PRESET_OOK270 = 1,  /* ASK/OOK, 270 kHz RX bandwidth */
    ESP_CC1101_PRESET_2FSK = 2,    /* 2-FSK, narrow deviation (~2.4 kHz) */
    ESP_CC1101_PRESET_2FSK_DEV = 3, /* 2-FSK, wide deviation (~47.6 kHz) */
    ESP_CC1101_PRESET_COUNT = 4,
} EspCc1101Preset;

/* Return the preset's register (addr,value) table; sets *n to its length. FREQ2/1/0 are NOT in
 * the table — carrier is applied separately via esp_cc1101_freq_regs() (esp_cc1101_tune). */
const EspCc1101Reg* esp_cc1101_preset_regs(EspCc1101Preset p, size_t* n);

/* Return the preset's 8-byte PATABLE (ASK/OOK uses index0=off/index1=on; FSK a single level). */
const uint8_t* esp_cc1101_preset_patable(EspCc1101Preset p);

/* Compute the CC1101 FREQ2/FREQ1/FREQ0 registers for a carrier: freq_word =
 * round(freq_hz * 2^16 / f_xosc). Returns the 24-bit freq word; writes the byte registers to
 * freq2, freq1, freq0 (any may be NULL). */
uint32_t esp_cc1101_freq_regs(int32_t freq_hz, uint8_t* freq2, uint8_t* freq1, uint8_t* freq0);

#endif /* ESP_CC1101_REGS_H */
