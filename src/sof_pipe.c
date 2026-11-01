#include "sof_priv.h"
#include "sof_ipc.h"
#include <mm/paging.h>
#include <drivers/serial/serial.h>
#include <ansii.h>
#include <string.h>

// everything here comes out of sof-glk-da7219.tplg, pipeline 1: PCM0P BUF1.0 PGA1.0 BUF1.1 SSP1.OUT
static const uint8_t uuid_host[SOF_UUID_SIZE] = { 0x0c, 0x10, 0x9d, 0x8b, 0x78, 0x6d, 0x8f, 0x41,
                                                  0x90, 0xa3, 0xe0, 0xe8, 0x05, 0xd0, 0x85, 0x2b };
static const uint8_t uuid_vol[SOF_UUID_SIZE]  = { 0x7e, 0x67, 0x7e, 0xb7, 0xf4, 0x5f, 0x88, 0x41,
                                                  0xaf, 0x14, 0xfb, 0xa8, 0xbd, 0xbf, 0x86, 0x82 };
static const uint8_t uuid_dai[SOF_UUID_SIZE]  = { 0x27, 0x0d, 0xb0, 0xc2, 0xbc, 0xff, 0x50, 0x41,
                                                  0xa5, 0x1a, 0x24, 0x5c, 0x79, 0xc5, 0xe5, 0x4b };

#define PIPE_ID         1
#define COMP_PIPE       1
#define COMP_HOST       2
#define COMP_BUF0       3
#define COMP_VOL        4
#define COMP_BUF1       5
#define COMP_DAI        6

#define BUF_SIZE        0x300
#define BUF_CAPS        0x71
#define PIPE_PERIOD_US  1000
#define PIPE_MIPS       5000
#define VOL_RAMP        2
#define VOL_RAMP_MS     20
#define VOL_UNITY       0x10000     // 0dB in the q16 the volume component works in

#define SSP_INDEX       1
#define SSP_MCLK_ID     1
#define SSP_MCLK        19200000
#define SSP_BCLK        1536000
#define SSP_FSYNC       48000
#define SSP_SLOTS       2
#define SSP_SLOT_WIDTH  16
#define SSP_SLOT_MASK   3
#define SSP_MCLK_DIR    1

static void comp_head(sof_comp_t *comp, uint32_t id, uint32_t type, uint32_t cmd) {
    comp->hdr.cmd     = cmd;
    comp->id          = id;
    comp->type        = type;
    comp->pipeline_id = PIPE_ID;
}

static void comp_uuid(sof_comp_t *comp) {
    comp->hdr.cmd         = SOF_GLB_TPLG | SOF_TPLG_COMP_NEW;
    comp->ext_data_length = SOF_UUID_SIZE;
}

static int pipe_new(sof_t *c) {
    sof_pipe_new_t m;
    memset(&m, 0, sizeof(m));
    m.hdr.cmd     = SOF_GLB_TPLG | SOF_TPLG_PIPE_NEW;
    m.comp_id     = COMP_PIPE;
    m.pipeline_id = PIPE_ID;
    m.sched_id    = COMP_DAI;
    m.period      = PIPE_PERIOD_US;
    m.period_mips = PIPE_MIPS;
    m.time_domain = SOF_TIME_DOMAIN_TIMER;
    return sof_ipc_send(c, &m, sizeof(m));
}

static int comp_host(sof_t *c) {
    sof_comp_host_t m;
    memset(&m, 0, sizeof(m));
    comp_head(&m.comp, COMP_HOST, SOF_COMP_TYPE_HOST, 0);
    comp_uuid(&m.comp);
    m.config.hdr.size     = sizeof(m.config);
    m.config.periods_sink = 2;
    m.config.frame_fmt    = SOF_FRAME_S16;
    memcpy(m.uuid, uuid_host, SOF_UUID_SIZE);
    return sof_ipc_send(c, &m, sizeof(m));
}

