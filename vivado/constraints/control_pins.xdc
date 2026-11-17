# Integration pin proposal for original Nexys 4 Rev B; verify wiring before power.
# JA1/2 = leg A, JA3/4 = leg B. JA7 = ISR marker; JA8 = active-high fault.
foreach {port pin} {gate_ah_o B13 gate_al_o F14 gate_bh_o D17 gate_bl_o E17
                   {gpio_debug[0]} G13 {gpio_debug[1]} T8 fault_i C17
                   {user_buttons[0]} E16 {user_buttons[1]} F15 {user_buttons[2]} T16} {
    set_property PACKAGE_PIN $pin [get_ports $port]
    set_property IOSTANDARD LVCMOS33 [get_ports $port]
}
set_property PULLDOWN true [get_ports fault_i]
create_clock -name sys_clock -period 10.000 [get_ports sys_clk_100mhz]
