// Seccion D: accionamiento seguro de los dos brazos y disparo del XADC.
// Sintetizable (SystemVerilog); reloj unico, entradas sincronas.
// raw=1 solicita transistor superior; raw=0 solicita inferior.
module pwm_deadtime_leg (
    input  logic        clk,
    input  logic        rst_n,
    input  logic        enable,
    input  logic        pwm_raw,
    input  logic [15:0] deadtime_count,
    output logic        gate_high,
    output logic        gate_low
);
    logic [15:0] delay_left;
    logic        pending_raw;
    logic        armed;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            gate_high   <= 1'b0;
            gate_low    <= 1'b0;
            delay_left  <= '0;
            pending_raw <= 1'b0;
            armed       <= 1'b0;
        end else if (!enable) begin
            gate_high   <= 1'b0;
            gate_low    <= 1'b0;
            delay_left  <= '0;
            pending_raw <= pwm_raw;
            armed       <= 1'b0;
        end else if (!armed) begin
            // Arranque: ambos MOSFET apagados durante el tiempo muerto.
            gate_high   <= 1'b0;
            gate_low    <= 1'b0;
            delay_left  <= deadtime_count;
            pending_raw <= pwm_raw;
            armed       <= 1'b1;
        end else if (pwm_raw != pending_raw) begin
            // Ante una nueva orden, apagar ambos inmediatamente (en el reloj).
            // Si cambia de nuevo durante la espera, reiniciar el intervalo.
            gate_high   <= 1'b0;
            gate_low    <= 1'b0;
            delay_left  <= deadtime_count;
            pending_raw <= pwm_raw;
        end else if (gate_high || gate_low) begin
            // Mantener el dispositivo solicitado encendido.
            gate_high <= pending_raw;
            gate_low  <= ~pending_raw;
        end else if (delay_left != 16'd0) begin
            // Ambos apagados durante el intervalo de tiempo muerto.
            delay_left <= delay_left - 16'd1;
        end else begin
            // Encender solo el dispositivo solicitado.
            gate_high <= pending_raw;
            gate_low  <= ~pending_raw;
        end
    end
endmodule

module pwm_deadtime #(
    parameter integer WIDTH = pwm_config_pkg::PWM_WIDTH,
    parameter integer TRIGGER_CYCLES = pwm_config_pkg::TRIGGER_WIDTH
)(
    input  logic                 clk,
    input  logic                 rst_n,
    input  logic                 enable,
    input  logic                 pwm_raw_a,
    input  logic                 pwm_raw_b,
    input  logic [15:0]          deadtime_count,
    input  logic [WIDTH-1:0]     carrier_count,
    input  logic [WIDTH-1:0]     period_max,
    output logic                 pwm_ah_o,
    output logic                 pwm_al_o,
    output logic                 pwm_bh_o,
    output logic                 pwm_bl_o,
    output logic                 pwm_trigger_o
);
    logic gate_ah, gate_al, gate_bh, gate_bl;

    pwm_deadtime_leg u_leg_a (
        .clk(clk), .rst_n(rst_n), .enable(enable),
        .pwm_raw(pwm_raw_a), .deadtime_count(deadtime_count),
        .gate_high(gate_ah), .gate_low(gate_al)
    );

    pwm_deadtime_leg u_leg_b (
        .clk(clk), .rst_n(rst_n), .enable(enable),
        .pwm_raw(pwm_raw_b), .deadtime_count(deadtime_count),
        .gate_high(gate_bh), .gate_low(gate_bl)
    );

    // Apagado inmediato ante enable=0 o reset, aun entre flancos de reloj.
    always_comb begin
        pwm_ah_o = rst_n && enable && gate_ah;
        pwm_al_o = rst_n && enable && gate_al;
        pwm_bh_o = rst_n && enable && gate_bh;
        pwm_bl_o = rst_n && enable && gate_bl;
    end

    // Disparo a la llegada al maximo de la portadora (una vez por periodo).
    // Por defecto dura 8 ciclos de reloj; no es un evento de ADC terminado.
    localparam integer TRIG_W = (TRIGGER_CYCLES > 1) ?
                                 $clog2(TRIGGER_CYCLES + 1) : 1;
    logic [TRIG_W-1:0] trigger_left;
    logic peak_prev;
    logic peak_now;
    assign peak_now = (carrier_count == period_max) && (period_max != '0);

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            peak_prev      <= 1'b0;
            trigger_left   <= '0;
            pwm_trigger_o  <= 1'b0;
        end else if (!enable) begin
            peak_prev      <= 1'b0;
            trigger_left   <= '0;
            pwm_trigger_o  <= 1'b0;
        end else begin
            peak_prev <= peak_now;
            if (peak_now && !peak_prev) begin
                pwm_trigger_o <= 1'b1;
                trigger_left  <= TRIG_W'(TRIGGER_CYCLES-1);
            end else if (trigger_left != '0) begin
                pwm_trigger_o <= 1'b1;
                trigger_left  <= trigger_left - 1'b1;
            end else begin
                pwm_trigger_o <= 1'b0;
            end
        end
    end
endmodule
