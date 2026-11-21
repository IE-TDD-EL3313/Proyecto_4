/* Standalone SDT application for the system built by build_nexys4.tcl.
   Default mode acquires raw ADC codes with all power gates blocked. */
#include "board_config.h"
#include "../app/control_runtime.h"
#include "xil_io.h"
#include "xil_exception.h"
#include "xintc.h"
#include "xstatus.h"
#include <stdatomic.h>
#include <string.h>
#ifndef SDT
#error "Generate a Standalone SDT BSP from the integrated Nexys4 XSA."
#endif
/* Assigned and checked by scripts/build_nexys4.tcl, not guessed BSP addresses. */
#define ADC_BASE UINT32_C(0x44a00000)
#define PWM_BASE UINT32_C(0x44a10000)
#define TIMER_BASE UINT32_C(0x41c00000)
#define UART_BASE UINT32_C(0x40600000)
#define GPIO_BASE UINT32_C(0x40000000)
#define INTC_BASE UINT32_C(0x41200000)
static struct tdd_runtime runtime; /* 48 KiB capture arrays: never put on stack */
static struct tdd_transmitter transmitter;
static XIntc intc;
static bool diagnostic;
static atomic_uint amplitude_request;
static atomic_uint published_state;
static uint32_t frequency_code=TDD_BOARD_FREQ_CODE;
static uint32_t read32(void *ctx,uint32_t offset)
{
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    uint32_t value=Xil_In32((UINTPTR)ctx+offset);
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    return value;
}
static void write32(void *ctx,uint32_t offset,uint32_t value)
{
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    Xil_Out32((UINTPTR)ctx+offset,value);
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    (void)Xil_In32((UINTPTR)ctx+((UINTPTR)ctx==UART_BASE?8u:offset));
}
static const struct tdd_bus adc={(void *)(uintptr_t)ADC_BASE,read32,write32};
static const struct tdd_bus pwm={(void *)(uintptr_t)PWM_BASE,read32,write32};
static const struct tdd_bus timer={(void *)(uintptr_t)TIMER_BASE,read32,write32};
static void block(void *ctx) {(void)ctx;tdd_pwm_block(&pwm);}
static bool request_gates(void *ctx) {(void)ctx;return tdd_pwm_request_gates(&pwm);}
static bool gates_ready(void *ctx) {(void)ctx;return tdd_pwm_ready(&pwm);}
static void marker(void *ctx,bool high)
{
    (void)ctx;
    uint32_t fault=(diagnostic || runtime.control.state==TDD_FAULT)?2u:0u;
    write32((void *)(uintptr_t)GPIO_BASE,8u,fault|(high?1u:0u));
}
static void irq(void *ctx)
{
    (void)ctx;
    if(diagnostic){marker(NULL,true);(void)tdd_timer_ack(&timer);marker(NULL,false);return;}
    /* Single hart, non-nested ISR: main cannot publish between this load and
       clear. Avoid an atomic RMW: the configured RV32IM CPU has no A extension. */
    unsigned code=atomic_load_explicit(&amplitude_request,memory_order_acquire);
    if(code) atomic_store_explicit(&amplitude_request,0,memory_order_relaxed);
    if(code){static const float levels[]={.2f,.5f,1.f};
        (void)tdd_control_set_amplitude(&runtime.control,levels[(code-1u)%3u]);}
    tdd_runtime_irq(&runtime);
    atomic_store_explicit(&published_state,runtime.control.state,memory_order_release);
}
static bool prepare(void)
{
    tdd_timer_stop(&timer);block(NULL);tdd_pwm_disable(&pwm,frequency_code);
    /* Reset both acquisition state/VALID and the physical XADC. */
    write32(adc.context,0,8u);
    uint32_t start=read32(adc.context,24u);bool ready=false;
    for(unsigned attempt=0;attempt<1000000u;++attempt){
        if(read32(adc.context,28u)&1u){ready=true;break;}
        if(read32(adc.context,24u)-start>1000000u)break; /* 10 ms bound */
    }
    if(!ready)return false;
    uint32_t period=tdd_pwm_period(frequency_code);
    if(!period)return false;
    struct tdd_config cfg={0};
    cfg.fs_hz=100000000u%period?0u:100000000u/period;cfg.period_ticks=period;
    cfg.age_min_ticks=TDD_SAMPLE_AGE_MIN;cfg.age_max_ticks=TDD_SAMPLE_AGE_MAX;
    cfg.warmup_ticks=10u*period;cfg.raw_min=TDD_RAW_MIN;cfg.raw_max=TDD_RAW_MAX;
    cfg.deadtime_cycles=TDD_BOARD_DEADTIME;cfg.offset_code=TDD_ADC_OFFSET;
    cfg.gain_a_per_code=TDD_ADC_GAIN;cfg.vdc=TDD_BOARD_VDC;cfg.m_max=TDD_M_MAX;
    cfg.amplitude_a=.2f;cfg.b0=TDD_B0;cfg.b1=TDD_B1;cfg.b2=TDD_B2;cfg.a1=TDD_A1;cfg.a2=TDD_A2;
    diagnostic=!(TDD_SENSING_CONFIRMED && TDD_PR_CONFIRMED &&
        TDD_MODULATION_LIMIT_CONFIRMED && TDD_PROTECTION_CONFIRMED && TDD_TIMING_CONFIRMED);
    if(!diagnostic){
        struct tdd_platform io={.adc=adc,.pwm=pwm,.timer=timer,.context=NULL,
            .block_gates=block,.request_gates=request_gates,.gates_ready=gates_ready,.mark_isr=marker};
        if(!tdd_runtime_prepare(&runtime,&io,&cfg,period,TDD_WRITE_BUDGET,TDD_LATCH_MARGIN,true))return false;
    }else if(!tdd_timer_prepare_ticks(&timer,period))return false;
    if(!tdd_pwm_configure(&pwm,frequency_code,TDD_BOARD_DEADTIME,TDD_BOARD_SYNC_PHASE))return false;
    write32(adc.context,0,3u); /* event acquisition, held off by ADC trigger gate */
    return tdd_pwm_arm(&pwm,frequency_code);
}
static void hex_word(char *dst,uint32_t value,unsigned digits)
{
    const char hex[]="0123456789ABCDEF";
    for(unsigned n=0;n<digits;++n)dst[n]=hex[(value>>(4u*(digits-1u-n)))&15u];
}
int main(void)
{
    Xil_ExceptionDisable();atomic_init(&amplitude_request,0);
    atomic_init(&published_state,TDD_WARMUP);
    tdd_pwm_disable(&pwm,frequency_code);tdd_timer_stop(&timer);
    write32((void *)(uintptr_t)GPIO_BASE,4,7u); /* buttons input */
    write32((void *)(uintptr_t)GPIO_BASE,12,0u); /* marker/LED output */
    write32((void *)(uintptr_t)UART_BASE,12,3u); /* reset RX/TX FIFOs, IRQ off */
    if(XIntc_Initialize(&intc,(UINTPTR)INTC_BASE)!=XST_SUCCESS)goto failed;
    if(XIntc_Connect(&intc,0,irq,NULL)!=XST_SUCCESS)goto failed;
    if(XIntc_Start(&intc,XIN_REAL_MODE)!=XST_SUCCESS)goto failed;
    XIntc_Enable(&intc,0); /* Timer is concatenator input 0; UART remains polled */
    Xil_ExceptionInit();
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,(Xil_ExceptionHandler)XIntc_InterruptHandler,&intc);
    if(!prepare())goto failed;
    marker(NULL,false);Xil_ExceptionEnable();tdd_timer_start(&timer);
    uint32_t changed[3]={0},raw_prev=0,stable=0,last_diag=0;
    unsigned amplitude=0;uint8_t bytes[32];size_t used=0,pos=0;
    for(;;){
        uint32_t now=read32(adc.context,24u),buttons=read32((void *)(uintptr_t)GPIO_BASE,0)&7u;
        for(unsigned n=0;n<3;++n){uint32_t bit=1u<<n;
            if((buttons^raw_prev)&bit)changed[n]=now;
            if(((buttons^stable)&bit) && now-changed[n]>=2000000u){
                stable^=bit;
                if(stable&bit){
                    if(n==0 && !diagnostic)(void)tdd_capture_request(&runtime.capture);
                    if(n==1 && !diagnostic){amplitude=(amplitude+1u)%3u;
                        atomic_store_explicit(&amplitude_request,amplitude+1u,memory_order_release);}
                    if(n==2 && !diagnostic && atomic_load_explicit(&published_state,memory_order_acquire)==TDD_FAULT && !used &&
                        !transmitter.active && !atomic_load_explicit(&runtime.capture.ready,memory_order_acquire)){
                        Xil_ExceptionDisable();
                        if(!prepare())goto failed;
                        memset(&transmitter,0,sizeof transmitter);
                        Xil_ExceptionEnable();tdd_timer_start(&timer);
                        now=read32(adc.context,24u);for(unsigned j=0;j<3;++j)changed[j]=now;
                        amplitude=0;atomic_store_explicit(&amplitude_request,0,memory_order_release);
                    }
                }
            }
        }
        raw_prev=buttons;
        if(!used){
            pos=0;
            if(!diagnostic){
                if(!transmitter.active)(void)tdd_tx_begin(&runtime.capture,&transmitter);
                used=tdd_tx_read(&runtime.capture,&transmitter,bytes,sizeof bytes);
            }else if(now-last_diag>=10000000u){ /* raw codes at 10 Hz, no invented amperes */
                struct tdd_sample sample;last_diag=now;
                if(tdd_adc_read(&adc,&sample)==TDD_ADC_OK){
                    const char line[]="RAW=0x0000 SEQ=0x00000000\r\n";
                    used=sizeof line-1u;memcpy(bytes,line,used);
                    hex_word((char *)bytes+6,sample.raw,4);hex_word((char *)bytes+17,sample.seq,8);
                }
            }
        }
        for(unsigned sent=0;sent<8u && pos<used;++sent){
            if(read32((void *)(uintptr_t)UART_BASE,8u)&8u)break; /* TX full */
            write32((void *)(uintptr_t)UART_BASE,4u,bytes[pos++]);
        }
        if(pos==used)used=0;
    }
failed:
    Xil_ExceptionDisable();tdd_timer_stop(&timer);tdd_pwm_disable(&pwm,frequency_code);
    write32((void *)(uintptr_t)GPIO_BASE,8u,2u);
    for(;;){} /* latched configuration/platform failure; no automatic power restart */
}
