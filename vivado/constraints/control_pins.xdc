# ============================================================
# Proyecto 4 - Taller de Diseno Digital
# Nexys 4 Rev. B
# Restricciones de pines para el sistema integrado
# ============================================================

# ------------------------------------------------------------
# Salidas PWM
# ------------------------------------------------------------

set_property PACKAGE_PIN B13 [get_ports gate_ah_o]
set_property PACKAGE_PIN F14 [get_ports gate_al_o]
set_property PACKAGE_PIN D17 [get_ports gate_bh_o]
set_property PACKAGE_PIN E17 [get_ports gate_bl_o]

# ------------------------------------------------------------
# Salidas de depuracion
# ------------------------------------------------------------

set_property PACKAGE_PIN G13 [get_ports {gpio_debug[0]}]
set_property PACKAGE_PIN T8  [get_ports {gpio_debug[1]}]

# ------------------------------------------------------------
# Entrada de falla
# ------------------------------------------------------------

set_property PACKAGE_PIN C17 [get_ports fault_i]
set_property PULLDOWN true [get_ports fault_i]

# ------------------------------------------------------------
# Botones de usuario
# ------------------------------------------------------------

set_property PACKAGE_PIN E16 [get_ports {user_buttons[0]}]
set_property PACKAGE_PIN F15 [get_ports {user_buttons[1]}]
set_property PACKAGE_PIN T16 [get_ports {user_buttons[2]}]

# ------------------------------------------------------------
# Estandar electrico
# ------------------------------------------------------------

set_property IOSTANDARD LVCMOS33 [get_ports {
    gate_ah_o
    gate_al_o
    gate_bh_o
    gate_bl_o
    gpio_debug[0]
    gpio_debug[1]
    fault_i
    user_buttons[0]
    user_buttons[1]
    user_buttons[2]
}]

# El reloj de 100 MHz se define en las restricciones
# originales del proyecto MicroBlaze V.
