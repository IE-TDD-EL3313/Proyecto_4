// Replay connected RTL publications through the real runtime and capture code.
#include "../../vitis/app/control_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
static unsigned checks;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"chain C FAIL %d: %s\n",__LINE__,#x);exit(1);}++checks;}while(0)
struct fixture {uint32_t now,seq,sample,status,valid,timer;unsigned writes,requests,blocks;};
static struct tdd_runtime rt;
static uint32_t read_adc(void *ctx,uint32_t off){struct fixture *f=ctx;
    if(off==24)return f->now;
    if(off==12)return f->seq;
    if(off==16)return f->sample;
    if(off==8)return 2048;
    return f->valid;}
static uint32_t read_pwm(void *ctx,uint32_t off){(void)off;return ((struct fixture *)ctx)->status;}
static uint32_t read_timer(void *ctx,uint32_t off){(void)off;return ((struct fixture *)ctx)->timer;}
static void write_pwm(void *ctx,uint32_t off,uint32_t value){(void)value;if(off==4)++((struct fixture *)ctx)->writes;}
static void write_timer(void *ctx,uint32_t off,uint32_t value){if(!off)((struct fixture *)ctx)->timer=value&~256u;}
static void block(void *ctx){++((struct fixture *)ctx)->blocks;}
static bool request(void *ctx){++((struct fixture *)ctx)->requests;return true;}
static bool ready(void *ctx){return (((struct fixture *)ctx)->status&1024u)!=0;}
int main(void)
{
    FILE *fp=fopen("build/host/control_chain.csv","r");CHECK(fp);
    char line[256];CHECK(fgets(line,sizeof line,fp));struct fixture f={0};unsigned rows=0;
    while(fgets(line,sizeof line,fp)){
        uint32_t freq,period,k;
        CHECK(sscanf(line,"%"SCNu32",%"SCNu32",%"SCNu32",%"SCNu32",%"SCNu32",%"SCNu32",%"SCNu32",%"SCNu32,
            &freq,&period,&k,&f.now,&f.seq,&f.sample,&f.valid,&f.status)==8);
        CHECK(tdd_pwm_period(freq)==period);
        if(k==0){
            struct tdd_config c={0};c.fs_hz=100000000u%period?0:100000000u/period;
            c.period_ticks=period;c.age_min_ticks=900;c.age_max_ticks=1200;c.warmup_ticks=10*period;
            c.raw_min=100;c.raw_max=3995;c.deadtime_cycles=100;c.offset_code=2048;
            c.gain_a_per_code=.001f;c.vdc=24;c.m_max=.8f;c.amplitude_a=.2f;c.b0=1;
            struct tdd_platform io={.adc={&f,read_adc,NULL},.pwm={&f,read_pwm,write_pwm},
                .timer={&f,read_timer,write_timer},.context=&f,.block_gates=block,
                .request_gates=request,.gates_ready=ready};
            f.writes=0;f.requests=0;
            CHECK(tdd_runtime_prepare(&rt,&io,&c,period,200,100,true));
        }
        f.timer=2u|4u|16u|64u|128u|256u;tdd_runtime_irq(&rt);
        CHECK(!(f.timer&256u));
        CHECK(rt.control.state==(k==0?TDD_WARMUP:k==1?TDD_PRECHARGE:TDD_RUNNING));
        CHECK(f.requests==(k>0?1u:0u));CHECK(f.writes==k);
        if(k>=2)CHECK(rt.capture.bank[rt.capture.active].count==k-1u);
        ++rows;
    }
    CHECK(rows==18);CHECK(!fclose(fp));
    printf("PASS RTL trace to C runtime: %u assertions, three exact PWM periods\n",checks);return 0;
}
