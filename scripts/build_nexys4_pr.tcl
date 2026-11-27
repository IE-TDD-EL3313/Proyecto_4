# Rebuild the integrated system from the shared MicroBlaze project.
# vivado -mode batch -source scripts/build_nexys4.tcl -tclargs validate|synth|bitstream ?output_dir?
set repo [file normalize [file join [file dirname [info script]] ..]]
set mode [lindex $argv 0]
if {$mode eq ""} {set mode validate}
if {$mode ni {validate synth bitstream}} {error "Use validate, synth or bitstream"}
set_param general.maxThreads 2
set out [file join $repo build nexys4_pr]
if {[llength $argv] > 1} {set out [file normalize [lindex $argv 1]]}
file mkdir $out
set scratch [file normalize [file join /tmp tdd4_pr_[pid]]]
file mkdir $scratch
file copy [file join $repo vivado microblaze_v_system] [file join $scratch p]
file copy [file join $repo rtl] [file join $scratch rtl]
file copy [file join $repo vivado constraints] [file join $scratch constraints]
cd $scratch
open_project [file join $scratch p microblaze_v_system.xpr]
set bd [get_files */system_bd.bd]
open_bd_design $bd
add_files [glob [file join $scratch rtl pwm_controller *.sv]]
add_files [glob [file join $scratch rtl pwm_pr *.sv]]
add_files [file join $scratch rtl pwm_pr pwm_axi.v]
add_files [glob [file join $scratch rtl xadc_if *.sv]]
update_compile_order -fileset sources_1
# Preserve the existing AXI XADC Wizard for the PR controller.
# The ADC uses VAUX3 on the Nexys 4.
set_property CONFIG.TIMING_MODE {Event} [get_bd_cells xadc_wiz_0]
# ============================================================
# Integracion PR - Nexys 4 Rev. B
# ============================================================

# Incorporar modulo de inhibicion de salidas.
add_files [file join $scratch rtl integracion_pr pwm_output_guard.sv]
update_compile_order -fileset sources_1

# Instanciar PWM AXI de los companeros.
create_bd_cell -type module -reference pwm_axi control_pwm

# Instanciar proteccion de salidas.
create_bd_cell -type module -reference pwm_output_guard pwm_guard

# Habilitar un sexto puerto maestro AXI.
set_property CONFIG.NUM_MI {6}     [get_bd_cells microblaze_riscv_0_axi_periph]

# Mantener configuracion de Timer y GPIO.
set_property CONFIG.enable_timer2 {1}     [get_bd_cells axi_timer_0]

set_property -dict [list     CONFIG.C_GPIO_WIDTH {3}     CONFIG.C_GPIO2_WIDTH {2}     CONFIG.C_ALL_INPUTS {1}     CONFIG.C_ALL_OUTPUTS_2 {1}     CONFIG.C_INTERRUPT_PRESENT {0}]     [get_bd_cells axi_gpio_0]

proc wire_control {a b} {
    connect_bd_net [get_bd_pins $a] [get_bd_pins $b]
}

# Reloj y reset del PWM.
wire_control clk_wiz_1/clk_out1 control_pwm/s_axi_aclk
wire_control rst_clk_wiz_1_100M/peripheral_aresetn     control_pwm/s_axi_aresetn

# Conexion AXI del PWM.
connect_bd_intf_net     [get_bd_intf_pins microblaze_riscv_0_axi_periph/M05_AXI]     [get_bd_intf_pins control_pwm/s_axi]

# Disparo sincronizado del XADC.
wire_control control_pwm/pwm_trigger_o xadc_wiz_0/convst_in

# Conexiones PWM hacia inhibicion.
foreach {origen destino} {
    pwm_ah_o pwm_ah_i
    pwm_al_o pwm_al_i
    pwm_bh_o pwm_bh_i
    pwm_bl_o pwm_bl_i
} {
    wire_control control_pwm/$origen pwm_guard/$destino
}

# Salidas fisicas desde el modulo de inhibicion.
foreach pin {
    gate_ah_o
    gate_al_o
    gate_bh_o
    gate_bl_o
} {
    connect_bd_net         [create_bd_port -dir O $pin]         [get_bd_pins pwm_guard/$pin]
}

# Botones y depuracion mediante interfaces GPIO.
make_bd_intf_pins_external [get_bd_intf_pins axi_gpio_0/GPIO]
make_bd_intf_pins_external [get_bd_intf_pins axi_gpio_0/GPIO2]

set_property name user_buttons [get_bd_intf_ports GPIO_0]
set_property name gpio_debug [get_bd_intf_ports GPIO2_0]

# Direcciones AXI del firmware PR.
# PWM:  0x44A00000
# XADC: 0x44A10000

