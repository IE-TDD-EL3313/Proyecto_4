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
uint32_t tdd_pwm_period(uint32_t frequency_code)
{
    static const uint32_t periods[4]={10000u,6666u,5000u,5000u};
    return frequency_code<4u ? periods[frequency_code] : 0u;
}
bool tdd_pwm_configure(const struct tdd_bus *bus, uint32_t frequency_code,
                       uint32_t deadtime, uint32_t phase)
{
    uint32_t period=tdd_pwm_period(frequency_code);
    if (!bus || !bus->read32 || !bus->write32 || !period || phase>=period/2u ||
        deadtime<20u || deadtime>1023u) return false;
    tdd_pwm_disable(bus,frequency_code);
    if (bus->read32(bus->context,0)&(1u|8u|32u)) return false;
    bus->write32(bus->context,4,0); /* safe before carrier and bootstrap */
    bus->write32(bus->context,8,deadtime);
    bus->write32(bus->context,12,phase);
    return bus->read32(bus->context,4)==0 && bus->read32(bus->context,8)==deadtime &&
           bus->read32(bus->context,12)==phase;
}
bool tdd_pwm_arm(const struct tdd_bus *bus, uint32_t frequency_code)
{
    if (!bus || !bus->read32 || !bus->write32 || !tdd_pwm_period(frequency_code) ||
        (bus->read32(bus->context,0)&(1u|32u|64u|256u))) return false;
    bus->write32(bus->context,0,129u|(frequency_code<<1));
    return (bus->read32(bus->context,0)&(1u|16u|64u|128u))==(1u|128u);
}
void tdd_pwm_block(const struct tdd_bus *bus)
{
    if (bus && bus->read32 && bus->write32)
        bus->write32(bus->context,0,bus->read32(bus->context,0)&7u);
}
bool tdd_pwm_request_gates(const struct tdd_bus *bus)
{
    if (!bus || !bus->read32 || !bus->write32) return false;
    uint32_t status=bus->read32(bus->context,0);
    if ((status&(1u|64u|256u))!=(1u|256u)) return false;
    bus->write32(bus->context,0,(status&7u)|16u);
    return true;
}
bool tdd_pwm_ready(const struct tdd_bus *bus)
{
    if (!bus || !bus->read32) return false;
    uint32_t status=bus->read32(bus->context,0);
    return (status&(1u|16u|32u|64u|256u|512u|1024u))==(1u|16u|32u|256u|1024u);
}

/* VALID means at least one enabled valley latched the shadow reference. */
bool tdd_pwm_read_latched(const struct tdd_bus *bus, int16_t *duty)
{
    if (!bus || !bus->read32 || !duty) return false;
    uint32_t word=bus->read32(bus->context,0x10u);
    int32_t value=(int32_t)(word&0xffffu);
    if (value&0x8000) value-=65536;
    if ((word&0xffff0000u)!=0x10000u || value < -31129 || value > 31129) return false;
    *duty=(int16_t)value;
    return true;
}
