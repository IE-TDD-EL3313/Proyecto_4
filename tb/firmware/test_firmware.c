#include "../../vitis/app/control_core.h"
#include "../../vitis/app/capture.h"
#include "../../vitis/drivers/tdd_mmio.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct tdd_capture capture;
static unsigned assertions;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } ++assertions; } while (0)
static struct tdd_config configuration(void)
{
    struct tdd_config cfg={0};
    cfg.fs_hz=20000; cfg.period_ticks=5000; cfg.age_min_ticks=900;
    cfg.age_max_ticks=1200; cfg.warmup_ticks=50000;
    cfg.raw_min=100; cfg.raw_max=3995; cfg.deadtime_cycles=100;
    cfg.offset_code=2048; cfg.gain_a_per_code=1.0f/1024.0f;
    cfg.vdc=12; cfg.m_max=.8f; cfg.amplitude_a=.2f;
    cfg.b0=10.09415323443f; cfg.b1=-19.97870442720f;
    cfg.b2=9.88701611868f; cfg.a1=-1.99787044272f; cfg.a2=.99811693531f;
    return cfg;
}
static void test_numeric(void)
{
    struct tdd_config cfg=configuration(); CHECK(tdd_config_valid(&cfg));
    cfg.m_max=NAN; CHECK(!tdd_config_valid(&cfg)); cfg=configuration();
    cfg.fs_hz=40000; cfg.period_ticks=2500; cfg.m_max=.94f;
    CHECK(!tdd_config_valid(&cfg)); cfg.m_max=.8f; CHECK(tdd_config_valid(&cfg));
    cfg=configuration(); cfg.gain_a_per_code=0; CHECK(!tdd_config_valid(&cfg));
    cfg=configuration(); cfg.a2=1.1f; CHECK(!tdd_config_valid(&cfg));
    cfg=configuration(); cfg.period_ticks=4998; CHECK(!tdd_config_valid(&cfg));
    int16_t q=0; bool saturated=false;
    CHECK(tdd_to_q15(1.0f/65536,1,.8f,&q,&saturated) && q==1 && !saturated);
    CHECK(tdd_to_q15(-1.0f/65536,1,.8f,&q,&saturated) && q==-1);
    CHECK(tdd_to_q15(20,12,.8f,&q,&saturated) && saturated && q==26214);
    CHECK(tdd_to_q15(-20,12,.8f,&q,&saturated) && saturated && q==-26214);
    q=123; CHECK(!tdd_to_q15(NAN,12,.8f,&q,&saturated) && q==123);
    CHECK(!tdd_to_q15(0,0,.8f,&q,&saturated));
    double maximum_error=0;
    for (uint32_t i=0;i<4096;++i) {
        double expected=sin(2.0*3.14159265358979323846*(double)i/4096.0);
        double error=fabs((double)tdd_sine(i<<20)-expected);
        if (error>maximum_error) maximum_error=error;
    }
    CHECK(maximum_error<8e-5); // interpolation bound fixed before executing
}
static void test_supervisor(void)
{
    struct tdd_config cfg=configuration(); struct tdd_control c; struct tdd_output out;
    struct tdd_sample s={.seq=UINT32_MAX,.ticks=UINT32_MAX-2500u,.status=1,.raw=2048};
    CHECK(tdd_control_init(&c,&cfg,s.ticks));
    CHECK(tdd_control_step(&c,&s,s.ticks+1000,&out) && c.state==TDD_RUNNING && out.duty_q15==0);
    s.seq++; s.ticks+=5000;
    CHECK(tdd_control_step(&c,&s,s.ticks+1000,&out)); // both counters wrap
    uint32_t phase=c.phase;
    CHECK(tdd_control_set_amplitude(&c,.5f) && c.phase==phase);
    CHECK(!tdd_control_set_amplitude(&c,INFINITY));
    CHECK(!tdd_control_step(&c,&s,s.ticks+1000,&out) && c.fault==TDD_FAULT_SEQUENCE);
    CHECK(out.duty_q15==0);
    CHECK(!tdd_control_step(&c,&s,s.ticks+1000,&out)); // explicit reinit required
    CHECK(tdd_control_init(&c,&cfg,0)); s.seq=1; s.ticks=5000; s.status=0;
    CHECK(!tdd_control_step(&c,&s,6000,&out) && c.state==TDD_WARMUP);
    CHECK(!tdd_control_step(&c,&s,50000,&out) && c.state==TDD_FAULT);
    CHECK(tdd_control_init(&c,&cfg,0)); s.status=1;
    CHECK(!tdd_control_step(&c,&s,7000,&out) && c.fault==TDD_FAULT_TIME);
    CHECK(tdd_control_init(&c,&cfg,0)); s.raw=0;
    CHECK(!tdd_control_step(&c,&s,6000,&out) && c.fault==TDD_FAULT_CURRENT);
    CHECK(tdd_control_init(&c,&cfg,0)); s.raw=2048; s.status=9;
    CHECK(!tdd_control_step(&c,&s,6000,&out) && c.fault==TDD_FAULT_ADC);
    CHECK(tdd_control_init(&c,&cfg,0)); s.status=1;
    CHECK(tdd_control_step(&c,&s,6000,&out));
    c.cfg.b0=INFINITY; s.seq++; s.ticks+=5000;
    CHECK(!tdd_control_step(&c,&s,s.ticks+1000,&out) && c.fault==TDD_FAULT_NUMERIC);
    CHECK(tdd_control_init(&c,&cfg,0)); s.seq=1;s.ticks=5000;s.raw=2048;
    CHECK(tdd_control_step(&c,&s,6000,&out));
    s.seq++;s.ticks+=5000;s.raw=3995;
    CHECK(!tdd_control_step(&c,&s,s.ticks+1000,&out) && c.fault==TDD_FAULT_SATURATION);
}
struct mock_bus { unsigned read_index,write_count; bool tear; uint32_t write_offset,write_value; };
static uint32_t mock_read(void *context,uint32_t offset)
{
    static const uint32_t order[]={12,8,16,4,12};
    struct mock_bus *m=context; unsigned i=m->read_index++;
    CHECK(offset==order[i%5]);
    if (offset==12) return m->tear && i==0 ? 1 : 2;
    if (offset==8) return 2048;
    if (offset==16) return 5000;
    return 1;
}
static void mock_write(void *context,uint32_t offset,uint32_t value)
{
    struct mock_bus *m=context; m->write_offset=offset;m->write_value=value;m->write_count++;
}
static void test_mmio(void)
{
    struct mock_bus m={0}; struct tdd_bus bus={&m,mock_read,mock_write}; struct tdd_sample s;
    m.tear=true; CHECK(tdd_adc_read(&bus,&s)==TDD_ADC_OK && m.read_index==10 && s.seq==2);
    CHECK(tdd_pwm_write_duty(&bus,-31129)); CHECK(m.write_value==0x8667 && m.write_offset==4);
    CHECK(!tdd_pwm_write_duty(&bus,32000) && m.write_count==1);
    tdd_pwm_disable(&bus,2); CHECK(m.write_value==4 && m.write_offset==0);
}
static void test_capture_boundaries(void)
{
    uint32_t crc=UINT32_MAX;
    const char *vector="123456789";
    for (unsigned i=0;i<9;++i) crc=tdd_crc32_update(crc,(uint8_t)vector[i]);
    CHECK((crc^UINT32_MAX)==UINT32_C(0xcbf43926));
    const unsigned sizes[]={0,2000,2001,2048};
    for (unsigned n=0;n<4;++n) {
        CHECK(tdd_capture_init(&capture,20000,5000,2048,1.0f/1024));
        for (unsigned k=0;k<sizes[n];++k) CHECK(tdd_capture_push(&capture,2048,.2f,k,k*5000));
        CHECK(tdd_capture_request(&capture)); tdd_capture_service(&capture);
        unsigned ready=atomic_load(&capture.ready);
        CHECK((ready!=0)==(sizes[n]!=0));
        if (ready) CHECK(((capture.bank[ready-1].frozen.flags&TDD_CAPTURE_SHORT)!=0) == (sizes[n]<2001));
    }
    CHECK(!tdd_capture_init(&capture,20000,4998,0,1));
    CHECK(tdd_capture_init(&capture,40000,2500,2048,1.0f/1024));
    for (unsigned k=0;k<2048;++k) CHECK(tdd_capture_push(&capture,2048,0,k,k*2500));
    CHECK(tdd_capture_request(&capture)); tdd_capture_service(&capture);
    CHECK(capture.bank[0].frozen.flags&TDD_CAPTURE_SHORT);
}
static void test_fault_during_transmission(void)
{
    CHECK(tdd_capture_init(&capture,20000,5000,2048,1.0f/1024));
    for (unsigned k=0;k<5;++k) CHECK(tdd_capture_push(&capture,2048,.2f,k,k*5000));
    CHECK(tdd_capture_request(&capture));tdd_capture_service(&capture);
    unsigned frozen=atomic_load(&capture.ready)-1;
    for (unsigned k=5;k<8;++k) CHECK(tdd_capture_push(&capture,2048,.2f,k,k*5000));
    CHECK(!tdd_capture_push(&capture,2048,.2f,99,99*5000));
    CHECK(capture.bank[frozen].frozen.flags==TDD_CAPTURE_SHORT);
    CHECK(capture.bank[capture.active].count==3 && capture.stopped);
    struct tdd_transmitter tx={0}; CHECK(tdd_tx_begin(&capture,&tx));
    uint8_t data[17];
    while (tx.active) CHECK(tdd_tx_read(&capture,&tx,data,sizeof data)>0);
    tdd_capture_service(&capture);
    CHECK(tdd_capture_request(&capture));tdd_capture_service(&capture);
    frozen=atomic_load(&capture.ready)-1;
    CHECK(capture.bank[frozen].frozen.count==3);
    CHECK(capture.bank[frozen].frozen.first_seq==5);
    CHECK(capture.bank[frozen].frozen.flags==7);
}
static void closed_loop(float amplitude, bool change_load, const char *fixture)
{
    struct tdd_config cfg=configuration(); cfg.amplitude_a=amplitude;
    struct tdd_control c; struct tdd_output out;
    CHECK(tdd_control_init(&c,&cfg,0));
    CHECK(tdd_capture_init(&capture,20000,5000,cfg.offset_code,cfg.gain_a_per_code));
    double current=0, pending_voltage=0, fundamental_sin=0, fundamental_cos=0;
    for (unsigned k=0;k<6000;++k) {
        struct tdd_sample s={.seq=k,.ticks=k*5000,.status=1,
            .raw=(uint16_t)lround(2048.0+1024.0*current)};
        CHECK(tdd_control_step(&c,&s,s.ticks+1000,&out));
        CHECK(tdd_capture_push(&capture,s.raw,out.reference_a,s.seq,s.ticks));
        if (k>=4000) {
            double phase=2*3.14159265358979323846*50*k/20000.0;
            fundamental_sin+=current*sin(phase); fundamental_cos+=current*cos(phase);
        }
        double resistance=change_load && k>=4100 ? 6.5 : 5.0;
        double a=exp(-resistance*.00005/.0022);
        current=a*current+(1-a)/resistance*pending_voltage;
        pending_voltage=out.volts; // exactly one period sample-to-actuation delay
    }
    if (!change_load) {
        double measured=2.0*hypot(fundamental_sin,fundamental_cos)/2000;
        CHECK(fabs(measured/amplitude-1)<.10);
        printf("SYNTHETIC RL: Iref=%.1f A, fundamental=%.6f A\n",amplitude,measured);
    }
    CHECK(tdd_capture_request(&capture)); tdd_capture_service(&capture);
    CHECK(!tdd_capture_request(&capture));
    unsigned frozen=atomic_load(&capture.ready)-1;
    CHECK(capture.bank[frozen].frozen.first_seq==6000-2048);
    CHECK(capture.bank[frozen].frozen.flags==0);
    struct tdd_transmitter tx={0}; CHECK(tdd_tx_begin(&capture,&tx));
    FILE *file=fixture ? fopen(fixture,"wb") : NULL;
    if (fixture) CHECK(file!=NULL);
    size_t total=0; uint8_t bytes[13]; unsigned step=0;
    while (tx.active) {
        size_t n=tdd_tx_read(&capture,&tx,bytes,sizeof bytes);
        CHECK(n>0 && n<=sizeof bytes); total+=n;
        if (file) CHECK(fwrite(bytes,1,n,file)==n);
        // ISR continues during slow, fragmented transmission; frozen bank is immutable.
        CHECK(tdd_capture_push(&capture,(uint16_t)(step%4096),0,6000+step,(6000+step)*5000));
        CHECK(capture.bank[frozen].frozen.first_seq==6000-2048);
        ++step;
    }
    if (file) CHECK(fclose(file)==0);
    CHECK(total==8244);
    CHECK(!tdd_tx_begin(&capture,&tx));
    tdd_capture_service(&capture); CHECK(atomic_load(&capture.ready)==0);
    // Close the active interval BEFORE admitting a discontinuous sample.
    uint32_t previous_count=capture.bank[capture.active].count;
    CHECK(!tdd_capture_push(&capture,2048,0,90000,90000*5000));
    CHECK(capture.bank[capture.active].count==previous_count);
    CHECK(tdd_capture_request(&capture)); tdd_capture_service(&capture);
    frozen=atomic_load(&capture.ready)-1;
    CHECK((capture.bank[frozen].frozen.flags&6u)==6u);
}
int main(int argc,char **argv)
{
    test_numeric(); test_supervisor(); test_mmio(); test_capture_boundaries();
    test_fault_during_transmission();
    closed_loop(.2f,false,NULL); closed_loop(.5f,false,NULL);
    closed_loop(1.0f,false,argc>1 ? argv[1] : NULL); closed_loop(.5f,true,NULL);
    printf("PASS firmware: %u assertions; record banks: %lu bytes\n",assertions,
        (unsigned long)(sizeof capture.bank[0].records+sizeof capture.bank[1].records));
    return 0;
}
