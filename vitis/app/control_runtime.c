#include "control_runtime.h"
#include <string.h>
static uint32_t ticks(struct tdd_runtime *rt)
{
    return rt->io.adc.read32(rt->io.adc.context,0x18u);
}
static void runtime_fault(struct tdd_runtime *rt, enum tdd_fault reason, bool adc_error)
{
    rt->io.block_gates(rt->io.context); // C4: higher priority than pending duty
    tdd_pwm_disable(&rt->io.pwm,0);
    rt->control.state=TDD_FAULT; rt->control.fault=reason;
    tdd_capture_fault(&rt->capture,adc_error);
}
bool tdd_runtime_prepare(struct tdd_runtime *rt, const struct tdd_platform *io,
    const struct tdd_config *cfg, uint32_t latch_age, uint32_t write_budget,
    uint32_t margin, bool c4_confirmed)
{
    if (!rt || !io || !io->block_gates || !io->pwm.write32) return false;
    io->block_gates(io->context); tdd_pwm_disable(&io->pwm,0);
    memset(rt,0,sizeof *rt); rt->io=*io;
    rt->control.state=TDD_FAULT; rt->control.fault=TDD_FAULT_CONFIG;
    if (!c4_confirmed || !io->release_at_valley || !io->adc.read32 ||
        !io->timer.read32 || !io->timer.write32 || !tdd_config_valid(cfg) ||
        !write_budget || !margin || latch_age>cfg->period_ticks ||
        (uint64_t)cfg->age_max_ticks+write_budget+margin>=latch_age) return false;
    if (!tdd_control_init(&rt->control,cfg,ticks(rt)) ||
        !tdd_capture_init(&rt->capture,cfg->fs_hz,cfg->period_ticks,
                          cfg->offset_code,cfg->gain_a_per_code) ||
        !tdd_timer_prepare(&io->timer,cfg->fs_hz)) return false;
    rt->latch_age_ticks=latch_age; rt->write_budget_ticks=write_budget; rt->margin_ticks=margin;
    rt->prepared=true;
    return true; // main still must connect INTC, arm sync, enable carrier and start Timer
}
void tdd_runtime_irq(struct tdd_runtime *rt)
{
    if (!rt || !rt->prepared) return;
    uint32_t entered=ticks(rt);
    if (rt->io.mark_isr) rt->io.mark_isr(rt->io.context,true);
    if (!tdd_timer_ack(&rt->io.timer)) goto done;
    ++rt->interrupts;
    if (rt->control.state==TDD_FAULT) { tdd_capture_service(&rt->capture); goto done; }
    struct tdd_sample sample={0}; struct tdd_output out;
    enum tdd_adc_result result=tdd_adc_read(&rt->io.adc,&sample);
    if (result==TDD_ADC_ERROR || result==TDD_ADC_INCOHERENT) {
        runtime_fault(rt,TDD_FAULT_ADC,true); goto done;
    }
    if (!tdd_control_step(&rt->control,&sample,ticks(rt),&out)) {
        if (rt->control.state==TDD_FAULT)
            runtime_fault(rt,rt->control.fault,rt->control.fault==TDD_FAULT_ADC ||
                          rt->control.fault==TDD_FAULT_SEQUENCE || rt->control.fault==TDD_FAULT_TIME);
        goto done;
    }
    uint32_t age=ticks(rt)-sample.ticks;
    if (age>=rt->latch_age_ticks-rt->write_budget_ticks-rt->margin_ticks) {
        runtime_fault(rt,TDD_FAULT_TIME,false); goto done;
    }
    if (!tdd_pwm_write_duty(&rt->io.pwm,out.duty_q15)) {
        runtime_fault(rt,TDD_FAULT_NUMERIC,false); goto done;
    }
    // write32 must have completed the AXI transaction before returning.
    if (ticks(rt)-sample.ticks>=rt->latch_age_ticks-rt->margin_ticks) {
        runtime_fault(rt,TDD_FAULT_TIME,false); goto done;
    }
    if (!rt->released) {
        if (!rt->io.release_at_valley(rt->io.context)) {
            runtime_fault(rt,TDD_FAULT_CONFIG,false); goto done;
        }
        rt->released=true;
    }
    if (!tdd_capture_push(&rt->capture,sample.raw,out.reference_a,sample.seq,sample.ticks))
        runtime_fault(rt,TDD_FAULT_ADC,true);
done:
    {
        uint32_t duration=ticks(rt)-entered;
        if (duration>rt->max_instrumented_ticks) rt->max_instrumented_ticks=duration;
    }
    if (rt->io.mark_isr) rt->io.mark_isr(rt->io.context,false);
}
