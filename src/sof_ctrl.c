#include "sof_priv.h"
#include <drivers/serial/serial.h>
#include <ansii.h>

bool sof_poll(uint64_t base, uint32_t reg, uint32_t mask, uint32_t want, uint32_t ms) {
    uint64_t start = hpet_get_femtoseconds();
    while (!sof_waited(start, ms)) {
        if ((mmio_read32(base, reg) & mask) == want)
            return true;
        asm volatile("pause");
    }
    return false;
}

static void cgctl_bits(sof_t *c, uint32_t mask, uint32_t val) {
    pci_device_t *p = &c->pci;
    uint32_t reg = pci_read32(p->bus, p->device, p->function, SOF_PCI_CGCTL);
    pci_write32(p->bus, p->device, p->function, SOF_PCI_CGCTL, (reg & ~mask) | val);
}

static void pgctl_bits(sof_t *c, uint16_t mask, uint16_t val) {
    pci_device_t *p = &c->pci;
    uint16_t reg = pci_read16(p->bus, p->device, p->function, SOF_PCI_PGCTL);
    pci_write16(p->bus, p->device, p->function, SOF_PCI_PGCTL, (reg & ~mask) | val);
}

// off while the firmware loads or the dsp can get gated away under us
void sof_gating(sof_t *c, bool enable) {
    cgctl_bits(c, SOF_CGCTL_ADSPDCGE, enable ? SOF_CGCTL_ADSPDCGE : 0);

    uint32_t em2 = hda_rd(c, SOF_EM2);
    hda_wr(c, SOF_EM2, enable ? em2 | SOF_EM2_L1SEN : em2 & ~SOF_EM2_L1SEN);

    pgctl_bits(c, SOF_PGCTL_ADSPPGD, enable ? 0 : SOF_PGCTL_ADSPPGD);
}

static bool sof_link_reset(sof_t *c, bool reset) {
    uint32_t gctl = hda_rd(c, SOF_GCTL);
    hda_wr(c, SOF_GCTL, reset ? gctl & ~SOF_GCTL_CRST : gctl | SOF_GCTL_CRST);
    if (sof_poll(c->hda, SOF_GCTL, SOF_GCTL_CRST, reset ? 0 : SOF_GCTL_CRST, SOF_LINK_WAIT_MS))
        return true;

    serial_printf(LOG_ERROR "sof: link wont %s reset, gctl 0x%x\n", reset ? "enter" : "leave", hda_rd(c, SOF_GCTL));
    return false;
}

static bool sof_link_cycle(sof_t *c) {
    if (!sof_link_reset(c, true))
        return false;
    hpet_busy_wait_ms(1);
    if (!sof_link_reset(c, false))
        return false;
    hpet_busy_wait_ms(2);
    return true;
}

static bool sof_caps(sof_t *c) {
    uint32_t off = hda_rd(c, SOF_LLCH) & 0xFFFF;

    for (int i = 0; off && i < 16; i++) {
        uint32_t cap = hda_rd(c, off);
        if (cap == 0xFFFFFFFF)
            break;

        serial_printf(LOG_INFO "sof: capability 0x%x at 0x%x\n", SOF_CAP_ID(cap), off);
        if (SOF_CAP_ID(cap) == SOF_CAP_ID_PP)
            c->pp = off;
        if (SOF_CAP_ID(cap) == SOF_CAP_ID_SPIB)
            c->spib = off;
        off = SOF_CAP_NEXT(cap);
    }
    return c->pp && c->spib;
}

bool sof_ctrl_init(sof_t *c) {
    // low three bits of this are tcsel, set they give static on the output
    pgctl_bits(c, SOF_PGCTL_TCSEL, 0);

    cgctl_bits(c, SOF_CGCTL_MISCBDCGE, 0);
    bool up = sof_link_cycle(c) && sof_link_cycle(c);
    cgctl_bits(c, SOF_CGCTL_MISCBDCGE, SOF_CGCTL_MISCBDCGE);
    if (!up)
        return false;

    serial_printf(LOG_INFO "sof: link up, gctl 0x%x statests 0x%x\n", hda_rd(c, SOF_GCTL),
                  mmio_read16(c->hda, SOF_STATESTS));

    if (!sof_caps(c)) {
        serial_printf(LOG_ERROR "sof: no pp or spib capability, pp 0x%x spib 0x%x\n", c->pp, c->spib);
        return false;
    }

    uint32_t ppctl = hda_rd(c, c->pp + SOF_PP_PPCTL);
    hda_wr(c, c->pp + SOF_PP_PPCTL, ppctl | SOF_PPCTL_GPROCEN);
    serial_printf(LOG_INFO "sof: dsp processing on, ppctl 0x%x\n", hda_rd(c, c->pp + SOF_PP_PPCTL));
    return true;
}
