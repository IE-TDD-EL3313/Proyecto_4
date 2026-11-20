/* Acquisition-only Standalone SDT application. No PR and no gate release.
   Connect the board without the inverter to perform the physical verification. */
#include "../app/adc_verify.h"
#include "board_config.h"
#include "xil_io.h"
#include "xil_exception.h"
#include "xintc.h"
#include "xstatus.h"
#ifndef SDT
#error "Generate the Standalone SDT BSP from the integrated 128 KiB XSA."
#endif
#ifndef TDD_ADC_VERIFY_FREQ_CODE
#define TDD_ADC_VERIFY_FREQ_CODE TDD_BOARD_FREQ_CODE
#endif
#define ADC_BASE UINT32_C(0x44a00000)
#define PWM_BASE UINT32_C(0x44a10000)
#define TIMER_BASE UINT32_C(0x41c00000)
#define UART_BASE UINT32_C(0x40600000)
#define GPIO_BASE UINT32_C(0x40000000)
#define INTC_BASE UINT32_C(0x41200000)
static struct tdd_adc_verify acquisition;
static struct tdd_transmitter transmitter;
static XIntc intc;
static uint32_t read32(void *context,uint32_t offset)
{
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    uint32_t value=Xil_In32((UINTPTR)context+offset);
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    return value;
}
static void write32(void *context,uint32_t offset,uint32_t value)
{
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    Xil_Out32((UINTPTR)context+offset,value);
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    (void)Xil_In32((UINTPTR)context+((UINTPTR)context==UART_BASE?8u:offset));
}
static const struct tdd_bus adc={(void *)(uintptr_t)ADC_BASE,read32,write32};
static const struct tdd_bus pwm={(void *)(uintptr_t)PWM_BASE,read32,write32};
static const struct tdd_bus timer={(void *)(uintptr_t)TIMER_BASE,read32,write32};
static void irq(void *context)
{
    (void)context;
    write32((void *)(uintptr_t)GPIO_BASE,8u,1u);
    tdd_adc_verify_irq(&acquisition);
    write32((void *)(uintptr_t)GPIO_BASE,8u,acquisition.state==TDD_VERIFY_FAULT?2u:0u);
}
int main(void)
{
    Xil_ExceptionDisable();tdd_pwm_disable(&pwm,0);tdd_timer_stop(&timer);
    write32((void *)(uintptr_t)GPIO_BASE,4u,7u);
    write32((void *)(uintptr_t)GPIO_BASE,12u,0u);
    write32((void *)(uintptr_t)UART_BASE,12u,3u);
    write32(adc.context,0,8u);
    bool ready=false;uint32_t start=read32(adc.context,24u);
    for(unsigned attempt=0;attempt<1000000u;++attempt){
        if(read32(adc.context,28u)&1u){ready=true;break;}
        if(read32(adc.context,24u)-start>=1000000u)break;
    }
    if(!ready)goto failed;
    if(XIntc_Initialize(&intc,(UINTPTR)INTC_BASE)!=XST_SUCCESS)goto failed;
    if(XIntc_Connect(&intc,0,irq,NULL)!=XST_SUCCESS)goto failed;
    if(XIntc_Start(&intc,XIN_REAL_MODE)!=XST_SUCCESS)goto failed;
    XIntc_Enable(&intc,0);Xil_ExceptionInit();
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,(Xil_ExceptionHandler)XIntc_InterruptHandler,&intc);
    if(!tdd_adc_verify_prepare(&acquisition,adc,pwm,timer,TDD_ADC_VERIFY_FREQ_CODE))goto failed;
    Xil_ExceptionEnable();tdd_timer_start(&timer);
    uint32_t previous=0,changed=read32(adc.context,24u);bool stable=false,auto_requested=false;
    uint8_t bytes[32];size_t used=0,position=0;
    for(;;){
        uint32_t now=read32(adc.context,24u),button=read32((void *)(uintptr_t)GPIO_BASE,0)&1u;
        if(button!=previous){previous=button;changed=now;}
        if((bool)button!=stable && now-changed>=2000000u){
            stable=(bool)button;
            if(stable)(void)tdd_capture_request(&acquisition.capture);
        }
        if(!auto_requested && atomic_load_explicit(&acquisition.samples,memory_order_acquire)>=TDD_CAPTURE_CAPACITY)
            auto_requested=tdd_capture_request(&acquisition.capture);
        if(!used){
            position=0;
            if(!transmitter.active)(void)tdd_tx_begin(&acquisition.capture,&transmitter);
            used=tdd_tx_read(&acquisition.capture,&transmitter,bytes,sizeof bytes);
        }
        for(unsigned sent=0;sent<8u && position<used;++sent){
            if(read32((void *)(uintptr_t)UART_BASE,8u)&8u)break;
            write32((void *)(uintptr_t)UART_BASE,4u,bytes[position++]);
        }
        if(position==used)used=0;
    }
failed:
    Xil_ExceptionDisable();tdd_pwm_disable(&pwm,0);tdd_timer_stop(&timer);
    write32((void *)(uintptr_t)GPIO_BASE,8u,2u);
    for(;;){}
}
