#pragma once

#include <stdint.h>

// sof ipc3 messages, these have to match the firmware byte for byte
#define SOF_GLB_TPLG            (0x3u << 28)
#define SOF_GLB_COMP            (0x5u << 28)
#define SOF_GLB_STREAM          (0x6u << 28)
#define SOF_GLB_DAI             (0x8u << 28)
#define SOF_CMD(x)              ((uint32_t)(x) << 16)

#define SOF_TPLG_COMP_NEW       SOF_CMD(0x001)
#define SOF_TPLG_COMP_CONNECT   SOF_CMD(0x003)
#define SOF_TPLG_PIPE_NEW       SOF_CMD(0x010)
#define SOF_TPLG_PIPE_COMPLETE  SOF_CMD(0x013)
#define SOF_TPLG_BUFFER_NEW     SOF_CMD(0x020)
#define SOF_COMP_SET_VALUE      SOF_CMD(0x001)
#define SOF_DAI_CONFIG          SOF_CMD(0x001)
#define SOF_STREAM_PCM_PARAMS   SOF_CMD(0x001)
#define SOF_STREAM_PCM_FREE     SOF_CMD(0x003)
#define SOF_STREAM_TRIG_START   SOF_CMD(0x004)
#define SOF_STREAM_TRIG_STOP    SOF_CMD(0x005)

#define SOF_COMP_TYPE_HOST      1
#define SOF_COMP_TYPE_DAI       2
#define SOF_COMP_TYPE_VOLUME    5
#define SOF_COMP_TYPE_BUFFER    12
#define SOF_DAI_INTEL_SSP       1
#define SOF_FRAME_S16           0
#define SOF_FRAME_S32           2
#define SOF_TIME_DOMAIN_TIMER   1

#define SOF_DAI_FMT_I2S         1
#define SOF_DAI_FMT_CBC_CFC     (4u << 12)
#define SOF_DAI_FLAG_NONE       0
#define SOF_DAI_FLAG_HW_PARAMS  (1u << 0)
#define SOF_DAI_FLAG_HW_FREE    (1u << 1)
#define SOF_DAI_CONFIG_SIZE     216     // the firmware sizes this by its largest dai

#define SOF_CTRL_CHAN_SET       1
#define SOF_CTRL_CMD_VOLUME     0
#define SOF_MAX_CHANNELS        8
#define SOF_UUID_SIZE           16

typedef struct { uint32_t size; uint32_t cmd; } __attribute__((packed)) sof_cmd_hdr_t;
typedef struct { sof_cmd_hdr_t hdr; int32_t error; } __attribute__((packed)) sof_reply_t;

typedef struct {
    sof_cmd_hdr_t hdr;
    uint32_t id;
    uint32_t type;
    uint32_t pipeline_id;
    uint32_t core;
    uint32_t ext_data_length;
} __attribute__((packed)) sof_comp_t;

typedef struct {
    sof_cmd_hdr_t hdr;
    uint32_t periods_sink;
    uint32_t periods_source;
    uint32_t reserved1;
    uint32_t frame_fmt;
    uint32_t xrun_action;
    uint32_t reserved[2];
} __attribute__((packed)) sof_comp_config_t;

typedef struct {
    sof_comp_t comp;
    sof_comp_config_t config;
    uint32_t direction;
    uint32_t no_irq;
    uint32_t dmac_config;
    uint8_t  uuid[SOF_UUID_SIZE];
} __attribute__((packed)) sof_comp_host_t;

typedef struct {
    sof_comp_t comp;
    sof_comp_config_t config;
    uint32_t direction;
    uint32_t dai_index;
    uint32_t type;
    uint32_t reserved;
    uint8_t  uuid[SOF_UUID_SIZE];
} __attribute__((packed)) sof_comp_dai_t;

typedef struct {
    sof_comp_t comp;
    sof_comp_config_t config;
    uint32_t channels;
    uint32_t min_value;
    uint32_t max_value;
    uint32_t ramp;
    uint32_t initial_ramp;
    uint8_t  uuid[SOF_UUID_SIZE];
} __attribute__((packed)) sof_comp_volume_t;

typedef struct {
    sof_comp_t comp;
    uint32_t size;
    uint32_t caps;
    uint32_t flags;
    uint32_t reserved;
} __attribute__((packed)) sof_buffer_t;

typedef struct {
    sof_cmd_hdr_t hdr;
    uint32_t comp_id;
    uint32_t pipeline_id;
    uint32_t sched_id;
    uint32_t core;
    uint32_t period;
    uint32_t priority;
    uint32_t period_mips;
    uint32_t frames_per_sched;
    uint32_t xrun_limit_usecs;
    uint32_t time_domain;
} __attribute__((packed)) sof_pipe_new_t;

typedef struct { sof_cmd_hdr_t hdr; uint32_t comp_id; } __attribute__((packed)) sof_comp_msg_t;
typedef struct { sof_cmd_hdr_t hdr; uint32_t source_id; uint32_t sink_id; } __attribute__((packed)) sof_connect_t;

typedef struct {
    uint32_t hdr_size;
    uint16_t reserved1;
    uint16_t mclk_id;
    uint32_t mclk_rate;
    uint32_t fsync_rate;
    uint32_t bclk_rate;
    uint32_t tdm_slots;
    uint32_t rx_slots;
    uint32_t tx_slots;
    uint32_t sample_valid_bits;
    uint16_t tdm_slot_width;
    uint16_t reserved2;
    uint32_t mclk_direction;
    uint16_t frame_pulse_width;
    uint16_t tdm_per_slot_padding_flag;
    uint32_t clks_control;
    uint32_t quirks;
    uint32_t bclk_delay;
} __attribute__((packed)) sof_ssp_t;

typedef struct {
    sof_cmd_hdr_t hdr;
    uint32_t type;
    uint32_t dai_index;
    uint16_t format;
    uint8_t  group_id;
    uint8_t  flags;
    uint32_t reserved[8];
    sof_ssp_t ssp;
    uint8_t  pad[SOF_DAI_CONFIG_SIZE - 52 - sizeof(sof_ssp_t)];
} __attribute__((packed)) sof_dai_config_t;

typedef struct {
    uint32_t hdr_size;
    uint32_t phy_addr;
    uint32_t pages;
    uint32_t size;
    uint32_t reserved[3];
} __attribute__((packed)) sof_host_buffer_t;

typedef struct {
    sof_cmd_hdr_t hdr;
    uint32_t comp_id;
    uint32_t flags;
    uint32_t reserved[2];
    uint32_t params_size;
    sof_host_buffer_t buffer;
    uint32_t direction;
    uint32_t frame_fmt;
    uint32_t buffer_fmt;
    uint32_t rate;
    uint16_t stream_tag;
    uint16_t channels;
    uint16_t sample_valid_bytes;
    uint16_t sample_container_bytes;
    uint32_t host_period_bytes;
    uint16_t no_stream_position;
    uint8_t  cont_update_posn;
    uint8_t  reserved0;
    int16_t  ext_data_length;
    uint8_t  reserved3[2];
    uint16_t chmap[SOF_MAX_CHANNELS];
} __attribute__((packed)) sof_pcm_params_t;

typedef struct {
    sof_reply_t rhdr;
    uint32_t comp_id;
    uint32_t type;
    uint32_t cmd;
    uint32_t index;
    sof_host_buffer_t buffer;
    uint32_t num_elems;
    uint32_t elems_remaining;
    uint32_t msg_index;
    uint32_t reserved[6];
    struct { uint32_t channel; uint32_t value; } chanv[2];
} __attribute__((packed)) sof_ctrl_t;
