#pragma once

// PCI identifiers
#define PCI_CLASS_MULTIMEDIA    0x04
#define PCI_SUBCLASS_AUDIO      0x01
#define SOF_DEVICE_GLK          0x3198

#define SOF_PCI_PGCTL           0x44    // shared with tcsel, low 3 bits are that
#define SOF_PCI_CGCTL           0x48
#define SOF_PGCTL_ADSPPGD       (1u << 2)
#define SOF_PGCTL_TCSEL         0x07u
#define SOF_CGCTL_ADSPDCGE      (1u << 1)
#define SOF_CGCTL_MISCBDCGE     (1u << 6)

// hda side, bar 0
#define SOF_GCAP                0x00
#define SOF_GCTL                0x08
#define SOF_STATESTS            0x0E
#define SOF_LLCH                0x14
#define SOF_EM2                 0x1030
#define SOF_SD_BASE             0x80
#define SOF_SD_SIZE             0x20

#define SOF_GCAP_ISS(g)         (((g) >> 8) & 0xF)
#define SOF_GCAP_OSS(g)         (((g) >> 12) & 0xF)
#define SOF_GCAP_64OK           (1u << 0)
#define SOF_GCTL_CRST           (1u << 0)
#define SOF_EM2_L1SEN           (1u << 13)

#define SOF_SD_CTL              0x00
#define SOF_SD_LPIB             0x04
#define SOF_SD_CBL              0x08
#define SOF_SD_LVI              0x0C
#define SOF_SD_FMT              0x12
#define SOF_SD_BDPL             0x18
#define SOF_SD_BDPU             0x1C

#define SOF_SDCTL_SRST          (1u << 0)
#define SOF_SDCTL_RUN           (1u << 1)
#define SOF_SDCTL_INTS          (0x7u << 2)
#define SOF_SDCTL_TAG_SHIFT     20
#define SOF_SDCTL_TAG_MASK      (0xFu << SOF_SDCTL_TAG_SHIFT)
#define SOF_SDSTS_MASK          0xFF000000u
#define SOF_SDSTS_CLEAR         (0x1Cu << 24)

// capability list off LLCH
#define SOF_CAP_NEXT(c)         ((c) & 0xFFFF)
#define SOF_CAP_ID(c)           (((c) >> 16) & 0xFFF)
#define SOF_CAP_ID_PP           0x3
#define SOF_CAP_ID_SPIB         0x4

#define SOF_PP_PPCTL            0x04
#define SOF_PPCTL_GPROCEN       (1u << 30)

#define SOF_SPIB_CTL            0x04
#define SOF_SPIB_BASE           0x08
#define SOF_SPIB_STEP           0x08

// dsp side, bar 4
#define SOF_ADSPCS              0x04
#define SOF_ADSPCS_CRST(m)      ((uint32_t)(m) << 0)
#define SOF_ADSPCS_CSTALL(m)    ((uint32_t)(m) << 8)
#define SOF_ADSPCS_SPA(m)       ((uint32_t)(m) << 16)
#define SOF_ADSPCS_CPA(m)       ((uint32_t)(m) << 24)

#define SOF_HIPCT               0x40
#define SOF_HIPCTE              0x44
#define SOF_HIPCI               0x48
#define SOF_HIPCIE              0x4C
#define SOF_HIPCT_BUSY          (1u << 31)
#define SOF_HIPCI_BUSY          (1u << 31)
#define SOF_HIPCIE_DONE         (1u << 30)

#define SOF_SSP_BASE            0x2000
#define SOF_SSP_SIZE            0x1000
#define SOF_SSP_COUNT           6
#define SOF_SSP_SSC1            0x04
#define SOF_SSP_CONSUMER        ((1u << 25) | (1u << 24))

#define SOF_ROM_STATUS          0x80000
#define SOF_ROM_ERROR           0x80004
#define SOF_MBOX_UPLINK         0x81000
#define SOF_FSR_STATE(s)        ((s) & 0xFFFFFF)
#define SOF_FSR_MODULE(s)       (((s) >> 28) & 0x7)
#define SOF_FSR_INIT_DONE       0x1
#define SOF_FSR_FW_ENTERED      0x5

#define SOF_ROM_CONTROL         0x01000000u
#define SOF_ROM_PURGE_FW        0x00004000u
#define SOF_ROM_TAG_SHIFT       9

#define SOF_IPC_FW_READY        0x70000000u
#define SOF_IPC_TYPE_MASK       0xF0000000u
