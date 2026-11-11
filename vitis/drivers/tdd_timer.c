#include "tdd_timer.h"
enum { TCSR=0, TLR=4, DOWN=2, GENERATE=4, RELOAD=16, LOAD=32,
       IRQ_ENABLE=64, ENABLE=128, IRQ_PENDING=256 };
bool tdd_timer_prepare(const struct tdd_bus *bus, uint32_t fs_hz)
{
    if (!bus || !bus->write32 || !fs_hz || 100000000u%fs_hz || 100000000u/fs_hz<3u)
        return false;
    uint32_t options=DOWN|GENERATE|RELOAD;
    bus->write32(bus->context,TCSR,0);
    bus->write32(bus->context,0x10u,0); // auxiliary counter IRQ disabled
    bus->write32(bus->context,TLR,100000000u/fs_hz-2u);
    bus->write32(bus->context,TCSR,options|LOAD|IRQ_PENDING);
    bus->write32(bus->context,TCSR,options);
    return true;
}
void tdd_timer_start(const struct tdd_bus *bus)
{
    if (bus && bus->write32)
        bus->write32(bus->context,TCSR,DOWN|GENERATE|RELOAD|IRQ_ENABLE|ENABLE|IRQ_PENDING);
}
bool tdd_timer_ack(const struct tdd_bus *bus)
{
    if (!bus || !bus->read32 || !bus->write32) return false;
    uint32_t status=bus->read32(bus->context,TCSR);
    if ((status&(IRQ_ENABLE|IRQ_PENDING))!=(IRQ_ENABLE|IRQ_PENDING)) return false;
    // Preserve configuration; acknowledge once, before control, without reloading.
    bus->write32(bus->context,TCSR,status|IRQ_PENDING);
    return true;
}
