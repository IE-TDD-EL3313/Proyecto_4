/* Standalone MicroBlaze V register test for issue #13. */
#include "xil_exception.h"
#include "xil_io.h"
#include "xil_printf.h"
#include "xparameters.h"

enum {
    CTRL_STATUS = 0x00,
    DUTY_REF = 0x04,
    DEADTIME = 0x08,
    SYNC_PHASE = 0x0c,
    DUTY_LATCHED = 0x10,
    CTRL_FREQ_20KHZ = 2u << 1,
    CTRL_RO = (1u << 3) | (1u << 5) | (1u << 6) |
              (1u << 8) | (1u << 9) | (1u << 10)
};

static uint32_t pwm_read(unsigned offset)
{
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    return Xil_In32((UINTPTR)XPAR_CONTROL_PWM_BASEADDR + offset);
}

static void pwm_write(unsigned offset, uint32_t value)
{
    /* Only the CTRL_STATUS test writes that register. Its pattern leaves
       enable, gates_en and sync_arm clear throughout the test. */
    Xil_Out32((UINTPTR)XPAR_CONTROL_PWM_BASEADDR + offset, value);
}

static void expect(const char *label, unsigned offset, uint32_t expected)
{
    uint32_t actual = pwm_read(offset);
    if (actual != expected) {
        xil_printf("FAIL %s got=%08x expected=%08x\r\n", label, actual, expected);
        for (;;) {}
    }
    xil_printf("OK %s=%08x\r\n", label, actual);
}

static void write_and_check(const char *label, unsigned offset, uint32_t value,
                            uint32_t expected)
{
    pwm_write(offset, value);
    expect(label, offset, expected);
}

int main(void)
{
    Xil_ExceptionDisable();
    xil_printf("AXI_PWM #13 BASE=%08x\r\n", XPAR_CONTROL_PWM_BASEADDR);

    /* Read hardware reset values before the first PWM write. */
    expect("RESET CTRL_STATUS", CTRL_STATUS, 0);
    expect("RESET DUTY_REF", DUTY_REF, 0);
    expect("RESET DEADTIME", DEADTIME, 100);
    expect("RESET SYNC_PHASE", SYNC_PHASE, 1000);
    expect("RESET DUTY_LATCHED", DUTY_LATCHED, 0);

    const uint32_t reserved = 0xfffff800u;
    write_and_check("CTRL_STATUS RO/reserved", CTRL_STATUS,
                    reserved | CTRL_RO | CTRL_FREQ_20KHZ, CTRL_FREQ_20KHZ);
    write_and_check("DUTY_REF positive", DUTY_REF, 0xa5a51234u, 0x1234u);
    write_and_check("DUTY_REF negative", DUTY_REF, 0x5678edccu, 0xedccu);
    write_and_check("DUTY_REF clamp positive", DUTY_REF, 0x7fffu, 0x7999u);
    const uint32_t negative_limit = 0x8667u;
    write_and_check("DUTY_REF clamp negative", DUTY_REF, 0x8000u, negative_limit);

    /* Leave distinct, safe values in the writable registers for the
       physical reset check on the next run. */
    write_and_check("DIRTY DEADTIME", DEADTIME, 120, 120);
    write_and_check("DIRTY SYNC_PHASE", SYNC_PHASE, 900, 900);
    /* While stopped, the shadow follows DUTY_REF and VALID (bit 16) is clear. */
    expect("STOPPED DUTY_LATCHED", DUTY_LATCHED, negative_limit);
    expect("STOPPED CTRL_STATUS", CTRL_STATUS, CTRL_FREQ_20KHZ);
    xil_printf("PASS AXI_PWM #13 - press CPU RESET to verify defaults again\r\n");
    for (;;) {}
}
