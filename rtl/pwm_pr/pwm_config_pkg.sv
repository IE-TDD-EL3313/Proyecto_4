package pwm_config_pkg;
    parameter int unsigned CLK_HZ = 100_000_000;
    parameter int unsigned FREQ0_HZ = 10_000;
    parameter int unsigned FREQ1_HZ = 15_000;
    parameter int unsigned FREQ2_HZ = 20_000;
    parameter int unsigned PWM_WIDTH = 16;
    parameter int unsigned DUTY_SCALE = 1000;
    parameter int unsigned DEADTIME_DEFAULT = 50;
    parameter int unsigned TRIGGER_WIDTH = 8;
endpackage