static int comp_buffer(sof_t *c, uint32_t id) {
    sof_buffer_t m;
    memset(&m, 0, sizeof(m));
    comp_head(&m.comp, id, SOF_COMP_TYPE_BUFFER, SOF_GLB_TPLG | SOF_TPLG_BUFFER_NEW);
    m.size = BUF_SIZE;
    m.caps = BUF_CAPS;
    return sof_ipc_send(c, &m, sizeof(m));
}

static int comp_volume(sof_t *c) {
    sof_comp_volume_t m;
    memset(&m, 0, sizeof(m));
    comp_head(&m.comp, COMP_VOL, SOF_COMP_TYPE_VOLUME, 0);
    comp_uuid(&m.comp);
    m.config.hdr.size       = sizeof(m.config);
    m.config.periods_sink   = 2;
    m.config.periods_source = 2;
    m.config.frame_fmt      = SOF_FRAME_S32;
    m.channels              = 2;
    m.max_value             = VOL_UNITY;
    m.ramp                  = VOL_RAMP;
    m.initial_ramp          = VOL_RAMP_MS;
    memcpy(m.uuid, uuid_vol, SOF_UUID_SIZE);
    return sof_ipc_send(c, &m, sizeof(m));
}

static int comp_dai(sof_t *c) {
    sof_comp_dai_t m;
    memset(&m, 0, sizeof(m));
    comp_head(&m.comp, COMP_DAI, SOF_COMP_TYPE_DAI, 0);
    comp_uuid(&m.comp);
    m.config.hdr.size       = sizeof(m.config);
    m.config.periods_source = 2;
    m.config.frame_fmt      = SOF_FRAME_S16;
    m.dai_index             = SSP_INDEX;
    m.type                  = SOF_DAI_INTEL_SSP;
    memcpy(m.uuid, uuid_dai, SOF_UUID_SIZE);
    return sof_ipc_send(c, &m, sizeof(m));
}

static int comp_connect(sof_t *c, uint32_t source, uint32_t sink) {
    sof_connect_t m;
    memset(&m, 0, sizeof(m));
    m.hdr.cmd   = SOF_GLB_TPLG | SOF_TPLG_COMP_CONNECT;
    m.source_id = source;
    m.sink_id   = sink;
    return sof_ipc_send(c, &m, sizeof(m));
}

static int dai_config(sof_t *c, uint8_t flags) {
    sof_dai_config_t m;
    memset(&m, 0, sizeof(m));
    m.hdr.cmd   = SOF_GLB_DAI | SOF_DAI_CONFIG;
    m.type      = SOF_DAI_INTEL_SSP;
    m.dai_index = SSP_INDEX;
    m.format    = SOF_DAI_FMT_I2S | SOF_DAI_FMT_CBC_CFC;
    m.flags     = flags;

    m.ssp.mclk_id           = SSP_MCLK_ID;
    m.ssp.mclk_rate         = SSP_MCLK;
    m.ssp.fsync_rate        = SSP_FSYNC;
    m.ssp.bclk_rate         = SSP_BCLK;
    m.ssp.tdm_slots         = SSP_SLOTS;
    m.ssp.rx_slots          = SSP_SLOT_MASK;
    m.ssp.tx_slots          = SSP_SLOT_MASK;
    m.ssp.sample_valid_bits = SSP_SLOT_WIDTH;
    m.ssp.tdm_slot_width    = SSP_SLOT_WIDTH;
    m.ssp.mclk_direction    = SSP_MCLK_DIR;
    return sof_ipc_send(c, &m, sizeof(m));
}

#define STEP(what, call)                                        \
    do {                                                        \
        int r_ = (call);                                        \
        serial_printf(LOG_INFO "sof: %s -> %d\n", what, r_);    \
        if (r_ < 0)                                             \
            return false;                                       \
    } while (0)

