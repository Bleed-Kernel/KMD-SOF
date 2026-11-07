#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <pci/pci.h>
#include <cpu/mmio.h>
#include <mm/pmm.h>
#include <ACPI/acpi_hpet.h>
#include <devices/type/audio_device.h>
#include "sof_regs.h"

#define SOF_FW_PATH             "/lib/firmware/sof-glk.ri"
#define SOF_XMAN_MAGIC          0x6E614D58
#define SOF_CORE_MASK           0x3
#define SOF_BOOT_CORE           0x1
#define SOF_BOOT_TRIES          3
#define SOF_LOADER_TAG          1
#define SOF_LOADER_FORMAT       0x40
#define SOF_BDL_CHUNK           4096
#define SOF_MBOX_DUMP           16

// from the windows in the firmwares extended manifest, sram window 1 is ours and 0 is theirs
#define SOF_HOSTBOX             0xA0000
#define SOF_HOSTBOX_SIZE        0x2000
#define SOF_IPC_LOG_MAX         16

#define SOF_PERIODS             32
#define SOF_PERIOD_BYTES        1024
#define SOF_RING_BYTES          (SOF_PERIODS * SOF_PERIOD_BYTES)
#define SOF_RATE                48000
#define SOF_PCM_FORMAT          0x11
#define SOF_DPIB_BASE           0x1084      // host side position once the stream is decoupled
#define SOF_DPIB_STEP           0x20
#define SOF_BACK_PERIODS        2
#define SOF_STUCK_TICKS         500
#define SOF_VOL_UNITY           0x10000
#define SOF_VOL_STEP_Q16        62013       // 0.48dB a percent, same curve as the other cards

#define SOF_RESET_WAIT_MS       50
#define SOF_LINK_WAIT_MS        100
#define SOF_STREAM_WAIT_MS      10
#define SOF_IPC_WAIT_MS         500
#define SOF_ROM_WAIT_MS         150
#define SOF_FW_WAIT_MS          3000

typedef struct {
    uint64_t addr;
    uint32_t length;
    uint32_t flags;
} __attribute__((packed)) sof_bdl_entry_t;

typedef struct {
    pci_device_t pci;
    uint64_t     hda;
    uint64_t     dsp;
    uint32_t     pp;
    uint32_t     spib;
    uint16_t     gcap;
    bool         ok64;
    uint32_t     sd;            // output stream 0, the loader
    uint32_t     index;         // its number across every stream

    paddr_t      bdl_phys;
    sof_bdl_entry_t *bdl;
    paddr_t      image_phys;
    uint8_t     *image;
    size_t       image_pages;
    size_t       image_size;

    bool         ipc_dead;
    uint32_t     told_ipc;

    paddr_t      ring_phys;
    uint8_t     *ring;
    paddr_t      ring_bdl_phys;
    sof_bdl_entry_t *ring_bdl;
    paddr_t      table_phys;
    uint8_t     *table;
    uint32_t     last_period;
    uint32_t     stuck_ticks;
    bool         told_moving;
    audio_device_t *audio;
} sof_t;

static inline uint32_t hda_rd(sof_t *c, uint32_t reg)             { return mmio_read32(c->hda, reg); }
static inline void     hda_wr(sof_t *c, uint32_t reg, uint32_t v) { mmio_write32(c->hda, reg, v); }
static inline uint32_t dsp_rd(sof_t *c, uint32_t reg)             { return mmio_read32(c->dsp, reg); }
static inline void     dsp_wr(sof_t *c, uint32_t reg, uint32_t v) { mmio_write32(c->dsp, reg, v); }

static inline bool sof_waited(uint64_t start, uint32_t ms) {
    return hpet_get_femtoseconds() - start >= (uint64_t)ms * 1000000000000ULL;
}

// spins
bool sof_poll(uint64_t base, uint32_t reg, uint32_t mask, uint32_t want, uint32_t ms);

bool sof_ctrl_init(sof_t *c);
void sof_gating(sof_t *c, bool enable);

void sof_dsp_off(sof_t *c);
bool sof_dsp_rom_init(sof_t *c);
void sof_dsp_dump(sof_t *c, const char *why);

void sof_amp_on(sof_t *c);

int  sof_ipc_send(sof_t *c, void *msg, uint32_t size);
void sof_ipc_drain(sof_t *c);

bool sof_pipe_build(sof_t *c);
int  sof_pipe_volume(sof_t *c, uint32_t left, uint32_t right);
int  sof_pipe_params(sof_t *c);
int  sof_pipe_start(sof_t *c);
void sof_pipe_stop(sof_t *c);
void sof_pipe_free(sof_t *c);

bool sof_audio_up(sof_t *c);
int  sof_audio_down(sof_t *c);
void sof_audio_free(sof_t *c);

bool sof_stream_program(sof_t *c, paddr_t bdl, uint32_t frags, uint32_t bytes, uint16_t format);
void sof_stream_run(sof_t *c, bool run);
bool sof_loader_read(sof_t *c);
void sof_loader_free(sof_t *c);
bool sof_loader_prepare(sof_t *c);
void sof_loader_cleanup(sof_t *c);
bool sof_loader_copy(sof_t *c);
