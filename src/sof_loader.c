#include "sof_priv.h"
#include <drivers/serial/serial.h>
#include <fs/vfs.h>
#include <mm/dma.h>
#include <mm/paging.h>
#include <ansii.h>
#include <string.h>

bool sof_loader_read(sof_t *c) {
    INode_t *file = NULL;
    path_t   p    = vfs_path_from_abs(SOF_FW_PATH);
    if (vfs_lookup(&p, &file) < 0 || !file) {
        serial_printf(LOG_ERROR "sof: no firmware at " SOF_FW_PATH "\n");
        return false;
    }

    size_t size = vfs_filesize(file);
    c->image_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    c->image = c->image_pages ? dma_alloc(c->image_pages, &c->image_phys, !c->ok64) : NULL;
    if (!c->image || inode_read(file, c->image, size, 0) != (long)size) {
        serial_printf(LOG_ERROR "sof: couldnt read %llu bytes of firmware\n", (uint64_t)size);
        vfs_drop(file);
        sof_loader_free(c);
        return false;
    }
    vfs_drop(file);

    // the rom wants the signed image, the extended manifest is for us
    size_t skip = 0;
    if (size > 8 && *(uint32_t *)c->image == SOF_XMAN_MAGIC)
        skip = *(uint32_t *)(c->image + 4);
    if (skip >= size) {
        serial_printf(LOG_ERROR "sof: manifest says %llu of %llu bytes\n", (uint64_t)skip, (uint64_t)size);
        sof_loader_free(c);
        return false;
    }

    c->image_size = size - skip;
    memmove(c->image, c->image + skip, c->image_size);
    serial_printf(LOG_INFO "sof: firmware %llu bytes, image %llu after %llu of manifest, starts 0x%x\n", (uint64_t)size,
                  (uint64_t)c->image_size, (uint64_t)skip, *(uint32_t *)c->image);
    return true;
}

void sof_loader_free(sof_t *c) {
    if (c->image)
        dma_free(c->image_phys, c->image_pages);
    c->image = NULL;
    c->image_phys = 0;
    c->image_pages = 0;
}

static void ppctl_stream(sof_t *c, bool decouple) {
    uint32_t reg = c->pp + SOF_PP_PPCTL;
    uint32_t bit = 1u << c->index;
    uint32_t val = hda_rd(c, reg);
    hda_wr(c, reg, decouple ? val | bit : val & ~bit);
}

static void sd_ctl(sof_t *c, uint32_t clear, uint32_t set) {
    uint32_t reg = c->sd + SOF_SD_CTL;
    hda_wr(c, reg, (hda_rd(c, reg) & ~(clear | SOF_SDSTS_MASK)) | set);
}

static bool sof_stream_reset(sof_t *c) {
    sd_ctl(c, SOF_SDCTL_RUN | SOF_SDCTL_INTS, 0);
    if (!sof_poll(c->hda, c->sd + SOF_SD_CTL, SOF_SDCTL_RUN, 0, SOF_STREAM_WAIT_MS))
        serial_printf(LOG_WARN "sof: stream wont stop, ctl 0x%x\n", hda_rd(c, c->sd + SOF_SD_CTL));
    sd_ctl(c, 0, SOF_SDSTS_CLEAR);

    sd_ctl(c, 0, SOF_SDCTL_SRST);
    if (!sof_poll(c->hda, c->sd + SOF_SD_CTL, SOF_SDCTL_SRST, SOF_SDCTL_SRST, SOF_STREAM_WAIT_MS))
        return false;
    sd_ctl(c, SOF_SDCTL_SRST, 0);
    return sof_poll(c->hda, c->sd + SOF_SD_CTL, SOF_SDCTL_SRST, 0, SOF_STREAM_WAIT_MS);
}

static uint32_t sof_bdl_fill(sof_t *c) {
    uint32_t n = 0;
    for (size_t at = 0; at < c->image_size; at += SOF_BDL_CHUNK, n++) {
        size_t left = c->image_size - at;
        c->bdl[n].addr   = (uint64_t)c->image_phys + at;
        c->bdl[n].length = (uint32_t)(left < SOF_BDL_CHUNK ? left : SOF_BDL_CHUNK);
        c->bdl[n].flags  = 0;
    }
    return n;
}

