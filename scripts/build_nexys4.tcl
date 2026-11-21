# Rebuild the integrated system from the shared MicroBlaze project.
# vivado -mode batch -source scripts/build_nexys4.tcl -tclargs validate|synth|bitstream
set repo [file normalize [file join [file dirname [info script]] ..]]
set mode [lindex $argv 0]
if {$mode eq ""} {set mode validate}
if {$mode ni {validate synth bitstream}} {error "Use validate, synth or bitstream"}
set_param general.maxThreads 2
set out [file join $repo build nexys4_integrated]
file mkdir $out
set scratch [file normalize [file join $::env(TEMP) tdd4_i_[pid]]]
file mkdir $scratch
file copy [file join $repo vivado microblaze_v_system] [file join $scratch p]
file copy [file join $repo rtl] [file join $scratch rtl]
file copy [file join $repo vivado constraints] [file join $scratch constraints]
cd $scratch
open_project [file join $scratch p microblaze_v_system.xpr]
set bd [get_files */system_bd.bd]
open_bd_design $bd
add_files [glob [file join $scratch rtl pwm_controller *.sv]]
add_files [glob [file join $scratch rtl xadc_if *.sv]]
update_compile_order -fileset sources_1
# Replace the continuous AXI Wizard with the custom atomic snapshot interface.
delete_bd_objs [get_bd_cells xadc_wiz_0]
create_bd_cell -type module -reference pwm_controller_axi control_pwm
create_bd_cell -type module -reference xadc_axi_lite control_adc
set_property CONFIG.DRP_ADDRESS {0x13} [get_bd_cells control_adc]
create_bd_cell -type module -reference xadc_startup_guard adc_guard
create_bd_cell -type ip -vlnv xilinx.com:ip:xadc_wiz:3.3 control_xadc
set_property -dict [list CONFIG.INTERFACE_SELECTION {Enable_DRP} \
    CONFIG.TIMING_MODE {Event} CONFIG.SINGLE_CHANNEL_SELECTION {VAUXP3_VAUXN3} \
    CONFIG.DCLK_FREQUENCY {100} CONFIG.ADC_CONVERSION_RATE {1000} \
    CONFIG.CHANNEL_AVERAGING {None} CONFIG.ENABLE_RESET {true} \
    CONFIG.ENABLE_CONVST {true} CONFIG.ENABLE_BUSY {true} CONFIG.ENABLE_EOC {true}] [get_bd_cells control_xadc]
set_property CONFIG.NUM_MI {6} [get_bd_cells microblaze_riscv_0_axi_periph]
set_property CONFIG.enable_timer2 {1} [get_bd_cells axi_timer_0]
set_property -dict [list CONFIG.C_GPIO_WIDTH {3} CONFIG.C_GPIO2_WIDTH {2} \
    CONFIG.C_ALL_INPUTS {1} CONFIG.C_ALL_OUTPUTS_2 {1} CONFIG.C_INTERRUPT_PRESENT {0}] [get_bd_cells axi_gpio_0]
proc wire_control {a b} {connect_bd_net [get_bd_pins $a] [get_bd_pins $b]}
foreach pin {control_pwm/s_axi_aclk control_adc/s_axi_aclk adc_guard/clk control_xadc/dclk_in} {
    wire_control clk_wiz_1/clk_out1 $pin
}
foreach pin {control_pwm/s_axi_aresetn control_adc/s_axi_aresetn adc_guard/rst_n} {
    wire_control rst_clk_wiz_1_100M/peripheral_aresetn $pin
}
connect_bd_intf_net [get_bd_intf_pins microblaze_riscv_0_axi_periph/M04_AXI] [get_bd_intf_pins control_adc/s_axi]
connect_bd_intf_net [get_bd_intf_pins microblaze_riscv_0_axi_periph/M05_AXI] [get_bd_intf_pins control_pwm/s_axi]
wire_control axi_timer_0/generateout0 control_pwm/sync_start_i
wire_control control_pwm/adc_trigger_o control_adc/pwm_trigger_i
wire_control control_adc/reset_request_o adc_guard/reset_request_i
wire_control adc_guard/xadc_reset_o control_xadc/reset_in
wire_control adc_guard/ready_o control_adc/xadc_ready_i
foreach {a b} {convst_o convst_in den_o den_in dwe_o dwe_in daddr_o daddr_in} {
    wire_control control_adc/$a control_xadc/$b
}
foreach {a b} {busy_out xadc_busy_i eoc_out xadc_eoc_i drdy_out xadc_drdy_i do_out xadc_do_i} {
    wire_control control_xadc/$a control_adc/$b
}
wire_control control_xadc/busy_out adc_guard/xadc_busy_i
create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 adc_zero
set_property -dict [list CONFIG.CONST_WIDTH {16} CONFIG.CONST_VAL {0}] [get_bd_cells adc_zero]
wire_control adc_zero/dout control_xadc/di_in
connect_bd_intf_net [get_bd_intf_ports Vaux3_0] [get_bd_intf_pins control_xadc/Vaux3]
# The selected channel is VAUX3; unused dedicated VP/VN inputs are tied low.
create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 analog_zero
set_property CONFIG.CONST_VAL {0} [get_bd_cells analog_zero]
foreach pin {vp_in vn_in} {wire_control analog_zero/dout control_xadc/$pin}
foreach pin {gate_ah_o gate_al_o gate_bh_o gate_bl_o} {
    set port [create_bd_port -dir O $pin]
    connect_bd_net $port [get_bd_pins control_pwm/$pin]
}
connect_bd_net [create_bd_port -dir I fault_i] [get_bd_pins control_pwm/fault_i]
connect_bd_net [create_bd_port -dir I -from 2 -to 0 user_buttons] [get_bd_pins axi_gpio_0/gpio_io_i]
connect_bd_net [create_bd_port -dir O -from 1 -to 0 gpio_debug] [get_bd_pins axi_gpio_0/gpio2_io_o]
foreach {cell offset} {control_adc 0x44A00000 control_pwm 0x44A10000} {
    set segments [get_bd_addr_segs -of_objects [get_bd_intf_pins $cell/s_axi]]
    puts "INTEGRATION_ADDRESS_SEGMENTS $cell: $segments"
    if {[llength $segments]!=1} {error "Expected one AXI register segment on $cell"}
    assign_bd_address -offset $offset -range 64K -target_address_space [get_bd_addr_spaces microblaze_riscv_0/Data] $segments
}
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
add_files -fileset constrs_1 [file join $scratch constraints control_pins.xdc]
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
