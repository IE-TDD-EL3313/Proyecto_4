// Integracion sintetizable A+B+C+D del controlador PWM monofasico.
// Este es el TOP que se conecta como esclavo AXI4-Lite al AXI Interconnect.
// Todas las secciones trabajan con el mismo reloj y reset activo en bajo.
module pwm_core #(
    parameter integer CLK_HZ = pwm_config_pkg::CLK_HZ,
    parameter integer FREQ0_HZ = pwm_config_pkg::FREQ0_HZ,
    parameter integer FREQ1_HZ = pwm_config_pkg::FREQ1_HZ,
    parameter integer FREQ2_HZ = pwm_config_pkg::FREQ2_HZ,
    parameter integer PWM_WIDTH = pwm_config_pkg::PWM_WIDTH,
    parameter integer DUTY_SCALE = pwm_config_pkg::DUTY_SCALE,
    parameter integer TRIGGER_CYCLES = pwm_config_pkg::TRIGGER_WIDTH,
    parameter [15:0] DEADTIME_RESET = 16'(pwm_config_pkg::DEADTIME_DEFAULT)
)(
    input  logic        s_axi_aclk,
    input  logic        s_axi_aresetn,

    // AXI4-Lite: direccion de escritura (AW)
    input  logic [5:0]  s_axi_awaddr,
    input  logic        s_axi_awvalid,
    output logic        s_axi_awready,

    // AXI4-Lite: datos de escritura (W)
    input  logic [31:0] s_axi_wdata,
    input  logic [3:0]  s_axi_wstrb,
    input  logic        s_axi_wvalid,
    output logic        s_axi_wready,

    // AXI4-Lite: respuesta de escritura (B)
    output logic [1:0]  s_axi_bresp,
    output logic        s_axi_bvalid,
    input  logic        s_axi_bready,

    // AXI4-Lite: direccion de lectura (AR)
    input  logic [5:0]  s_axi_araddr,
    input  logic        s_axi_arvalid,
    output logic        s_axi_arready,

    // AXI4-Lite: datos de lectura (R)
    output logic [31:0] s_axi_rdata,
    output logic [1:0]  s_axi_rresp,
    output logic        s_axi_rvalid,
    input  logic        s_axi_rready,

    // Salidas hacia los drivers del puente H y el XADC
    output logic pwm_ah_o,
    output logic pwm_al_o,
    output logic pwm_bh_o,
    output logic pwm_bl_o,
    output logic pwm_trigger_o
);

    // Senales internas: A -> B,C,D
    logic enable;
    logic [1:0] freq_sel;
    logic signed [15:0] duty_ref;
    logic [15:0] deadtime_count;
    logic running;

    // Senales internas: B -> C,D
    logic [PWM_WIDTH-1:0] carrier_count;
    logic [PWM_WIDTH-1:0] period_max;
    logic update_event;

    // Senales internas: C -> D
    logic [PWM_WIDTH-1:0] duty_a, duty_b;
    logic pwm_raw_a, pwm_raw_b;

    // A: registros de control accesibles por MicroBlaze mediante AXI.
    pwm_axi_regs #(
        .DEADTIME_RESET(DEADTIME_RESET)
    ) u_axi (
        .s_axi_aclk(s_axi_aclk),
        .s_axi_aresetn(s_axi_aresetn),
        .s_axi_awaddr(s_axi_awaddr),
        .s_axi_awvalid(s_axi_awvalid),
        .s_axi_awready(s_axi_awready),
        .s_axi_wdata(s_axi_wdata),
        .s_axi_wstrb(s_axi_wstrb),
        .s_axi_wvalid(s_axi_wvalid),
        .s_axi_wready(s_axi_wready),
        .s_axi_bresp(s_axi_bresp),
        .s_axi_bvalid(s_axi_bvalid),
        .s_axi_bready(s_axi_bready),
        .s_axi_araddr(s_axi_araddr),
        .s_axi_arvalid(s_axi_arvalid),
        .s_axi_arready(s_axi_arready),
        .s_axi_rdata(s_axi_rdata),
        .s_axi_rresp(s_axi_rresp),
        .s_axi_rvalid(s_axi_rvalid),
        .s_axi_rready(s_axi_rready),
        .running_i(running),
        .enable_o(enable),
        .freq_sel_o(freq_sel),
        .duty_ref_o(duty_ref),
        .deadtime_count_o(deadtime_count)
    );

    // B: portadora triangular. update_event marca fin de periodo.
    pwm_carrier #(
        .CLK_HZ(CLK_HZ),
        .FREQ0_HZ(FREQ0_HZ),
        .FREQ1_HZ(FREQ1_HZ),
        .FREQ2_HZ(FREQ2_HZ),
        .WIDTH(PWM_WIDTH)
    ) u_carrier (
        .clk(s_axi_aclk),
        .rst_n(s_axi_aresetn),
        .enable(enable),
        .freq_sel(freq_sel),
        .carrier_count(carrier_count),
        .period_max(period_max),
        .update_event(update_event),
        .running(running)
    );

    // C: modulacion unipolar; actualiza ambos umbrales con update_event.
    pwm_modulator #(
        .WIDTH(PWM_WIDTH),
        .DUTY_SCALE(DUTY_SCALE)
    ) u_modulator (
        .clk(s_axi_aclk),
        .rst_n(s_axi_aresetn),
        .enable(enable),
        .duty_ref(duty_ref),
        .period_max(period_max),
        .carrier_count(carrier_count),
        .update_event(update_event),
        .duty_a(duty_a),
        .duty_b(duty_b),
        .pwm_raw_a(pwm_raw_a),
        .pwm_raw_b(pwm_raw_b)
    );

    // D: tiempo muerto por brazo y disparo sincronizado del XADC.
    pwm_deadtime #(
        .WIDTH(PWM_WIDTH),
        .TRIGGER_CYCLES(TRIGGER_CYCLES)
    ) u_deadtime (
        .clk(s_axi_aclk),
        .rst_n(s_axi_aresetn),
        .enable(enable),
        .pwm_raw_a(pwm_raw_a),
        .pwm_raw_b(pwm_raw_b),
        .deadtime_count(deadtime_count),
        .carrier_count(carrier_count),
        .period_max(period_max),
        .pwm_ah_o(pwm_ah_o),
        .pwm_al_o(pwm_al_o),
        .pwm_bh_o(pwm_bh_o),
        .pwm_bl_o(pwm_bl_o),
        .pwm_trigger_o(pwm_trigger_o)
    );
endmodule
