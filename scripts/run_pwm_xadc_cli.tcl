# Programa la Nexys 4 y ejecuta el diagnostico PWM-XADC sin abrir Vitis.
set repo [file normalize [file join [file dirname [info script]] ..]]
set bit [file join $repo build nexys4_pwm_test nexys4_control.bit]
set elf [file join $repo vitis_workspace_pr pwm_xadc_continuo build pwm_xadc_continuo.elf]

if {![file isfile $bit]} {error "No existe el bitstream: $bit"}
if {![file isfile $elf]} {error "No existe el ELF: $elf"}

connect -url tcp:127.0.0.1:3121
targets -set -filter {name =~ "xc7a100t"}
puts "Programando FPGA con: $bit"
fpga -file $bit
after 1500

set cpus [targets -target-properties -filter {name =~ "Hart #0"}]
if {[llength $cpus] == 0} {
    puts "Targets disponibles despues de programar:"
    puts [targets]
    error "No se encontro el Hart #0 del MicroBlaze V"
}

targets -set -filter {name =~ "Hart #0"}
stop
rst -processor
puts "Descargando ELF: $elf"
dow $elf
con
puts "PASS: FPGA programada y pwm_xadc_continuo ejecutandose"
disconnect
exit
