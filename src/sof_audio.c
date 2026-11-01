#include "sof_priv.h"
#include <drivers/serial/serial.h>
#include <cpu/irq.h>
#include <mm/dma.h>
#include <mm/paging.h>
#include <user/errno.h>
#include <ansii.h>
#include <string.h>

// the firmware takes ring pages as packed 20 bit frame numbers
static void sof_page_table(sof_t *c) {
    for (uint32_t i = 0; i < SOF_RING_BYTES / PAGE_SIZE; i++) {
        uint32_t pfn = (uint32_t)(((uint64_t)c->ring_phys + i * PAGE_SIZE) >> 12);
        uint8_t *at  = c->table + (i * 5) / 2;
        uint32_t val = (i & 1) ? (uint32_t)(at[0] & 0xF) | pfn << 4 : pfn;
        at[0] = (uint8_t)val;
        at[1] = (uint8_t)(val >> 8);
        at[2] = (uint8_t)(val >> 16);
        at[3] = (uint8_t)(val >> 24);
    }
}

static int sof_audio_start(void *priv) {
    sof_t *c = priv;
    c->last_period = 0;
    c->stuck_ticks = 0;

    if (!sof_stream_program(c, c->ring_bdl_phys, SOF_PERIODS, SOF_RING_BYTES, SOF_PCM_FORMAT))
        return -EIO;

    int r = sof_pipe_params(c);
    serial_printf(LOG_INFO "sof: pcm params -> %d\n", r);
    if (r < 0)
        return -EIO;

    sof_stream_run(c, true);
    r = sof_pipe_start(c);
    serial_printf(LOG_INFO "sof: stream start -> %d, ctl 0x%x\n", r, hda_rd(c, c->sd + SOF_SD_CTL));
    if (r < 0) {
        sof_stream_run(c, false);
        sof_pipe_free(c);
        return -EIO;
    }
    return 0;
}

static void sof_audio_stop(void *priv) {
    sof_t *c = priv;
    sof_pipe_stop(c);
    sof_stream_run(c, false);
    sof_pipe_free(c);
}

static uint32_t sof_audio_pos(sof_t *c) {
    uint32_t pos = hda_rd(c, SOF_DPIB_BASE + c->index * SOF_DPIB_STEP);
    return pos < SOF_RING_BYTES ? pos : 0;
}

static uint32_t sof_audio_offset(void *priv) {
    return sof_audio_pos(priv);
}

static int sof_audio_rate(void *priv, uint32_t rate) {
    (void)priv;
    (void)rate;
    return SOF_RATE;
}

static uint32_t sof_gain(uint8_t percent) {
    uint32_t amp = SOF_VOL_UNITY;
    for (int i = percent; i < 100; i++)
        amp = (amp * SOF_VOL_STEP_Q16) >> 16;
    return percent ? amp : 0;
}

static int sof_audio_volume(void *priv, const struct audio_volume *vol) {
    bool mute = vol->mute;
    int  r    = sof_pipe_volume(priv, mute ? 0 : sof_gain(vol->left), mute ? 0 : sof_gain(vol->right));
    return r < 0 ? -EIO : 0;
}

static const audio_driver_t sof_audio_driver = {
    .name       = "sof",
    .start      = sof_audio_start,
    .stop       = sof_audio_stop,
    .hw_offset  = sof_audio_offset,
    .set_rate   = sof_audio_rate,
    .set_volume = sof_audio_volume,
};

static void sof_tick(void *ctx) {
    sof_t *c = ctx;
    sof_ipc_drain(c);
    if (!c->audio || !c->audio->running)
        return;

    uint32_t period = sof_audio_pos(c) / SOF_PERIOD_BYTES;
    if (period == c->last_period) {
        if (++c->stuck_ticks == SOF_STUCK_TICKS)
            sof_dsp_dump(c, "stream hasnt moved");
        return;
    }

    if ((period + SOF_PERIODS - c->last_period) % SOF_PERIODS >= SOF_PERIODS - SOF_BACK_PERIODS)
        return;

    if (!c->told_moving) {
        c->told_moving = true;
        serial_printf(LOG_OK "sof: stream is moving, ctl 0x%x\n", hda_rd(c, c->sd + SOF_SD_CTL));
    }
    c->stuck_ticks = 0;
    c->last_period = period;
    audio_device_advance(c->audio, period);
}

void sof_audio_free(sof_t *c) {
    if (c->ring)
        dma_free(c->ring_phys, SOF_RING_BYTES / PAGE_SIZE);
    if (c->ring_bdl)
        dma_free(c->ring_bdl_phys, 1);
    if (c->table)
        dma_free(c->table_phys, 1);
    c->ring     = NULL;
    c->ring_bdl = NULL;
    c->table    = NULL;
}

bool sof_audio_up(sof_t *c) {
    c->ring     = dma_alloc(SOF_RING_BYTES / PAGE_SIZE, &c->ring_phys, !c->ok64);
    c->ring_bdl = dma_alloc(1, &c->ring_bdl_phys, !c->ok64);
    c->table    = dma_alloc(1, &c->table_phys, true);
    if (!c->ring || !c->ring_bdl || !c->table) {
        serial_printf(LOG_ERROR "sof: OOM for the audio ring\n");
        sof_audio_free(c);
        return false;
    }

    for (int i = 0; i < SOF_PERIODS; i++) {
        c->ring_bdl[i].addr   = (uint64_t)c->ring_phys + i * SOF_PERIOD_BYTES;
        c->ring_bdl[i].length = SOF_PERIOD_BYTES;
        c->ring_bdl[i].flags  = 0;
    }
    sof_page_table(c);

    c->audio = audio_device_register("audio0", &sof_audio_driver, c, c->ring_phys, SOF_PERIOD_BYTES, SOF_PERIODS,
                                     SOF_RATE, 2);
    if (!c->audio) {
        serial_printf(LOG_ERROR "sof: couldnt register /dev/audio0\n");
        sof_audio_free(c);
        return false;
    }

    if (tick_register(sof_tick, c) != 0) {
        serial_printf(LOG_ERROR "sof: no room for the tick\n");
        audio_device_unregister(c->audio);
        c->audio = NULL;
        sof_audio_free(c);
        return false;
    }
    return true;
}

int sof_audio_down(sof_t *c) {
    tick_unregister(sof_tick, c);
    int r = audio_device_unregister(c->audio);
    if (r < 0) {
        tick_register(sof_tick, c);
        return r;
    }
    c->audio = NULL;
    sof_audio_free(c);
    return 0;
}
