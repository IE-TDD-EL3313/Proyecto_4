# Integracion PR: pwm_output_guard fija las salidas a cero para la
# validacion inicial. No conectar el puente de potencia durante esta etapa.

# ============================================================
# Proyecto 4 - Taller de Diseno Digital
# Nexys 4 Rev. B
# Restricciones de pines para el sistema integrado
# ============================================================

# ------------------------------------------------------------
# Salidas PWM
# ------------------------------------------------------------

# Conector Pmod JA de Nexys 4 Rev. B:
# JA1=B13, JA2=F14, JA3=D17, JA4=E17.

set_property PACKAGE_PIN B13 [get_ports gate_ah_o]
set_property PACKAGE_PIN F14 [get_ports gate_al_o]
set_property PACKAGE_PIN D17 [get_ports gate_bh_o]
set_property PACKAGE_PIN E17 [get_ports gate_bl_o]

# ------------------------------------------------------------
# Salidas de depuracion
# ------------------------------------------------------------

set_property PACKAGE_PIN G13 [get_ports {gpio_debug_tri_o[0]}]
set_property PACKAGE_PIN T8  [get_ports {gpio_debug_tri_o[1]}]

# ------------------------------------------------------------
# Botones de usuario
# ------------------------------------------------------------

set_property PACKAGE_PIN E16 [get_ports {user_buttons_tri_i[0]}]
set_property PACKAGE_PIN F15 [get_ports {user_buttons_tri_i[1]}]
set_property PACKAGE_PIN T16 [get_ports {user_buttons_tri_i[2]}]

# ------------------------------------------------------------
# Estandar electrico
# ------------------------------------------------------------

set_property IOSTANDARD LVCMOS33 [get_ports {
    gate_ah_o
    gate_al_o
    gate_bh_o
    gate_bl_o
    gpio_debug_tri_o[0]
    gpio_debug_tri_o[1]
    user_buttons_tri_i[0]
    user_buttons_tri_i[1]
    user_buttons_tri_i[2]
}]

# El reloj de 100 MHz se define en las restricciones
# originales del proyecto MicroBlaze V.
