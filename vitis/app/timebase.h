#ifndef TDD_TIMEBASE_H
#define TDD_TIMEBASE_H
#include <stdbool.h>
#include <stdint.h>
#define TDD_CLOCK_HZ UINT32_C(100000000)
/* period is authoritative. fs=0 selects an exact rational sample frequency. */
static inline bool tdd_timebase_valid(uint32_t fs, uint32_t period)
{
    return period >= 3u && period <= TDD_CLOCK_HZ/100u &&
        (!fs || (uint64_t)fs*period == TDD_CLOCK_HZ);
}
#endif
