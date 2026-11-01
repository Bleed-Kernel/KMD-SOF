#include "sof_priv.h"
#include <kmd/kmd.h>
#include <drivers/serial/serial.h>
#include <mm/dma.h>
#include <user/errno.h>
#include <ansii.h>
#include <string.h>

static sof_t sof;
static bool  sof_bound;

static bool sof_boot(sof_t *c) {
    if (!sof_loader_prepare(c)) {
        sof_loader_cleanup(c);
        return false;
    }

    bool rom = false;
    for (int i = 0; i < SOF_BOOT_TRIES && !rom; i++) {
        serial_printf(LOG_INFO "sof: rom init, try %d\n", i + 1);
        rom = sof_dsp_rom_init(c);
    }

    bool fw = rom && sof_loader_copy(c);
    if (rom && !fw)
        sof_dsp_dump(c, "firmware never entered");

    sof_loader_cleanup(c);
    return fw;
}

// the first thing the firmware says, what it is and where its mailboxes are
static bool sof_fw_ready(sof_t *c) {
    if (!sof_poll(c->dsp, SOF_HIPCT, SOF_HIPCT_BUSY, SOF_HIPCT_BUSY, SOF_FW_WAIT_MS)) {
        sof_dsp_dump(c, "firmware entered but never said ready");
        return false;
    }

    uint32_t msg = dsp_rd(c, SOF_HIPCT) & ~SOF_HIPCT_BUSY;
    uint32_t ext = dsp_rd(c, SOF_HIPCTE);
    serial_printf(LOG_INFO "sof: first ipc 0x%x ext 0x%x\n", msg, ext);

    for (int i = 0; i < SOF_MBOX_DUMP; i += 4)
        serial_printf(LOG_INFO "sof: mbox +0x%x %08x %08x %08x %08x\n", i * 4, dsp_rd(c, SOF_MBOX_UPLINK + i * 4),
                      dsp_rd(c, SOF_MBOX_UPLINK + (i + 1) * 4), dsp_rd(c, SOF_MBOX_UPLINK + (i + 2) * 4),
                      dsp_rd(c, SOF_MBOX_UPLINK + (i + 3) * 4));

    dsp_wr(c, SOF_HIPCT, dsp_rd(c, SOF_HIPCT) | SOF_HIPCT_BUSY);
    return (msg & SOF_IPC_TYPE_MASK) == SOF_IPC_FW_READY;
}

static int sof_probe(kmd_device_t *dev) {
    sof_t *c = &sof;
    if (dev->pci.device_id != SOF_DEVICE_GLK) {
        serial_printf(LOG_INFO "sof: %04x:%04x isnt a dsp we know\n", dev->pci.vendor_id, dev->pci.device_id);
        return -ENODEV;
    }
    if (sof_bound)
        return -EBUSY;

    memset(c, 0, sizeof(*c));
    c->pci = dev->pci;

    uint64_t hda_size = pci_bar_size(&c->pci, 0);
    uint64_t dsp_size = pci_bar_size(&c->pci, 4);
    pci_enable(&c->pci);
    c->hda = pci_map_bar(&c->pci, 0, hda_size);
    c->dsp = pci_map_bar(&c->pci, 4, dsp_size);
    if (!hda_size || !dsp_size || !c->hda || !c->dsp) {
        serial_printf(LOG_ERROR "sof: couldnt map, bar0 %llu bytes bar4 %llu bytes\n", hda_size, dsp_size);
        return -EIO;
    }

    c->gcap  = mmio_read16(c->hda, SOF_GCAP);
    c->ok64  = c->gcap & SOF_GCAP_64OK;
    c->index = SOF_GCAP_ISS(c->gcap);
    c->sd    = SOF_SD_BASE + c->index * SOF_SD_SIZE;
    serial_printf(LOG_INFO "sof: %04x:%04x at " PCI_SPEC ", gcap 0x%x, %u in %u out, bar0 %llu bar4 %llu, adspcs 0x%x\n",
                  c->pci.vendor_id, c->pci.device_id, PCI_SPEC_ARGS(&c->pci), c->gcap, SOF_GCAP_ISS(c->gcap),
                  SOF_GCAP_OSS(c->gcap), hda_size, dsp_size, dsp_rd(c, SOF_ADSPCS));
    if (c->gcap == 0xFFFF || !SOF_GCAP_OSS(c->gcap))
        return -ENODEV;

    if (!sof_loader_read(c))
        return -ENOENT;
    if (!sof_ctrl_init(c)) {
        sof_loader_free(c);
        return -EIO;
    }

    sof_gating(c, false);
    bool booted = sof_boot(c);
    bool ready  = booted && sof_fw_ready(c);
    sof_gating(c, true);
    sof_loader_free(c);

    if (!ready) {
        sof_dsp_off(c);
        return -EIO;
    }

    serial_printf(LOG_OK "sof: firmware is running\n");
    sof_amp_on(c);

    if (!sof_pipe_build(c) || !sof_audio_up(c)) {
        serial_printf(LOG_ERROR "sof: couldnt get the speakers going\n");
        sof_dsp_off(c);
        return -EIO;
    }

    sof_bound = true;
    dev->priv = c;
    return 0;
}

static int sof_remove(kmd_device_t *dev) {
    sof_t *c = dev->priv;
    int    r = sof_audio_down(c);
    if (r < 0) {
        serial_printf(LOG_WARN "sof: /dev/audio0 is in use, staying (%d)\n", r);
        return r;
    }

    sof_dsp_off(c);
    sof_bound = false;
    serial_printf(LOG_OK "sof: dsp off\n");
    return 0;
}

static int sof_unload(void) {
    return 0;
}

static const kmd_pci_match_t sof_ids[] = {
    { PCI_VENDOR_INTEL, KMD_ANY, PCI_CLASS_MULTIMEDIA, PCI_SUBCLASS_AUDIO, KMD_ANY },
    KMD_PCI_END,
};

KMD_DRIVER(sof_kmd) = {
    .description = "Sound Open Firmware audio dsp",
    .pci         = sof_ids,
    .probe       = sof_probe,
    .remove      = sof_remove,
    .unload      = sof_unload,
};
