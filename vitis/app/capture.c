#include "capture.h"
#include <float.h>
#include <math.h>
#include <string.h>
_Static_assert(sizeof(struct tdd_record)==4,"record layout must be four bytes");
_Static_assert(sizeof(float)==4 && FLT_RADIX==2 && FLT_MANT_DIG==24 && FLT_MAX_EXP==128,
               "protocol v1 requires IEEE-754 binary32");

uint32_t tdd_crc32_update(uint32_t crc, uint8_t byte)
{
    crc ^= byte;
    for (unsigned bit=0;bit<8u;++bit)
        crc=(crc>>1)^((crc & 1u) ? UINT32_C(0xedb88320) : 0u);
    return crc;
}
bool tdd_capture_init(struct tdd_capture *c, uint32_t fs, uint32_t period,
                      float offset, float gain)
{
    if (!c || (uint64_t)fs*period != UINT64_C(100000000) || !fs || !period ||
        !isfinite(offset) || !isfinite(gain) || gain==0.0f) return false;
    memset(c,0,sizeof *c);
    atomic_init(&c->request,0); atomic_init(&c->ready,0); atomic_init(&c->release,0);
    c->config.fs_hz=fs; c->config.period_ticks=period;
    c->config.offset_code=offset; c->config.gain_a_per_code=gain;
    c->next_id=1;
    return true;
}
bool tdd_capture_request(struct tdd_capture *c)
{
    if (!c || atomic_load_explicit(&c->ready,memory_order_acquire) ||
        atomic_load_explicit(&c->request,memory_order_acquire)) return false;
    atomic_store_explicit(&c->request,1,memory_order_release);
    return true;
}
void tdd_capture_service(struct tdd_capture *c)
{
    if (atomic_load_explicit(&c->release,memory_order_acquire)) {
        atomic_store_explicit(&c->release,0,memory_order_relaxed);
        atomic_store_explicit(&c->ready,0,memory_order_release);
    }
    struct tdd_bank *b=&c->bank[c->active];
    if (!atomic_load_explicit(&c->request,memory_order_acquire) ||
        atomic_load_explicit(&c->ready,memory_order_acquire) || !b->count) return;
    b->frozen=c->config;
    b->frozen.capture_id=c->next_id++;
    b->frozen.count=b->count;
    b->frozen.first_seq=b->last_seq-(b->count-1u);
    b->frozen.first_ticks=b->last_ticks-(b->count-1u)*c->config.period_ticks;
    b->frozen.flags=b->flags;
    if ((uint64_t)(b->count-1u)*c->config.period_ticks < UINT64_C(10000000))
        b->frozen.flags |= TDD_CAPTURE_SHORT;
    unsigned frozen=c->active;
    c->active ^= 1u;
    // Clear only metadata: no O(N) copy or array clear in the ISR.
    b=&c->bank[c->active]; b->head=0; b->count=0; b->flags=0;
    atomic_store_explicit(&c->ready,frozen+1u,memory_order_release);
    atomic_store_explicit(&c->request,0,memory_order_release);
}
void tdd_capture_fault(struct tdd_capture *c, bool adc_error)
{
    c->stopped=true;
    c->bank[c->active].flags |= TDD_CAPTURE_FAULT |
        (adc_error ? TDD_CAPTURE_ADC_ERROR : 0u);
    tdd_capture_service(c);
}
bool tdd_capture_push(struct tdd_capture *c, uint16_t raw, float reference_a,
                      uint32_t seq, uint32_t ticks)
{
    if (!c || c->stopped) return false;
    struct tdd_bank *b=&c->bank[c->active];
    if (raw>4095u || !isfinite(reference_a) || fabsf(reference_a)>32.767f) {
        tdd_capture_fault(c,true); return false;
    }
    if (b->count && (seq != b->last_seq+1u || ticks-b->last_ticks != c->config.period_ticks)) {
        tdd_capture_fault(c,true); return false; // close before discontinuous sample
    }
    b->records[b->head].raw=raw;
    b->records[b->head].reference_ma=(int16_t)roundf(reference_a*1000.0f);
    b->head=(b->head+1u)%TDD_CAPTURE_CAPACITY;
    if (b->count<TDD_CAPTURE_CAPACITY) ++b->count;
    b->last_seq=seq; b->last_ticks=ticks;
    tdd_capture_service(c);
    return true;
}
static void put16(uint8_t *b,uint16_t x) { b[0]=(uint8_t)x; b[1]=(uint8_t)(x>>8); }
static void put32(uint8_t *b,uint32_t x)
{
    for (unsigned i=0;i<4u;++i) b[i]=(uint8_t)(x>>(8u*i));
}
static void put_float(uint8_t *b,float x)
{
    uint32_t bits; memcpy(&bits,&x,4); put32(b,bits);
}
bool tdd_tx_begin(struct tdd_capture *c, struct tdd_transmitter *tx)
{
    if (!c || !tx || tx->active || atomic_load_explicit(&c->release,memory_order_acquire)) return false;
    unsigned ready=atomic_load_explicit(&c->ready,memory_order_acquire);
    if (!ready) return false;
    tx->bank=ready-1u;
    const struct tdd_metadata *m=&c->bank[tx->bank].frozen;
    memset(tx->header,0,sizeof tx->header);
    memcpy(tx->header,"TDD4",4); tx->header[4]=1; tx->header[5]=1;
    put16(tx->header+6,48); put32(tx->header+8,m->capture_id);
    put32(tx->header+12,m->count); put32(tx->header+16,m->fs_hz);
    put32(tx->header+20,m->first_seq); put32(tx->header+24,m->first_ticks);
    put_float(tx->header+28,m->offset_code); put_float(tx->header+32,m->gain_a_per_code);
    put16(tx->header+36,4); put16(tx->header+38,m->flags);
    put32(tx->header+40,m->period_ticks); put32(tx->header+44,4u*m->count);
    tx->position=0; tx->total=48u+4u*m->count+4u;
    tx->crc=UINT32_MAX; tx->active=true;
    return true;
}
size_t tdd_tx_read(struct tdd_capture *c, struct tdd_transmitter *tx,
                    uint8_t *output, size_t capacity)
{
    if (!c || !tx || !tx->active || !output) return 0;
    const struct tdd_bank *b=&c->bank[tx->bank];
    size_t n=0;
    while (n<capacity && tx->position<tx->total) {
        uint32_t p=tx->position;
        uint8_t byte;
        if (p<48u) byte=tx->header[p];
        else if (p<tx->total-4u) {
            uint32_t record=(p-48u)/4u;
            uint32_t oldest=(b->head+TDD_CAPTURE_CAPACITY-b->count)%TDD_CAPTURE_CAPACITY;
            const struct tdd_record *r=&b->records[(oldest+record)%TDD_CAPTURE_CAPACITY];
            uint32_t word=(uint32_t)r->raw|((uint32_t)(uint16_t)r->reference_ma<<16);
            byte=(uint8_t)(word>>(8u*((p-48u)%4u)));
        } else byte=(uint8_t)((tx->crc^UINT32_MAX)>>(8u*(p-(tx->total-4u))));
        if (p<tx->total-4u) tx->crc=tdd_crc32_update(tx->crc,byte);
        output[n++]=byte; ++tx->position;
    }
    if (tx->position==tx->total) {
        tx->active=false;
        atomic_store_explicit(&c->release,1,memory_order_release);
    }
    return n;
}
