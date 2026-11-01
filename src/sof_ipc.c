#include "sof_priv.h"
#include "sof_ipc.h"
#include <drivers/serial/serial.h>
#include <ansii.h>

static void box_write(sof_t *c, uint32_t off, const void *msg, uint32_t size) {
    const uint8_t *src = msg;
    for (uint32_t i = 0; i + 4 <= size; i += 4) {
        uint32_t v = (uint32_t)src[i] | (uint32_t)src[i + 1] << 8 | (uint32_t)src[i + 2] << 16 | (uint32_t)src[i + 3] << 24;
        dsp_wr(c, off + i, v);
    }
}

// polled and synchronous, the audio layer calls some of this with its lock held
int sof_ipc_send(sof_t *c, void *msg, uint32_t size) {
    sof_cmd_hdr_t *hdr = msg;
    hdr->size = size;
    if (size > SOF_HOSTBOX_SIZE || c->ipc_dead)
        return -1;

    if (!sof_poll(c->dsp, SOF_HIPCI, SOF_HIPCI_BUSY, 0, SOF_IPC_WAIT_MS)) {
        serial_printf(LOG_ERROR "sof: ipc still busy before 0x%x, hipci 0x%x\n", hdr->cmd, dsp_rd(c, SOF_HIPCI));
        c->ipc_dead = true;
        return -1;
    }

    box_write(c, SOF_HOSTBOX, msg, size);
    dsp_wr(c, SOF_HIPCI, SOF_HIPCI_BUSY);

    if (!sof_poll(c->dsp, SOF_HIPCIE, SOF_HIPCIE_DONE, SOF_HIPCIE_DONE, SOF_IPC_WAIT_MS)) {
        sof_dsp_dump(c, "firmware never answered");
        serial_printf(LOG_ERROR "sof: that was ipc 0x%x, %u bytes\n", hdr->cmd, size);
        c->ipc_dead = true;
        return -1;
    }

    int32_t  err  = (int32_t)dsp_rd(c, SOF_HOSTBOX + 8);
    uint32_t rcmd = dsp_rd(c, SOF_HOSTBOX + 4);
    dsp_wr(c, SOF_HIPCIE, dsp_rd(c, SOF_HIPCIE) | SOF_HIPCIE_DONE);

    if (err < 0)
        serial_printf(LOG_ERROR "sof: ipc 0x%x refused, error %d reply 0x%x\n", hdr->cmd, err, rcmd);
    return err;
}

// firmware telling us something
void sof_ipc_drain(sof_t *c) {
    uint32_t t = dsp_rd(c, SOF_HIPCT);
    if (!(t & SOF_HIPCT_BUSY))
        return;

    if (c->told_ipc < SOF_IPC_LOG_MAX) {
        c->told_ipc++;
        serial_printf(LOG_INFO "sof: firmware says 0x%x ext 0x%x\n", t & ~SOF_HIPCT_BUSY, dsp_rd(c, SOF_HIPCTE));
    }
    dsp_wr(c, SOF_HIPCT, t);
}
