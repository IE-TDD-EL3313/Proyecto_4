# Source from an open Block Design; clock, reset, AXI, IRQ and pins are connected
# by the platform integrator. Existing cells are never silently reconfigured.
proc set_control_parameters {cell settings} {
    set properties [list_property $cell]
    foreach {key value} $settings {
        if {$key ni $properties} { error "Unsupported IP parameter $key on $cell" }
    }
    set_property -dict $settings $cell
    foreach {key expected} $settings {
        set actual [get_property $key $cell]
        if {$actual eq "true"} { set actual 1 }
        if {$actual eq "false"} { set actual 0 }
        if {$actual ne $expected} { error "Parameter readback mismatch: $cell $key=$actual, expected $expected" }
    }
}
proc create_control_peripherals {} {
    if {[current_bd_design -quiet] eq ""} { error "Open a Block Design first" }
    foreach name {control_timer capture_uart user_gpio} {
        if {[llength [get_bd_cells -quiet $name]]} { error "Cell already exists: $name" }
    }
    create_bd_cell -type ip -vlnv xilinx.com:ip:axi_timer:2.0 control_timer
    set_control_parameters [get_bd_cells control_timer] [list CONFIG.COUNT_WIDTH {32} CONFIG.enable_timer2 {1}]
    create_bd_cell -type ip -vlnv xilinx.com:ip:axi_uartlite:2.0 capture_uart
    set_control_parameters [get_bd_cells capture_uart] [list CONFIG.C_BAUDRATE {115200} CONFIG.C_DATA_BITS {8} CONFIG.C_USE_PARITY {0}]
    create_bd_cell -type ip -vlnv xilinx.com:ip:axi_gpio:2.0 user_gpio
    set_control_parameters [get_bd_cells user_gpio] [list CONFIG.C_IS_DUAL {1} CONFIG.C_GPIO_WIDTH {3} CONFIG.C_ALL_INPUTS {1} \
        CONFIG.C_GPIO2_WIDTH {1} CONFIG.C_ALL_OUTPUTS_2 {1} CONFIG.C_INTERRUPT_PRESENT {0}]
    save_bd_design
    puts "Configured Timer/UART/GPIO. Connections, address assignment and validate_bd_design remain required."
}
