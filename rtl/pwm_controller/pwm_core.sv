// -----------------------------------------------------------------------------
// pwm_core.sv
// Nucleo del PWM: registros sombra, control de compuertas y subbloques.
//
// - Los valores escritos por software (freq_sel, duty, dead_time) se copian a
//   registros sombra solo en el valle de la portadora (o mientras esta detenida).
// - enable_i     : corre la portadora y el trigger del ADC.
// - gates_en_i   : permite que las compuertas conmuten. Se liberan en el
//                  siguiente PICO de la portadora y se bloquean de inmediato
//                  (contrato C4). En el pico s_a = s_b = 0, asi que lo primero
//                  que se enciende son los MOSFET inferiores: se carga el
//                  capacitor bootstrap del IR2110 antes del primer pulso alto.
//                  Ademas, al liberar las compuertas se mantienen los dos
//                  inferiores encendidos PRECHARGE_CYC ciclos (precarga).
// - sync_start_i : primer flanco tras sync_arm_i -> carga sync_phase_i,
//                  solo con compuertas bloqueadas. Se rearma al detener PWM.
// - fault_i      : falla externa (asincrona). Bloquea las compuertas.
// -----------------------------------------------------------------------------
`timescale 1ns / 1ps

module pwm_core #(
    parameter logic [13:0] N_00            = 14'd5000,   // 10 kHz
    parameter logic [13:0] N_01            = 14'd3333,   // 15 kHz (15.0015 kHz)
    parameter logic [13:0] N_10            = 14'd2500,   // 20 kHz
    parameter logic [13:0] N_11            = 14'd2500,   // 20 kHz (repetido)
    parameter int          TRIG_WIDTH      = 4,
    parameter int          PRECHARGE_CYC   = 2000,       // 20 us de precarga bootstrap
    parameter bit          GATE_ACTIVE_LOW = 1'b0
) (
    input  logic               clk,
    input  logic               rst_n,
    // Configuracion desde los registros AXI
    input  logic               enable_i,
    input  logic               gates_en_i,
    input  logic               sync_arm_i,
    input  logic        [1:0]  freq_sel_i,
    input  logic signed [15:0] duty_i,
    input  logic        [9:0]  dead_time_i,
    input  logic        [13:0] sync_phase_i,
    // Entradas externas
    input  logic               sync_start_i,
    input  logic               fault_i,
    // Salidas
    output logic               gate_ah_o,
    output logic               gate_al_o,
    output logic               gate_bh_o,
    output logic               gate_bl_o,
    output logic               pwm_trigger_o,
    // Estado hacia los registros AXI
    output logic               running_o,
    output logic               gates_on_o,
    output logic               fault_o,
    output logic               sync_done_o,
    output logic               precharge_o,
    output logic               power_ready_o,
    output logic [13:0]        phase_limit_o
);

    // ---------------- Registros sombra ----------------
    logic        [13:0] n_sh;
    logic signed [15:0] duty_sh;
    logic        [9:0]  dt_sh;
    logic        [13:0] n_sel;
    logic        [13:0] c;
    logic               valley, peak;

    always_comb begin
        case (freq_sel_i)
            2'b00:   n_sel = N_00;
            2'b01:   n_sel = N_01;
            2'b10:   n_sel = N_10;
            default: n_sel = N_11;
        endcase
    end
    assign phase_limit_o = n_sel;

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            n_sh    <= N_00;
            duty_sh <= '0;
            dt_sh   <= 10'd100;
        end else if (!running_o || valley) begin
            n_sh    <= n_sel;
            duty_sh <= duty_i;
            dt_sh   <= dead_time_i;
        end
    end

    // ---------------- Sincronizador de la falla externa ----------------
    logic fault_meta, fault_s;
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            fault_meta <= 1'b0;
            fault_s    <= 1'b0;
        end else begin
            fault_meta <= fault_i;
            fault_s    <= fault_meta;
        end
    end
    assign fault_o = fault_s;

    // ---------------- Deteccion de flanco de sync_start_i ----------------
    logic sync_prev, sync_load;
    always_ff @(posedge clk) begin
        if (!rst_n) sync_prev <= 1'b0;
        else        sync_prev <= sync_start_i;
    end
    // Solo se acepta con las compuertas bloqueadas, para no generar pulsos raros
    assign sync_load = enable_i && running_o && sync_arm_i && !sync_done_o &&
                       sync_start_i && !sync_prev && !gates_en_i && !gates_on_o &&
                       sync_phase_i<n_sh;
    always_ff @(posedge clk) begin
        if (!rst_n || !enable_i) sync_done_o <= 1'b0;
        else if (sync_load) sync_done_o <= 1'b1;
    end

    // ---------------- Control de compuertas ----------------
    logic block;  // condicion de bloqueo inmediato
    assign block = !enable_i || !gates_en_i || fault_s || !running_o;

    always_ff @(posedge clk) begin
        if (!rst_n || block) gates_on_o <= 1'b0;
        else if (peak)       gates_on_o <= 1'b1;   // se liberan en el pico (bootstrap)
    end

    // ---------------- Subbloques ----------------
    pwm_carrier u_carrier (
        .clk          (clk),
        .rst_n        (rst_n),
        .enable_i     (enable_i),
        .n_i          (n_sh),
        .sync_load_i  (sync_load),
        .sync_phase_i (sync_phase_i),
        .c_o          (c),
        .valley_o     (valley),
        .peak_o       (peak),
        .running_o    (running_o)
    );

    logic s_a, s_b;
    pwm_compare u_compare (
        .clk    (clk),
        .rst_n  (rst_n),
        .c_i    (c),
        .n_i    (n_sh),
        .duty_i (duty_sh),
        .s_a_o  (s_a),
        .s_b_o  (s_b)
    );

    logic allow;
    assign allow = gates_on_o && !block;

    // ---------------- Precarga del bootstrap ----------------
    logic        gates_on_q;
    logic [15:0] pre_cnt;
    logic        precharge;
    always_ff @(posedge clk) begin
        if (!rst_n || !allow) begin
            gates_on_q <= 1'b0;
            pre_cnt    <= '0;
        end else begin
            gates_on_q <= 1'b1;
            if (!gates_on_q)       pre_cnt <= 16'(PRECHARGE_CYC);  // recien liberadas
            else if (pre_cnt != 0) pre_cnt <= pre_cnt - 16'd1;
        end
    end
    assign precharge = allow && (!gates_on_q || pre_cnt != 0);
    assign precharge_o = precharge;
    assign power_ready_o = allow && !precharge;

    logic ah, al, bh, bl;
    pwm_deadtime u_dt_a (
        .clk         (clk),
        .rst_n       (rst_n),
        .allow_i     (allow),
        .precharge_i (precharge),
        .s_i         (s_a),
        .dead_time_i (dt_sh),
        .gate_h_o    (ah),
        .gate_l_o    (al)
    );

    pwm_deadtime u_dt_b (
        .clk         (clk),
        .rst_n       (rst_n),
        .allow_i     (allow),
        .precharge_i (precharge),
        .s_i         (s_b),
        .dead_time_i (dt_sh),
        .gate_h_o    (bh),
        .gate_l_o    (bl)
    );

    pwm_trigger #(.TRIG_WIDTH(TRIG_WIDTH)) u_trigger (
        .clk       (clk),
        .rst_n     (rst_n),
        .running_i (running_o),
        .valley_i  (valley),
        .trigger_o (pwm_trigger_o)
    );

    // Polaridad de salida segun el driver de compuertas
    assign gate_ah_o = ah ^ GATE_ACTIVE_LOW;
    assign gate_al_o = al ^ GATE_ACTIVE_LOW;
    assign gate_bh_o = bh ^ GATE_ACTIVE_LOW;
    assign gate_bl_o = bl ^ GATE_ACTIVE_LOW;

endmodule
