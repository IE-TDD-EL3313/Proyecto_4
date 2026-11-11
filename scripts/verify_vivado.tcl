# Usage: vivado -mode batch -source scripts/verify_vivado.tcl -tclargs nexys4
# Target selection checks only the acquisition module, not a complete board.
set repo [file normalize [file join [file dirname [info script]] ..]]
set target [lindex $argv 0]
if {$target eq ""} { set target nexys4 }
set mode [lindex $argv 1]
if {$mode eq ""} { set mode all }
if {$mode ni {all ips synth}} { error "Mode must be all, ips or synth" }
set_param general.maxThreads 1
switch -- $target {
    basys3 { set part xc7a35tcpg236-1 }
    nexys4 { set part xc7a100tcsg324-1 }
    default { error "Target must be basys3 or nexys4; confirm the physical board before assigning pins" }
}
set exported [file join $repo build vivado $target]
file mkdir $exported
# Windows IP generation exceeds MAX_PATH inside the long OneDrive repository.
# Keep generated projects/helper processes in a short, unique temp directory.
set scratch [file normalize [file join $::env(TEMP) tdd4_${target}_[pid]]]
file mkdir $scratch
cd $scratch
set out [file join $scratch reports]
file mkdir $out
puts "Temporary Vivado work directory: $scratch"
if {$mode ne "ips"} {
    create_project -in_memory -part $part
    file mkdir [file join $scratch rtl]
    set rtl_files {}
    foreach name {xadc_acquisition.sv xadc_axi_lite.sv} {
        set copied [file join $scratch rtl $name]
        file copy [file join $repo rtl xadc_if $name] $copied
        lappend rtl_files $copied
    }
    read_verilog -sv $rtl_files
    synth_design -top xadc_axi_lite -part $part -mode out_of_context
    create_clock -name axi_clk -period 10 [get_ports s_axi_aclk]
    report_utilization -file [file join $out utilization_synth.rpt]
    report_timing_summary -file [file join $out timing_synth.rpt]
    write_checkpoint -force [file join $out xadc_synth.dcp]
    close_project
}
# A separate sandbox validates IP names/properties; it is not the system BD.
if {$mode ne "synth"} {
    create_project control_ip_probe [file join $scratch ip_probe] -part $part
    create_bd_design control_ip_probe
    source [file join $repo scripts configure_control_peripherals.tcl]
    create_control_peripherals
    write_bd_tcl -force [file join $out control_peripherals.tcl]
    close_project
}
foreach generated [glob -nocomplain -directory $out *] {
    file copy -force $generated $exported
}
puts "PASS: verification mode=$mode target=$part. Full BD integration is pending."
