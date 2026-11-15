# Simula el testbench del PWM con el simulador de Vivado (xsim), en modo batch.
# Uso: abrir la "Vivado Tcl Shell", ir a la raiz del repo y ejecutar
#   source scripts/sim_pwm_vivado.tcl
set root [pwd]
file mkdir resultados/pwm/xsim
cd resultados/pwm/xsim
set src [glob $root/rtl/pwm_controller/*.sv]
exec xvlog -sv {*}$src $root/tb/pwm_controller/tb_pwm_controller.sv
exec xelab tb_pwm_controller -s tb_pwm_sim -debug typical
catch {exec xsim tb_pwm_sim -runall -log ../sim_pwm_xsim.txt} out
puts $out
cd $root
