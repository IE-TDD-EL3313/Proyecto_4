#include "../../vitis/app/control_core.h"
#include "../../vitis/app/capture.h"
#include "../../vitis/drivers/tdd_timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { if (!(x)) {fprintf(stderr,"timebase FAIL %d: %s\n",__LINE__,#x);exit(1);}++checks;} while(0)
static uint32_t regs[8],pwm[4];
static uint32_t read_pwm(void *ctx,uint32_t off) { (void)ctx;return pwm[off/4u]; }
static void write_pwm(void *ctx,uint32_t off,uint32_t value)
{
    (void)ctx;
    if(off){pwm[off/4u]=value;return;}
    pwm[0]=(pwm[0]&~151u)|(value&151u);
    if(value&1u)pwm[0]|=8u;
    else pwm[0]&=151u;
    if(!(value&16u))pwm[0]&=~(32u|512u|1024u);
}
static void write_timer(void *ctx,uint32_t off,uint32_t value) {(void)ctx;regs[off/4u]=value;}
static struct tdd_capture capture;
int main(int argc,char **argv)
{
    const uint32_t periods[]={10000,6666,5000,5000};
    struct tdd_bus timer={NULL,NULL,write_timer},bus={NULL,read_pwm,write_pwm};
    for(unsigned i=0;i<4;++i){
        uint32_t p=periods[i];
        struct tdd_config c={0};c.fs_hz=(100000000u%p)?0u:100000000u/p;
        c.period_ticks=p;c.age_min_ticks=900;c.age_max_ticks=1200;c.warmup_ticks=p*10u;
        c.raw_min=100;c.raw_max=3995;c.deadtime_cycles=100;
        c.offset_code=2048;c.gain_a_per_code=.001f;c.vdc=24;c.m_max=.8f;c.b0=1;
        struct tdd_control ctl;struct tdd_output out;
        CHECK(tdd_control_init(&ctl,&c,0));CHECK(tdd_pwm_period(i)==p);
        CHECK(tdd_timer_prepare_ticks(&timer,p));CHECK(regs[1]==p-2u);
        CHECK(tdd_pwm_configure(&bus,i,100,1000));CHECK(tdd_pwm_arm(&bus,i));
        CHECK(!tdd_pwm_request_gates(&bus)); /* not synchronized */
        pwm[0]|=256u;CHECK(tdd_pwm_request_gates(&bus));
        pwm[0]|=32u|512u;CHECK(!tdd_pwm_ready(&bus));
        pwm[0]=(pwm[0]&~512u)|1024u;CHECK(tdd_pwm_ready(&bus));
        tdd_pwm_block(&bus);CHECK(!(pwm[0]&16u));
        struct tdd_sample s={.status=1,.raw=2048};
        CHECK(tdd_control_step(&ctl,&s,1000,&out));
        for(uint32_t k=1;k<=10000;++k){
            s.seq=k;s.ticks=k*p;
            CHECK(tdd_control_step(&ctl,&s,s.ticks+1000,&out));
        }
        uint64_t numerator=(UINT64_C(50)<<32)*p;
        uint64_t ideal=(numerator/100000000u)*10000u+
            ((numerator%100000000u)*10000u+50000000u)/100000000u;
        int32_t error=(int32_t)(ctl.phase-(uint32_t)ideal);
        CHECK(error>=-5000 && error<=5000); /* <=0.5 phase LSB per sample */
    }
    CHECK(!tdd_pwm_configure(&bus,4,100,1000));
    CHECK(!tdd_pwm_configure(&bus,2,100,2500));
    CHECK(!tdd_timer_prepare(&timer,15000)); /* use exact ticks instead */
    CHECK(tdd_capture_init(&capture,0,6666,2048,1.f/1024));
    for(uint32_t k=0;k<2048;++k)CHECK(tdd_capture_push(&capture,2048,.2f,k,k*6666));
    CHECK(tdd_capture_request(&capture));tdd_capture_service(&capture);
    struct tdd_transmitter tx={0};CHECK(tdd_tx_begin(&capture,&tx));
    CHECK(tx.header[4]==2);CHECK(!(capture.bank[tx.bank].frozen.flags&1u));
    FILE *file=argc>1?fopen(argv[1],"wb"):NULL;
    CHECK(argc<2 || file);
    uint8_t bytes[7];size_t total=0;
    while(tx.active){size_t n=tdd_tx_read(&capture,&tx,bytes,sizeof bytes);CHECK(n>0);
        if(file){CHECK(fwrite(bytes,1,n,file)==n);}
        total+=n;
    }
    if(file){CHECK(!fclose(file));}
    CHECK(total==8244);
    puts("PASS: exact periods 10000/6666/5000, PWM driver, phase and v2 frame");
    printf("timebase: %u assertions\n",checks);return 0;
}
