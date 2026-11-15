#!/usr/bin/env bash
# Simula el testbench autoverificable del PWM con Icarus Verilog.
# Uso (desde la raiz del repo):  bash scripts/sim_pwm.sh
# Genera resultados/pwm/sim_pwm.txt con el reporte PASS/FAIL.
set -e
mkdir -p resultados/pwm/build
iverilog -g2012 -o resultados/pwm/build/tb_pwm.vvp \
    rtl/pwm_controller/*.sv tb/pwm_controller/tb_pwm_controller.sv
vvp -n resultados/pwm/build/tb_pwm.vvp | tee resultados/pwm/sim_pwm.txt
