/* test_esp_cc1101_regs.c — CC1101 preset register tables + carrier tuning math (Esp §2/§3).
 * These are hardware-independent artifacts (mirroring the stock Flipper presets); the SPI
 * writes + antenna are the on-device TODO(hw). Here we prove the tables + tuning math only. */
#include "esp_cc1101_regs.h"
#include "sc_test.h"

/* Find a register's value in a preset table; returns -1 if absent. */
static int reg_val(EspCc1101Preset p, uint8_t addr) {
    size_t n = 0;
    const EspCc1101Reg* r = esp_cc1101_preset_regs(p, &n);
    for(size_t i = 0; i < n; i++)
        if(r[i].addr == addr) return r[i].value;
    return -1;
}

int main(void) {
    printf("test_esp_cc1101_regs\n");

    /* every preset returns a non-empty table */
    for(int p = 0; p < ESP_CC1101_PRESET_COUNT; p++) {
        size_t n = 0;
        const EspCc1101Reg* r = esp_cc1101_preset_regs((EspCc1101Preset)p, &n);
        SC_CHECK(r != NULL && n > 0, "preset table present");
    }

    /* OOK vs 2-FSK modulation format (MDMCFG2) + RX bandwidth (MDMCFG4) distinguish presets */
    SC_CHECK_INT(reg_val(ESP_CC1101_PRESET_OOK650, CC1101_MDMCFG2), 0x30); /* ASK/OOK */
    SC_CHECK_INT(reg_val(ESP_CC1101_PRESET_2FSK, CC1101_MDMCFG2), 0x00);   /* 2-FSK */
    SC_CHECK_INT(reg_val(ESP_CC1101_PRESET_OOK650, CC1101_MDMCFG4), 0x17); /* 650 kHz BW */
    SC_CHECK_INT(reg_val(ESP_CC1101_PRESET_OOK270, CC1101_MDMCFG4), 0x67); /* 270 kHz BW */
    /* the two FSK presets differ in deviation (DEVIATN) */
    SC_CHECK(reg_val(ESP_CC1101_PRESET_2FSK, CC1101_DEVIATN) !=
                 reg_val(ESP_CC1101_PRESET_2FSK_DEV, CC1101_DEVIATN),
             "FSK narrow vs wide deviation differ");
    /* GDO0 is async serial data out on every preset (drives RMT edge capture, Esp §3) */
    for(int p = 0; p < ESP_CC1101_PRESET_COUNT; p++)
        SC_CHECK_INT(reg_val((EspCc1101Preset)p, CC1101_IOCFG0), 0x0D);

    /* PATABLE: ASK/OOK needs off + on entries; FSK a single power level */
    const uint8_t* pa_ook = esp_cc1101_preset_patable(ESP_CC1101_PRESET_OOK650);
    const uint8_t* pa_fsk = esp_cc1101_preset_patable(ESP_CC1101_PRESET_2FSK);
    SC_CHECK(pa_ook[0] == 0x00 && pa_ook[1] == 0xC0, "OOK PATABLE off/on");
    SC_CHECK(pa_fsk[0] == 0xC0, "FSK PATABLE single level");

    /* --- carrier tuning math: FREQ2/1/0 = round(freq * 2^16 / 26 MHz) --- */
    uint8_t f2, f1, f0;
    uint32_t word = esp_cc1101_freq_regs(433920000, &f2, &f1, &f0);
    SC_CHECK_INT(f2, 0x10); /* known FREQ2 for 433.92 MHz @ 26 MHz xtal */
    /* the byte registers reassemble to the returned 24-bit word */
    SC_CHECK_INT(((uint32_t)f2 << 16) | ((uint32_t)f1 << 8) | f0, word);
    /* round-trips back to the carrier within one LSB (~397 Hz) */
    double back = (double)word * ESP_CC1101_XTAL_HZ / 65536.0;
    SC_CHECK_DBL(back, 433920000.0, 400.0);
    /* monotonic: a higher carrier yields a larger word */
    SC_CHECK(esp_cc1101_freq_regs(915000000, 0, 0, 0) >
                 esp_cc1101_freq_regs(315000000, 0, 0, 0),
             "freq word monotonic in carrier");

    return sc_test_summary();
}