bool sof_pipe_build(sof_t *c) {
    STEP("pipeline new", pipe_new(c));
    STEP("host comp", comp_host(c));
    STEP("buffer 0", comp_buffer(c, COMP_BUF0));
    STEP("volume comp", comp_volume(c));
    STEP("buffer 1", comp_buffer(c, COMP_BUF1));
    STEP("ssp comp", comp_dai(c));
    STEP("ssp config", dai_config(c, SOF_DAI_FLAG_NONE));
    STEP("host -> buf0", comp_connect(c, COMP_HOST, COMP_BUF0));
    STEP("buf0 -> vol", comp_connect(c, COMP_BUF0, COMP_VOL));
    STEP("vol -> buf1", comp_connect(c, COMP_VOL, COMP_BUF1));
    STEP("buf1 -> ssp", comp_connect(c, COMP_BUF1, COMP_DAI));

    sof_comp_msg_t done;
    memset(&done, 0, sizeof(done));
    done.hdr.cmd = SOF_GLB_TPLG | SOF_TPLG_PIPE_COMPLETE;
    done.comp_id = COMP_PIPE;
    STEP("pipeline complete", sof_ipc_send(c, &done, sizeof(done)));

    STEP("volume", sof_pipe_volume(c, VOL_UNITY, VOL_UNITY));
    return true;
}

int sof_pipe_volume(sof_t *c, uint32_t left, uint32_t right) {
    sof_ctrl_t m;
    memset(&m, 0, sizeof(m));
    m.rhdr.hdr.cmd     = SOF_GLB_COMP | SOF_COMP_SET_VALUE;
    m.comp_id          = COMP_VOL;
    m.type             = SOF_CTRL_CHAN_SET;
    m.cmd              = SOF_CTRL_CMD_VOLUME;
    m.num_elems        = 2;
    m.chanv[0].channel = 0;
    m.chanv[0].value   = left;
    m.chanv[1].channel = 1;
    m.chanv[1].value   = right;
    return sof_ipc_send(c, &m, sizeof(m));
}

static int stream_cmd(sof_t *c, uint32_t cmd) {
    sof_comp_msg_t m;
    memset(&m, 0, sizeof(m));
    m.hdr.cmd = SOF_GLB_STREAM | cmd;
    m.comp_id = COMP_HOST;
    return sof_ipc_send(c, &m, sizeof(m));
}

int sof_pipe_params(sof_t *c) {
    int r = dai_config(c, SOF_DAI_FLAG_HW_PARAMS);
    if (r < 0)
        return r;

    sof_pcm_params_t m;
    memset(&m, 0, sizeof(m));
    m.hdr.cmd                = SOF_GLB_STREAM | SOF_STREAM_PCM_PARAMS;
    m.comp_id                = COMP_HOST;
    m.params_size            = sizeof(m) - offsetof(sof_pcm_params_t, params_size);
    m.buffer.hdr_size        = sizeof(m.buffer);
    m.buffer.phy_addr        = (uint32_t)c->table_phys;
    m.buffer.pages           = SOF_RING_BYTES / PAGE_SIZE;
    m.buffer.size            = SOF_RING_BYTES;
    m.frame_fmt              = SOF_FRAME_S16;
    m.rate                   = SOF_RATE;
    m.stream_tag             = SOF_LOADER_TAG;
    m.channels               = 2;
    m.sample_valid_bytes     = 2;
    m.sample_container_bytes = 2;
    m.host_period_bytes      = SOF_PERIOD_BYTES;
    m.no_stream_position     = 1;
    return sof_ipc_send(c, &m, sizeof(m));
}

int sof_pipe_start(sof_t *c) {
    return stream_cmd(c, SOF_STREAM_TRIG_START);
}

void sof_pipe_stop(sof_t *c) {
    stream_cmd(c, SOF_STREAM_TRIG_STOP);
}

void sof_pipe_free(sof_t *c) {
    stream_cmd(c, SOF_STREAM_PCM_FREE);
    dai_config(c, SOF_DAI_FLAG_HW_FREE);
}
