#include "tdd_mmio.h"
#include <stddef.h>
enum tdd_adc_result tdd_adc_read(const struct tdd_bus *bus, struct tdd_sample *out)
{
    if (!bus || !bus->read32 || !out) return TDD_ADC_ERROR;
    for (unsigned attempt=0;attempt<2u;++attempt) {
        uint32_t seq1=bus->read32(bus->context,0x0cu);
        uint32_t raw=bus->read32(bus->context,0x08u);
        uint32_t time=bus->read32(bus->context,0x10u);
        uint32_t status=bus->read32(bus->context,0x04u);
        uint32_t seq2=bus->read32(bus->context,0x0cu);
        if (seq1 != seq2) continue;
        if (status & 12u || raw > 4095u) return TDD_ADC_ERROR;
        if (!(status & 1u)) return TDD_ADC_NOT_READY;
        out->seq=seq2; out->ticks=time; out->status=status; out->raw=(uint16_t)raw;
        return TDD_ADC_OK;
    }
    return TDD_ADC_INCOHERENT;
}
void tdd_pwm_disable(const struct tdd_bus *bus, uint32_t frequency_code)
{
    if (bus && bus->write32) bus->write32(bus->context,0,(frequency_code & 3u)<<1);
}
bool tdd_pwm_write_duty(const struct tdd_bus *bus, int16_t duty)
{
    if (!bus || !bus->write32 || duty < -31129 || duty > 31129) return false;
    bus->write32(bus->context,4,(uint32_t)(uint16_t)duty);
    return true;
}
