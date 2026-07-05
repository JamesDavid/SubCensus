#include "esp_cc1101_regs.h"

#include <stddef.h>

/* Preset register dumps mirror the stock Flipper CC1101 async presets (the same physics the
 * Zero uses through subghz_devices). Common baseline: GDO0 = async serial data out (0x0D) so
 * GDO0 edges drive the RMT capture (Esp §3); PKTCTRL0 = 0x32 async infinite-length; the FSCAL/
 * TEST calibration block is shared. Presets differ in RX bandwidth (MDMCFG4), modulation format
 * (MDMCFG2), deviation (DEVIATN) and the front-end (FREND0). On-device RX validation is TODO(hw).
 */

static const EspCc1101Reg ook650[] = {
    {CC1101_IOCFG0, 0x0D},   {CC1101_FIFOTHR, 0x07},  {CC1101_PKTCTRL0, 0x32},
    {CC1101_FSCTRL1, 0x06},  {CC1101_MDMCFG4, 0x17},  {CC1101_MDMCFG3, 0x32},
    {CC1101_MDMCFG2, 0x30},  {CC1101_DEVIATN, 0x04},  {CC1101_MCSM0, 0x18},
    {CC1101_FOCCFG, 0x18},   {CC1101_AGCCTRL2, 0x07}, {CC1101_AGCCTRL1, 0x00},
    {CC1101_AGCCTRL0, 0x91}, {CC1101_WORCTRL, 0xFB},  {CC1101_FREND0, 0x11},
    {CC1101_FSCAL3, 0xE9},   {CC1101_FSCAL2, 0x2A},   {CC1101_FSCAL1, 0x00},
    {CC1101_FSCAL0, 0x1F},   {CC1101_TEST2, 0x81},    {CC1101_TEST1, 0x35},
    {CC1101_TEST0, 0x09},
};

/* OOK 270 kHz: narrower RX bandwidth (MDMCFG4 high nibble) for weaker/narrower emitters. */
static const EspCc1101Reg ook270[] = {
    {CC1101_IOCFG0, 0x0D},   {CC1101_FIFOTHR, 0x07},  {CC1101_PKTCTRL0, 0x32},
    {CC1101_FSCTRL1, 0x06},  {CC1101_MDMCFG4, 0x67},  {CC1101_MDMCFG3, 0x32},
    {CC1101_MDMCFG2, 0x30},  {CC1101_DEVIATN, 0x04},  {CC1101_MCSM0, 0x18},
    {CC1101_FOCCFG, 0x18},   {CC1101_AGCCTRL2, 0x07}, {CC1101_AGCCTRL1, 0x00},
    {CC1101_AGCCTRL0, 0x91}, {CC1101_WORCTRL, 0xFB},  {CC1101_FREND0, 0x11},
    {CC1101_FSCAL3, 0xE9},   {CC1101_FSCAL2, 0x2A},   {CC1101_FSCAL1, 0x00},
    {CC1101_FSCAL0, 0x1F},   {CC1101_TEST2, 0x81},    {CC1101_TEST1, 0x35},
    {CC1101_TEST0, 0x09},
};

/* 2-FSK, narrow deviation (~2.4 kHz): MDMCFG2=0x00 (2-FSK, no manchester/sync), FREND0=0x10
 * (no PA ramp table index), DEVIATN low. */
static const EspCc1101Reg fsk_narrow[] = {
    {CC1101_IOCFG0, 0x0D},   {CC1101_FIFOTHR, 0x07},  {CC1101_PKTCTRL0, 0x32},
    {CC1101_FSCTRL1, 0x06},  {CC1101_MDMCFG4, 0xC8},  {CC1101_MDMCFG3, 0x93},
    {CC1101_MDMCFG2, 0x00},  {CC1101_DEVIATN, 0x07},  {CC1101_MCSM0, 0x18},
    {CC1101_FOCCFG, 0x16},   {CC1101_AGCCTRL2, 0x43}, {CC1101_AGCCTRL1, 0x40},
    {CC1101_AGCCTRL0, 0x91}, {CC1101_WORCTRL, 0xFB},  {CC1101_FREND0, 0x10},
    {CC1101_FSCAL3, 0xE9},   {CC1101_FSCAL2, 0x2A},   {CC1101_FSCAL1, 0x00},
    {CC1101_FSCAL0, 0x1F},   {CC1101_TEST2, 0x81},    {CC1101_TEST1, 0x35},
    {CC1101_TEST0, 0x09},
};

