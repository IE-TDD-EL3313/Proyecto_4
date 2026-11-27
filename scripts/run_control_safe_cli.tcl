# Programa la Nexys 4 con compuertas bloqueadas y ejecuta el control integrado.
set repo [file normalize [file join [file dirname [info script]] ..]]
set bit [file join $repo build nexys4_pr nexys4_control.bit]
set elf [file join $repo vitis_workspace_pr control_pr_app build control_pr_app.elf]

if {![file isfile $bit]} {error "No existe el bitstream seguro: $bit"}
if {![file isfile $elf]} {error "No existe el ELF: $elf"}

connect -url tcp:127.0.0.1:3121
targets -set -filter {name =~ "xc7a100t"}
puts "Programando bitstream seguro: $bit"
fpga -file $bit
after 1500

set cpus [targets -target-properties -filter {name =~ "Hart #0"}]
if {[llength $cpus] == 0} {
    puts [targets]
    error "No se encontro el Hart #0 del MicroBlaze V"
}

targets -set -filter {name =~ "Hart #0"}
stop
rst -processor
puts "Descargando control integrado seguro: $elf"
dow $elf
con
puts "PASS: control integrado ejecutandose con compuertas bloqueadas"
disconnect
exit