bool sof_stream_program(sof_t *c, paddr_t bdl, uint32_t frags, uint32_t bytes, uint16_t format) {
    ppctl_stream(c, true);
    if (!sof_stream_reset(c)) {
        serial_printf(LOG_ERROR "sof: stream wont reset, ctl 0x%x\n", hda_rd(c, c->sd + SOF_SD_CTL));
        return false;
    }

    sd_ctl(c, SOF_SDCTL_TAG_MASK, (uint32_t)SOF_LOADER_TAG << SOF_SDCTL_TAG_SHIFT);
    hda_wr(c, c->sd + SOF_SD_CBL, bytes);

    // has to be coupled while the format goes in on this generation
    ppctl_stream(c, false);
    mmio_write16(c->hda, c->sd + SOF_SD_FMT, format);
    ppctl_stream(c, true);

    mmio_write16(c->hda, c->sd + SOF_SD_LVI, (uint16_t)(frags - 1));
    hda_wr(c, c->sd + SOF_SD_BDPL, (uint32_t)bdl);
    hda_wr(c, c->sd + SOF_SD_BDPU, (uint32_t)((uint64_t)bdl >> 32));
    return true;
}

void sof_stream_run(sof_t *c, bool run) {
    if (run)
        sd_ctl(c, 0, SOF_SDCTL_RUN | SOF_SDCTL_INTS);
    else
        sd_ctl(c, SOF_SDCTL_RUN | SOF_SDCTL_INTS, 0);

    if (!sof_poll(c->hda, c->sd + SOF_SD_CTL, SOF_SDCTL_RUN, run ? SOF_SDCTL_RUN : 0, SOF_STREAM_WAIT_MS))
        serial_printf(LOG_WARN "sof: stream wont %s, ctl 0x%x\n", run ? "start" : "stop", hda_rd(c, c->sd + SOF_SD_CTL));
}

bool sof_loader_prepare(sof_t *c) {
    c->bdl = dma_alloc(1, &c->bdl_phys, !c->ok64);
    if (!c->bdl) {
        serial_printf(LOG_ERROR "sof: OOM for the bdl\n");
        return false;
    }

    uint32_t frags = sof_bdl_fill(c);
    if (frags < 2 || frags > PAGE_SIZE / sizeof(sof_bdl_entry_t)) {
        serial_printf(LOG_ERROR "sof: %u bdl entries for this image\n", frags);
        return false;
    }

    if (!sof_stream_program(c, c->bdl_phys, frags, (uint32_t)c->image_size, SOF_LOADER_FORMAT))
        return false;

    // spib is where the dsp is allowed to read up to
    uint32_t spib = c->spib + SOF_SPIB_CTL;
    hda_wr(c, spib, hda_rd(c, spib) | (1u << c->index));
    hda_wr(c, c->spib + SOF_SPIB_BASE + SOF_SPIB_STEP * c->index, (uint32_t)c->image_size);

    serial_printf(LOG_INFO "sof: loader stream %u ready, %u bdl entries, %u bytes, ctl 0x%x\n", c->index, frags,
                  (uint32_t)c->image_size, hda_rd(c, c->sd + SOF_SD_CTL));
    return true;
}

void sof_loader_cleanup(sof_t *c) {
    sd_ctl(c, SOF_SDCTL_RUN | SOF_SDCTL_INTS, 0);

    uint32_t spib = c->spib + SOF_SPIB_CTL;
    hda_wr(c, spib, hda_rd(c, spib) & ~(1u << c->index));
    hda_wr(c, c->spib + SOF_SPIB_BASE + SOF_SPIB_STEP * c->index, 0);

    hda_wr(c, c->sd + SOF_SD_BDPL, 0);
    hda_wr(c, c->sd + SOF_SD_BDPU, 0);
    hda_wr(c, c->sd + SOF_SD_CTL, 0);

    if (c->bdl)
        dma_free(c->bdl_phys, 1);
    c->bdl = NULL;
    c->bdl_phys = 0;
}

bool sof_loader_copy(sof_t *c) {
    sof_stream_run(c, true);
    serial_printf(LOG_INFO "sof: loader dma on, ctl 0x%x\n", hda_rd(c, c->sd + SOF_SD_CTL));

    bool ok = sof_poll(c->dsp, SOF_ROM_STATUS, 0xFFFFFF, SOF_FSR_FW_ENTERED, SOF_FW_WAIT_MS);
    serial_printf(ok ? LOG_OK "sof: firmware entered, lpib 0x%x\n" : LOG_ERROR "sof: lpib 0x%x and no firmware\n",
                  hda_rd(c, c->sd + SOF_SD_LPIB));

    sof_stream_run(c, false);
    return ok;
}