set pwm_segments [get_bd_addr_segs -of_objects     [get_bd_intf_pins control_pwm/s_axi]]

set adc_segments [get_bd_addr_segs -of_objects     [get_bd_intf_pins xadc_wiz_0/s_axi_lite]]

if {[llength $pwm_segments] != 1} {
    error "No se encontro un segmento AXI unico para PWM"
}

if {[llength $adc_segments] != 1} {
    error "No se encontro un segmento AXI unico para XADC"
}

set data_space [get_bd_addr_spaces microblaze_riscv_0/Data]

# Primero trasladar el XADC a su nueva direccion.
set adc_mapped [get_bd_addr_segs -of_objects $data_space     -filter {NAME =~ "*SEG_xadc_wiz_0_Reg"}]

if {[llength $adc_mapped] != 1} {
    error "No se encontro la asignacion original del XADC"
}

set_property offset 0x44A10000 $adc_mapped

# Despues asignar el PWM a la direccion liberada.
assign_bd_address     -offset 0x44A00000     -range 64K     -target_address_space $data_space     $pwm_segments

# Comprobar direcciones efectivas.
set pwm_mapped [get_bd_addr_segs -of_objects $data_space     -filter {NAME =~ "*control_pwm*"}]

puts "MAPA AXI XADC: [get_property OFFSET $adc_mapped]"
if {[llength $pwm_mapped] != 1} {
    error "No se encontro la asignacion AXI del PWM"
}

puts "MAPA AXI PWM:  [get_property OFFSET $pwm_mapped]"

puts "PWM AXI:  0x44A00000"
puts "XADC AXI: 0x44A10000"

# Both LMB ports share the same physical RAM. Capture banks alone use 48 KiB.
foreach space {Data Instruction} controller {dlmb_bram_if_cntlr ilmb_bram_if_cntlr} {
    set seg [get_bd_addr_segs microblaze_riscv_0/$space/SEG_${controller}_Mem]
    set_property range 128K $seg
}
validate_bd_design -force
save_bd_design
set wrapper [make_wrapper -files $bd -top]
add_files -norecurse $wrapper
set_property top system_bd_wrapper [current_fileset]
add_files -fileset constrs_1 [file join $scratch constraints control_pins_pr.xdc]
update_compile_order -fileset sources_1
write_bd_tcl -force [file join $out integrated_bd.tcl]
report_ip_status -file [file join $out ip_status.rpt]
set stream [open [file join $out project_path.txt] w]
puts $stream [file join $scratch p microblaze_v_system.xpr]
close $stream
# Export the real hardware handoff even without an implementation/bitstream.
generate_target all $bd
write_hw_platform -fixed -force -file [file join $scratch nexys4_control_hw.xsa]
file copy -force [file join $scratch nexys4_control_hw.xsa] [file join $out nexys4_control_hw.xsa]
if {$mode ne "validate"} {
    reset_run synth_1
    launch_runs synth_1 -jobs 1
    wait_on_run synth_1
    if {[get_property PROGRESS [get_runs synth_1]] ne "100%"} {error "System synthesis failed"}
    open_run synth_1
    foreach port [get_ports] {
        if {[get_property PACKAGE_PIN $port] eq "" || [get_property IOSTANDARD $port] eq "DEFAULT"} {
            error "Missing physical pin/IOSTANDARD constraint on $port"
        }
    }
    report_utilization -file [file join $out utilization_synth.rpt]
    report_timing_summary -file [file join $out timing_synth.rpt]
    close_design
}
if {$mode eq "bitstream"} {
    launch_runs impl_1 -to_step write_bitstream -jobs 1
    wait_on_run impl_1
    if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {error "Implementation failed"}
    open_run impl_1
    report_timing_summary -file [file join $out timing_impl.rpt]
    # Check actual routed setup/hold slack; successful bit generation alone
    # does not demonstrate clock timing closure. External IO budgets remain a
    # separate board-level requirement.
    foreach delay {max min} {
        set worst [get_timing_paths -delay_type $delay -max_paths 1]
        if {![llength $worst] || [get_property SLACK $worst]<0} {
            error "Routed $delay timing has no passing worst path; inspect timing_impl.rpt"
        }
    }
    write_hw_platform -fixed -include_bit -force -file [file join $scratch nexys4_control.xsa]
    file copy -force [file join $scratch nexys4_control.xsa] [file join $out nexys4_control.xsa]
    set run_dir [get_property DIRECTORY [get_runs impl_1]]
    file copy -force [file join $run_dir system_bd_wrapper.bit] [file join $out nexys4_control.bit]
    file copy -force [file join $run_dir system_bd_wrapper.mmi] [file join $out nexys4_control.mmi]
}
puts "PASS: integrated Nexys4 mode=$mode; physical calibration and timing measurements remain required."
close_project
exit
