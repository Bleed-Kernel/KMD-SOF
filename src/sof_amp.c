#include "sof_priv.h"
#include <drivers/serial/serial.h>
#include <ACPI/acpi.h>
#include <mm/paging.h>
#include <ansii.h>
#include <string.h>

#define AMP_HID             "MX98357A"
#define AMP_GPIO_DESC       0x8C
#define AMP_GPIO_SEARCH     96
#define AMP_GPIO_PIN_OFF    14      // word in the descriptor saying where its pin list is
#define AMP_GPIO_NAME_OFF   17
#define AMP_GPIO_MAX_LEN    0x40
#define AMP_COMMUNITY       "\\_SB.GPO1"

// north gpio community, the dsdt builds this out of 0xD0000000 | 0xC50000
#define GPO1_BASE           0xD0C50000ULL
#define GPO1_SIZE           0x4000
#define GPO_REVID           0x00
#define GPO_PADBAR          0x0C
#define GPO_REVID_SHIFT     16
#define GPO_WIDE_REV        0x94
#define GPO_PAD_MAX         79

#define PAD_TX_STATE        (1u << 0)
#define PAD_TX_DIS          (1u << 8)
#define PAD_RX_DIS          (1u << 9)
#define PAD_MODE_MASK       (0xFu << 10)

#define AMP_DELAY_MS        5

// coreboot writes the amp out as a device with one gpio, thats the only pin well touch
static bool sof_amp_find(uint32_t *pin) {
    for (size_t t = 0; ; t++) {
        struct acpi_sdt *sdt = acpi_get_sdt(t);
        if (!sdt)
            return false;
        if (memcmp(sdt->signature, "SSDT", 4) && memcmp(sdt->signature, "DSDT", 4))
            continue;

        const uint8_t *d = (const uint8_t *)sdt;
        for (uint32_t i = sizeof(*sdt); i + sizeof(AMP_HID) + AMP_GPIO_SEARCH < sdt->length; i++) {
            if (memcmp(d + i, AMP_HID, sizeof(AMP_HID) - 1))
                continue;

            for (uint32_t g = i; g < i + AMP_GPIO_SEARCH; g++) {
                uint16_t len = (uint16_t)(d[g + 1] | d[g + 2] << 8);
                if (d[g] != AMP_GPIO_DESC || len > AMP_GPIO_MAX_LEN || d[g + 4] != 1)
                    continue;

                uint16_t pins = (uint16_t)(d[g + AMP_GPIO_PIN_OFF] | d[g + AMP_GPIO_PIN_OFF + 1] << 8);
                uint16_t name = (uint16_t)(d[g + AMP_GPIO_NAME_OFF] | d[g + AMP_GPIO_NAME_OFF + 1] << 8);
                if (memcmp(d + g + name, AMP_COMMUNITY, sizeof(AMP_COMMUNITY))) {
                    serial_printf(LOG_WARN "sof: amp gpio isnt on the community we know\n");
                    return false;
                }
                *pin = (uint32_t)(d[g + pins] | d[g + pins + 1] << 8);
                return true;
            }
        }
    }
}

void sof_amp_on(sof_t *c) {
    (void)c;

    uint32_t pin = 0;
    if (!sof_amp_find(&pin) || pin > GPO_PAD_MAX) {
        serial_printf(LOG_WARN "sof: no " AMP_HID " in acpi, leaving the gpios alone (pin %u)\n", pin);
        return;
    }

    if (paging_map_mmio(GPO1_BASE, GPO1_SIZE) < 0) {
        serial_printf(LOG_ERROR "sof: couldnt map the gpio community\n");
        return;
    }

    uint64_t gpio   = (uint64_t)(uintptr_t)paddr_to_vaddr(GPO1_BASE);
    uint32_t rev    = mmio_read32(gpio, GPO_REVID) >> GPO_REVID_SHIFT;
    uint32_t padbar = mmio_read32(gpio, GPO_PADBAR);
    uint32_t stride = rev >= GPO_WIDE_REV ? 16 : 8;
    uint32_t reg    = padbar + pin * stride;
    if (padbar == 0xFFFFFFFF || reg + 4 > GPO1_SIZE) {
        serial_printf(LOG_ERROR "sof: gpio community looks wrong, rev 0x%x padbar 0x%x\n", rev, padbar);
        return;
    }

    uint32_t was = mmio_read32(gpio, reg);

    // this pads default job is a spi chip select, firmware has it as a gpio if its really the amp
    if (was & PAD_MODE_MASK) {
        serial_printf(LOG_WARN "sof: amp pad %u is in native mode 0x%x, leaving it alone\n", pin, was);
        return;
    }

    uint32_t cfg = (was & ~PAD_TX_DIS) | PAD_RX_DIS | PAD_TX_STATE;
    mmio_write32(gpio, reg, cfg);
    hpet_busy_wait_ms(AMP_DELAY_MS);

    serial_printf(LOG_OK "sof: amp on, pin %u rev 0x%x padbar 0x%x, pad 0x%x -> 0x%x\n", pin, rev, padbar, was,
                  mmio_read32(gpio, reg));
}
