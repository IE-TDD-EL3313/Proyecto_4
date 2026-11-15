// -----------------------------------------------------------------------------
// pwm_controller_axi.sv
// Top del periferico PWM personalizado (PWM Controller) - Seccion 6.2
// Proyecto 4 - EL3313 Taller de Diseno Digital
//
// Modulacion PWM unipolar con tiempo muerto configurable para un puente H,
// interfaz esclava AXI4-Lite y senal de sincronizacion pwm_trigger_o hacia el
// ADC. Un solo dominio de reloj (s_axi_aclk = 100 MHz).
//
// Etapa de potencia (PEDP): 2 x IR2110 (HIN/LIN activos en alto, sin tiempo
// muerto interno, bootstrap en el lado alto) y 4 x IRF540N.
//   gate_ah_o -> HIN IR2110 pierna A (FETD4)   gate_al_o -> LIN pierna A (FETD2)
//   gate_bh_o -> HIN IR2110 pierna B (FETD1)   gate_bl_o -> LIN pierna B (FETD3)
//   v_AB = v_A - v_B; corriente positiva de A hacia B.
// -----------------------------------------------------------------------------
`timescale 1ns / 1ps

module pwm_controller_axi #(
    parameter int                 C_S_AXI_DATA_WIDTH = 32,
    parameter int                 C_S_AXI_ADDR_WIDTH = 4,
    parameter logic        [13:0] N_00            = 14'd5000,   // 10 kHz
    parameter logic        [13:0] N_01            = 14'd3333,   // 15 kHz (15.0015 kHz)
    parameter logic        [13:0] N_10            = 14'd2500,   // 20 kHz
    parameter logic        [13:0] N_11            = 14'd2500,   // 20 kHz (repetido)
    parameter logic signed [15:0] DUTY_MAX        = 16'sd31129, // ~0.95
    parameter logic        [9:0]  DT_MIN          = 10'd20,
    parameter logic        [9:0]  DT_MAX          = 10'd1023,
    parameter logic        [9:0]  DT_RESET        = 10'd100,    // 1 us (confirmado con PEDP)
    parameter logic        [13:0] PHASE_RESET     = 14'd1000,
    parameter int                 TRIG_WIDTH      = 4,
    parameter int                 PRECHARGE_CYC   = 2000,       // 20 us de precarga bootstrap
    parameter bit                 GATE_ACTIVE_LOW = 1'b0        // IR2110: HIN/LIN activos en alto
) (
    // Reloj y reset
    input  logic                            s_axi_aclk,
    input  logic                            s_axi_aresetn,
    // AXI4-Lite esclavo
    input  logic [C_S_AXI_ADDR_WIDTH-1:0]   s_axi_awaddr,
    input  logic [2:0]                      s_axi_awprot,
    input  logic                            s_axi_awvalid,
    output logic                            s_axi_awready,
    input  logic [C_S_AXI_DATA_WIDTH-1:0]   s_axi_wdata,
    input  logic [C_S_AXI_DATA_WIDTH/8-1:0] s_axi_wstrb,
    input  logic                            s_axi_wvalid,
    output logic                            s_axi_wready,
    output logic [1:0]                      s_axi_bresp,
    output logic                            s_axi_bvalid,
    input  logic                            s_axi_bready,
    input  logic [C_S_AXI_ADDR_WIDTH-1:0]   s_axi_araddr,
    input  logic [2:0]                      s_axi_arprot,
    input  logic                            s_axi_arvalid,
    output logic                            s_axi_arready,
    output logic [C_S_AXI_DATA_WIDTH-1:0]   s_axi_rdata,
    output logic [1:0]                      s_axi_rresp,
    output logic                            s_axi_rvalid,
    input  logic                            s_axi_rready,
    // Sincronizacion y proteccion
    input  logic                            sync_start_i,  // generateout0 del AXI Timer
    input  logic                            fault_i,       // falla externa (0 si no se usa)
    // Salidas
    output logic                            gate_ah_o,
    output logic                            gate_al_o,
    output logic                            gate_bh_o,
    output logic                            gate_bl_o,
    output logic                            pwm_trigger_o
);

    logic               enable, gates_en, running, gates_on, fault;
    logic        [1:0]  freq_sel;
    logic signed [15:0] duty;
    logic        [9:0]  dead_time;
    logic        [13:0] sync_phase;

    pwm_axi_regs #(
        .ADDR_W      (C_S_AXI_ADDR_WIDTH),
        .DUTY_MAX    (DUTY_MAX),
        .DT_MIN      (DT_MIN),
        .DT_MAX      (DT_MAX),
        .DT_RESET    (DT_RESET),
        .PHASE_RESET (PHASE_RESET)
    ) u_regs (
        .clk          (s_axi_aclk),
        .rst_n        (s_axi_aresetn),
        .awaddr       (s_axi_awaddr),
        .awvalid      (s_axi_awvalid),
        .awready      (s_axi_awready),
        .wdata        (s_axi_wdata),
        .wstrb        (s_axi_wstrb),
        .wvalid       (s_axi_wvalid),
        .wready       (s_axi_wready),
        .bresp        (s_axi_bresp),
        .bvalid       (s_axi_bvalid),
        .bready       (s_axi_bready),
        .araddr       (s_axi_araddr),
        .arvalid      (s_axi_arvalid),
        .arready      (s_axi_arready),
        .rdata        (s_axi_rdata),
        .rresp        (s_axi_rresp),
        .rvalid       (s_axi_rvalid),
        .rready       (s_axi_rready),
        .enable_o     (enable),
        .gates_en_o   (gates_en),
        .freq_sel_o   (freq_sel),
        .duty_o       (duty),
        .dead_time_o  (dead_time),
        .sync_phase_o (sync_phase),
        .running_i    (running),
        .gates_on_i   (gates_on),
        .fault_i      (fault)
    );

    pwm_core #(
        .N_00            (N_00),
        .N_01            (N_01),
        .N_10            (N_10),
        .N_11            (N_11),
        .TRIG_WIDTH      (TRIG_WIDTH),
        .PRECHARGE_CYC   (PRECHARGE_CYC),
        .GATE_ACTIVE_LOW (GATE_ACTIVE_LOW)
    ) u_core (
        .clk           (s_axi_aclk),
        .rst_n         (s_axi_aresetn),
        .enable_i      (enable),
        .gates_en_i    (gates_en),
        .freq_sel_i    (freq_sel),
        .duty_i        (duty),
        .dead_time_i   (dead_time),
        .sync_phase_i  (sync_phase),
        .sync_start_i  (sync_start_i),
        .fault_i       (fault_i),
        .gate_ah_o     (gate_ah_o),
        .gate_al_o     (gate_al_o),
        .gate_bh_o     (gate_bh_o),
        .gate_bl_o     (gate_bl_o),
        .pwm_trigger_o (pwm_trigger_o),
        .running_o     (running),
        .gates_on_o    (gates_on),
        .fault_o       (fault)
    );

endmodule
