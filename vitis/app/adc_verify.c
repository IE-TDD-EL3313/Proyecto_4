#include "adc_verify.h"
#include <string.h>

static uint32_t clock_ticks(struct tdd_adc_verify *v)
{
    return v->adc.read32(v->adc.context,0x18u);
}
static void fail(struct tdd_adc_verify *v, enum tdd_adc_verify_fault reason, bool adc_error)
{
    tdd_pwm_disable(&v->pwm,0);
    v->state=TDD_VERIFY_FAULT;v->fault=reason;
    tdd_capture_fault(&v->capture,adc_error);
}
bool tdd_adc_verify_prepare(struct tdd_adc_verify *v, struct tdd_bus adc,
    struct tdd_bus pwm, struct tdd_bus timer, uint32_t frequency_code)
{
    tdd_pwm_disable(&pwm,0);tdd_timer_stop(&timer);
    if (!v || !adc.read32 || !adc.write32 || !pwm.read32 || !pwm.write32 ||
        !timer.read32 || !timer.write32 || frequency_code>2u) return false;
    memset(v,0,sizeof *v);atomic_init(&v->samples,0);
    v->adc=adc;v->pwm=pwm;v->timer=timer;v->period=tdd_pwm_period(frequency_code);
    v->state=TDD_VERIFY_FAULT;v->fault=TDD_VERIFY_CONFIG;v->age_min=UINT32_MAX;
    if (!(adc.read32(adc.context,0x1cu)&1u) ||
        !tdd_capture_init_raw(&v->capture,v->period) ||
        !tdd_timer_prepare_ticks(&timer,v->period) ||
        !tdd_pwm_configure(&pwm,frequency_code,100u,1000u)) return false;
    adc.write32(adc.context,0,3u);
    if (!tdd_pwm_arm(&pwm,frequency_code)) return false;
    v->state=TDD_VERIFY_SYNC;v->fault=TDD_VERIFY_OK;v->prepared=true;
    return true;
}
void tdd_adc_verify_irq(struct tdd_adc_verify *v)
{
    if (!v || !v->prepared) return;
    uint32_t entered=clock_ticks(v);
    if (!tdd_timer_ack(&v->timer)) return;
    ++v->interrupts;
    if (v->state==TDD_VERIFY_FAULT) {tdd_capture_service(&v->capture);goto done;}
    uint32_t pwm=v->pwm.read32(v->pwm.context,0);
    /* Both software permission and actual power-state indications must be off. */
    if (pwm&(16u|32u|64u|512u|1024u)) {fail(v,TDD_VERIFY_GATES,false);goto done;}
    if (v->state==TDD_VERIFY_SYNC) {
        if ((pwm&(1u|256u))!=(1u|256u)) {fail(v,TDD_VERIFY_CONFIG,false);goto done;}
        v->sync_ticks=clock_ticks(v);v->state=TDD_VERIFY_DATA;goto done;
    }
    struct tdd_sample sample={0};
    enum tdd_adc_result result=tdd_adc_read(&v->adc,&sample);
    if (result==TDD_ADC_ERROR || result==TDD_ADC_INCOHERENT) {fail(v,TDD_VERIFY_ADC,true);goto done;}
    if (result!=TDD_ADC_OK || (v->state==TDD_VERIFY_DATA &&
        sample.ticks-v->sync_ticks>=UINT32_C(0x80000000))) {
        if (v->state==TDD_VERIFY_RUNNING) fail(v,TDD_VERIFY_GAP,true);
        else if (clock_ticks(v)-v->sync_ticks>=10u*v->period) fail(v,TDD_VERIFY_START_TIMEOUT,true);
        goto done;
    }
    uint32_t age=clock_ticks(v)-sample.ticks;
    if (age>=v->period) {fail(v,TDD_VERIFY_STALE,true);goto done;}
    if (v->state==TDD_VERIFY_RUNNING && (sample.seq!=v->last_seq+1u ||
        sample.ticks-v->last_ticks!=v->period)) {fail(v,TDD_VERIFY_GAP,true);goto done;}
    if (!tdd_capture_push_raw(&v->capture,sample.raw,sample.seq,sample.ticks)) {
        fail(v,TDD_VERIFY_ADC,true);goto done;
    }
    v->last_seq=sample.seq;v->last_ticks=sample.ticks;v->state=TDD_VERIFY_RUNNING;
    if (age<v->age_min)v->age_min=age;
    if (age>v->age_max)v->age_max=age;
    unsigned count=atomic_load_explicit(&v->samples,memory_order_relaxed);
    atomic_store_explicit(&v->samples,count+1u,memory_order_release);
done:
    {
        uint32_t elapsed=clock_ticks(v)-entered;
        if (elapsed>v->max_service_ticks)v->max_service_ticks=elapsed;
    }
}
