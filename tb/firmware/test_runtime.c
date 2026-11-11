#include "../../vitis/app/control_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr,"runtime FAIL line %d: %s\n",__LINE__,#x); exit(1); } ++checks; } while (0)
struct fixture {
    uint32_t now, seq, sample_ticks, timer_status, tlr;
    unsigned blocks, releases, duty_writes, timer_acks, tick_reads, jump_on_tick;
    uint32_t write_cost;
    bool marked;
};
static struct tdd_runtime runtime;
static uint32_t read_adc(void *ctx,uint32_t offset)
{
    struct fixture *f=ctx;
    if (offset==24) {
        if (++f->tick_reads==f->jump_on_tick) f->now=4800;
        return f->now;
    }
    REQUIRE(!(f->timer_status&256u)); // timer acknowledged before reading samples
    if (offset==12) return f->seq;
    if (offset==8) return 2048;
    if (offset==16) return f->sample_ticks;
    return 1;
}
static uint32_t read_timer(void *ctx,uint32_t offset)
{
    struct fixture *f=ctx; REQUIRE(offset==0); return f->timer_status;
}
static void write_timer(void *ctx,uint32_t offset,uint32_t value)
{
    struct fixture *f=ctx;
    if (offset==0) {
        if (value&256u) ++f->timer_acks;
        f->timer_status=value&~256u;
    } else if (offset==4) f->tlr=value;
    else REQUIRE(offset==16 && value==0);
}
static void write_pwm(void *ctx,uint32_t offset,uint32_t value)
{
    struct fixture *f=ctx;
    if (offset==4) {
        REQUIRE(f->marked); REQUIRE((value&0xffff0000u)==0);
        ++f->duty_writes; f->now+=f->write_cost;
    } else REQUIRE(offset==0 && !(value&1u));
}
static void block(void *ctx) { ++((struct fixture *)ctx)->blocks; }
static bool release(void *ctx) { ++((struct fixture *)ctx)->releases; return true; }
static void mark(void *ctx,bool high) { ((struct fixture *)ctx)->marked=high; }
static struct tdd_config config(void)
{
    struct tdd_config c={0}; c.fs_hz=20000;c.period_ticks=5000;c.age_min_ticks=900;
    c.age_max_ticks=1200;c.warmup_ticks=50000;c.raw_min=100;c.raw_max=3995;
    c.deadtime_cycles=100;c.offset_code=2048;c.gain_a_per_code=.001f;
    c.vdc=12;c.m_max=.8f;c.amplitude_a=.2f;c.b0=1; return c;
}
static void prepare(struct fixture *f)
{
    memset(f,0,sizeof *f);
    struct tdd_platform io={.adc={f,read_adc,NULL},.pwm={f,NULL,write_pwm},
        .timer={f,read_timer,write_timer},.context=f,.block_gates=block,
        .release_at_valley=release,.mark_isr=mark};
    struct tdd_config c=config();
    REQUIRE(!tdd_runtime_prepare(&runtime,&io,&c,5000,200,100,false));
    REQUIRE(f->blocks==1 && !runtime.prepared);
    tdd_runtime_irq(&runtime); REQUIRE(f->duty_writes==0);
    REQUIRE(tdd_runtime_prepare(&runtime,&io,&c,5000,200,100,true));
    REQUIRE(f->tlr==4998 && !(f->timer_status&128u));
    tdd_timer_start(&io.timer);
    f->timer_status|=256u; f->now=1000;f->tick_reads=0;
}
int main(void)
{
    struct fixture f; prepare(&f);
    tdd_runtime_irq(&runtime);
    REQUIRE(runtime.control.state==TDD_RUNNING && f.duty_writes==1 && f.releases==1 && !f.marked);
    REQUIRE(runtime.capture.bank[0].count==1);
    f.seq=1;f.sample_ticks=5000;f.now=6000;f.timer_status|=256;
    tdd_runtime_irq(&runtime);
    REQUIRE(f.duty_writes==2 && f.releases==1);
    f.timer_status|=256; tdd_runtime_irq(&runtime); // repeated sample
    REQUIRE(runtime.control.state==TDD_FAULT && f.duty_writes==2 && f.blocks>=3);
    prepare(&f);f.jump_on_tick=3;tdd_runtime_irq(&runtime);
    REQUIRE(runtime.control.fault==TDD_FAULT_TIME && f.duty_writes==0 && f.releases==0);
    prepare(&f);f.write_cost=4000;tdd_runtime_irq(&runtime);
    REQUIRE(runtime.control.fault==TDD_FAULT_TIME && f.duty_writes==1 && f.releases==0);
    REQUIRE(runtime.capture.stopped);
    printf("PASS runtime: %u assertions (timer ack, gate block, pre/post deadlines)\n",checks);
    return 0;
}