/* 2-FSK, wide deviation (~47.6 kHz): same as narrow but DEVIATN raised (typical TPMS/telemetry). */
static const EspCc1101Reg fsk_dev[] = {
    {CC1101_IOCFG0, 0x0D},   {CC1101_FIFOTHR, 0x07},  {CC1101_PKTCTRL0, 0x32},
    {CC1101_FSCTRL1, 0x06},  {CC1101_MDMCFG4, 0xC8},  {CC1101_MDMCFG3, 0x93},
    {CC1101_MDMCFG2, 0x00},  {CC1101_DEVIATN, 0x47},  {CC1101_MCSM0, 0x18},
    {CC1101_FOCCFG, 0x16},   {CC1101_AGCCTRL2, 0x43}, {CC1101_AGCCTRL1, 0x40},
    {CC1101_AGCCTRL0, 0x91}, {CC1101_WORCTRL, 0xFB},  {CC1101_FREND0, 0x10},
    {CC1101_FSCAL3, 0xE9},   {CC1101_FSCAL2, 0x2A},   {CC1101_FSCAL1, 0x00},
    {CC1101_FSCAL0, 0x1F},   {CC1101_TEST2, 0x81},    {CC1101_TEST1, 0x35},
    {CC1101_TEST0, 0x09},
};

/* ASK/OOK needs two PA entries (off, on); FSK a single power level. 0xC0 ~ +10 dBm @ 433 MHz. */
static const uint8_t patable_ook[ESP_CC1101_PATABLE_LEN] = {0x00, 0xC0, 0, 0, 0, 0, 0, 0};
static const uint8_t patable_fsk[ESP_CC1101_PATABLE_LEN] = {0xC0, 0, 0, 0, 0, 0, 0, 0};

const EspCc1101Reg* esp_cc1101_preset_regs(EspCc1101Preset p, size_t* n) {
    switch(p) {
    case ESP_CC1101_PRESET_OOK650:
        if(n) *n = sizeof(ook650) / sizeof(ook650[0]);
        return ook650;
    case ESP_CC1101_PRESET_OOK270:
        if(n) *n = sizeof(ook270) / sizeof(ook270[0]);
        return ook270;
    case ESP_CC1101_PRESET_2FSK:
        if(n) *n = sizeof(fsk_narrow) / sizeof(fsk_narrow[0]);
        return fsk_narrow;
    case ESP_CC1101_PRESET_2FSK_DEV:
        if(n) *n = sizeof(fsk_dev) / sizeof(fsk_dev[0]);
        return fsk_dev;
    default:
        if(n) *n = 0;
        return NULL;
    }
}

const uint8_t* esp_cc1101_preset_patable(EspCc1101Preset p) {
    return (p == ESP_CC1101_PRESET_2FSK || p == ESP_CC1101_PRESET_2FSK_DEV) ? patable_fsk
                                                                            : patable_ook;
}

uint32_t esp_cc1101_freq_regs(int32_t freq_hz, uint8_t* freq2, uint8_t* freq1, uint8_t* freq0) {
    if(freq_hz < 0) freq_hz = 0;
    /* freq_word = round(freq_hz * 2^16 / f_xosc); 64-bit intermediate avoids overflow. */
    uint64_t word = ((uint64_t)freq_hz * 65536u + ESP_CC1101_XTAL_HZ / 2) / ESP_CC1101_XTAL_HZ;
    if(word > 0xFFFFFF) word = 0xFFFFFF; /* 24-bit register field */
    if(freq2) *freq2 = (uint8_t)((word >> 16) & 0xFF);
    if(freq1) *freq1 = (uint8_t)((word >> 8) & 0xFF);
    if(freq0) *freq0 = (uint8_t)(word & 0xFF);
    return (uint32_t)word;
}
