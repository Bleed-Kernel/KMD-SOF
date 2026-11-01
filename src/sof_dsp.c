#include "sof_priv.h"
#include <drivers/serial/serial.h>
#include <ansii.h>

static void cs_bits(sof_t *c, uint32_t mask, uint32_t val) {
    dsp_wr(c, SOF_ADSPCS, (dsp_rd(c, SOF_ADSPCS) & ~mask) | val);
}

static bool sof_core_stall_reset(sof_t *c, uint8_t mask) {
    cs_bits(c, SOF_ADSPCS_CSTALL(mask), SOF_ADSPCS_CSTALL(mask));
    cs_bits(c, SOF_ADSPCS_CRST(mask), SOF_ADSPCS_CRST(mask));
    return sof_poll(c->dsp, SOF_ADSPCS, SOF_ADSPCS_CRST(mask), SOF_ADSPCS_CRST(mask), SOF_RESET_WAIT_MS);
}

static bool sof_core_power_up(sof_t *c, uint8_t mask) {
    cs_bits(c, SOF_ADSPCS_SPA(mask), SOF_ADSPCS_SPA(mask));
    return sof_poll(c->dsp, SOF_ADSPCS, SOF_ADSPCS_CPA(mask), SOF_ADSPCS_CPA(mask), SOF_RESET_WAIT_MS);
}

static bool sof_core_power_down(sof_t *c, uint8_t mask) {
    if (!sof_core_stall_reset(c, mask))
        return false;
    cs_bits(c, SOF_ADSPCS_SPA(mask), 0);
    return sof_poll(c->dsp, SOF_ADSPCS, SOF_ADSPCS_CPA(mask), 0, SOF_RESET_WAIT_MS);
}

static bool sof_core_run(sof_t *c, uint8_t mask) {
    cs_bits(c, SOF_ADSPCS_CRST(mask), 0);
    if (!sof_poll(c->dsp, SOF_ADSPCS, SOF_ADSPCS_CRST(mask), 0, SOF_RESET_WAIT_MS))
        return false;

    cs_bits(c, SOF_ADSPCS_CSTALL(mask), 0);
    uint32_t cs = dsp_rd(c, SOF_ADSPCS);
    return (cs & SOF_ADSPCS_CPA(mask)) == SOF_ADSPCS_CPA(mask) && !(cs & SOF_ADSPCS_CSTALL(mask));
}

void sof_dsp_off(sof_t *c) {
    if (!sof_core_power_down(c, SOF_CORE_MASK))
        serial_printf(LOG_WARN "sof: cores wont power down, adspcs 0x%x\n", dsp_rd(c, SOF_ADSPCS));
}

void sof_dsp_dump(sof_t *c, const char *why) {
    uint32_t rom = dsp_rd(c, SOF_ROM_STATUS);
    serial_printf(LOG_ERROR "sof: %s\n", why);
    serial_printf(LOG_ERROR "sof: adspcs 0x%x hipci 0x%x hipcie 0x%x hipct 0x%x hipcte 0x%x\n", dsp_rd(c, SOF_ADSPCS),
                  dsp_rd(c, SOF_HIPCI), dsp_rd(c, SOF_HIPCIE), dsp_rd(c, SOF_HIPCT), dsp_rd(c, SOF_HIPCTE));
    serial_printf(LOG_ERROR "sof: rom status 0x%x (module %u state 0x%x) error 0x%x\n", rom, SOF_FSR_MODULE(rom),
                  SOF_FSR_STATE(rom), dsp_rd(c, SOF_ROM_ERROR));
    serial_printf(LOG_ERROR "sof: stream ctl 0x%x lpib 0x%x cbl 0x%x\n", hda_rd(c, c->sd + SOF_SD_CTL),
                  hda_rd(c, c->sd + SOF_SD_LPIB), hda_rd(c, c->sd + SOF_SD_CBL));
}

// cores up, rom told where the image will come from, then let core 0 run
bool sof_dsp_rom_init(sof_t *c) {
    sof_dsp_off(c);

    if (!sof_core_power_up(c, SOF_CORE_MASK)) {
        sof_dsp_dump(c, "cores wont power up");
        return false;
    }
    serial_printf(LOG_INFO "sof: cores powered, adspcs 0x%x\n", dsp_rd(c, SOF_ADSPCS));

    for (int i = 0; i < SOF_SSP_COUNT; i++) {
        uint32_t reg = SOF_SSP_BASE + i * SOF_SSP_SIZE + SOF_SSP_SSC1;
        dsp_wr(c, reg, dsp_rd(c, reg) | SOF_SSP_CONSUMER);
    }

    dsp_wr(c, SOF_HIPCI, SOF_HIPCI_BUSY | SOF_ROM_CONTROL | SOF_ROM_PURGE_FW |
                         ((uint32_t)(SOF_LOADER_TAG - 1) << SOF_ROM_TAG_SHIFT));

    if (!sof_core_run(c, SOF_BOOT_CORE)) {
        sof_dsp_dump(c, "core 0 wont run");
        return false;
    }

    if (!sof_poll(c->dsp, SOF_HIPCIE, SOF_HIPCIE_DONE, SOF_HIPCIE_DONE, SOF_IPC_WAIT_MS)) {
        sof_dsp_dump(c, "rom never took the control message");
        return false;
    }
    dsp_wr(c, SOF_HIPCIE, dsp_rd(c, SOF_HIPCIE) | SOF_HIPCIE_DONE);

    if (!sof_core_power_down(c, SOF_CORE_MASK & ~SOF_BOOT_CORE))
        serial_printf(LOG_WARN "sof: core 1 wont power down, adspcs 0x%x\n", dsp_rd(c, SOF_ADSPCS));

    if (!sof_poll(c->dsp, SOF_ROM_STATUS, 0xFFFFFF, SOF_FSR_INIT_DONE, SOF_ROM_WAIT_MS)) {
        sof_dsp_dump(c, "rom never finished init");
        return false;
    }

    serial_printf(LOG_OK "sof: rom is up, status 0x%x\n", dsp_rd(c, SOF_ROM_STATUS));
    return true;
}
