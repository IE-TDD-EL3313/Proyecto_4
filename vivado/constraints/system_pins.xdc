# ============================================================
# Proyecto 4 - Taller de Diseño Digital (EL3313)
# Placa: Nexys 4 Rev. B
# Sistema: MicroBlaze V
# ============================================================

# ------------------------------------------------------------
# 1. Reloj principal de 100 MHz
# ------------------------------------------------------------

set_property PACKAGE_PIN E3 [get_ports sys_clk_100mhz]
set_property IOSTANDARD LVCMOS33 [get_ports sys_clk_100mhz]


# ------------------------------------------------------------
# 2. Reset del sistema
# Boton CPU_RESET
# ------------------------------------------------------------

set_property PACKAGE_PIN C12 [get_ports reset_0]
set_property IOSTANDARD LVCMOS33 [get_ports reset_0]


# ------------------------------------------------------------
# 3. Comunicacion UART
# ------------------------------------------------------------

# Recepcion UART: USB-UART hacia FPGA

set_property PACKAGE_PIN C4 [get_ports uart_rtl_0_rxd]
set_property IOSTANDARD LVCMOS33 [get_ports uart_rtl_0_rxd]

# Transmision UART: FPGA hacia USB-UART

set_property PACKAGE_PIN D4 [get_ports uart_rtl_0_txd]
set_property IOSTANDARD LVCMOS33 [get_ports uart_rtl_0_txd]


# ------------------------------------------------------------
# 4. Entradas analogicas XADC
# Canal VAUX3
# ------------------------------------------------------------

# Entrada analogica positiva
set_property PACKAGE_PIN A13 [get_ports {Vaux3_0_v_p}]
set_property IOSTANDARD LVCMOS33 [get_ports {Vaux3_0_v_p}]

# Entrada analogica negativa
set_property PACKAGE_PIN A14 [get_ports {Vaux3_0_v_n}]
set_property IOSTANDARD LVCMOS33 [get_ports {Vaux3_0_v_n}]