// -----------------------------------------------------------------------------
// pwm_deadtime.sv
// Generador de tiempo muerto para una pierna del puente H - Seccion 7.4
//
// En cada flanco de s_i apaga el transistor que conducia, espera dead_time_i
// ciclos con ambas compuertas en 0 y luego enciende el complementario.
// Por construccion gate_h_o y gate_l_o nunca estan en 1 al mismo tiempo.
// precharge_i fuerza el MOSFET inferior encendido (y el superior apagado) para
// cargar el capacitor bootstrap del IR2110; al terminar, la FSM sigue desde
// LOW_ON y respeta el tiempo muerto normalmente.
// Las salidas son registradas (sin glitches hacia el driver).
// -----------------------------------------------------------------------------
`timescale 1ns / 1ps

module pwm_deadtime (
    input  logic       clk,
    input  logic       rst_n,
    input  logic       allow_i,      // 0 = compuertas apagadas (estado OFF)
    input  logic       precharge_i,  // 1 = forzar inferior encendido (bootstrap)
    input  logic       s_i,          // senal ideal de la pierna
    input  logic [9:0] dead_time_i,  // tiempo muerto en ciclos (>= 1)
    output logic       gate_h_o,
    output logic       gate_l_o
);

    typedef enum logic [2:0] {
        OFF        = 3'd0,
        DT_TO_HIGH = 3'd1,
        HIGH_ON    = 3'd2,
        DT_TO_LOW  = 3'd3,
        LOW_ON     = 3'd4
    } state_t;

    state_t     state, state_n;
    logic [9:0] cnt,   cnt_n;

    always_comb begin
        state_n = state;
        cnt_n   = cnt;
        if (precharge_i) begin
            // El superior nunca se enciende durante la precarga. Si estaba
            // encendido (no deberia), primero pasa por el tiempo muerto.
            if (state == HIGH_ON || state == DT_TO_HIGH) begin
                state_n = DT_TO_LOW;
                cnt_n   = '0;
            end else if (state == DT_TO_LOW && cnt < dead_time_i - 10'd1) begin
                cnt_n = cnt + 10'd1;
            end else begin
                state_n = LOW_ON;
                cnt_n   = '0;
            end
        end else
        case (state)
            OFF: begin
                cnt_n   = '0;
                if (s_i) state_n = DT_TO_HIGH;
                else     state_n = DT_TO_LOW;
            end
            DT_TO_HIGH: begin
                if (!s_i) begin
                    state_n = DT_TO_LOW;
                    cnt_n   = '0;
                end else if (cnt >= dead_time_i - 10'd1) begin
                    state_n = HIGH_ON;
                end else begin
                    cnt_n = cnt + 10'd1;
                end
            end
            HIGH_ON: begin
                if (!s_i) begin
                    state_n = DT_TO_LOW;
                    cnt_n   = '0;
                end
            end
            DT_TO_LOW: begin
                if (s_i) begin
                    state_n = DT_TO_HIGH;
                    cnt_n   = '0;
                end else if (cnt >= dead_time_i - 10'd1) begin
                    state_n = LOW_ON;
                end else begin
                    cnt_n = cnt + 10'd1;
                end
            end
            LOW_ON: begin
                if (s_i) begin
                    state_n = DT_TO_HIGH;
                    cnt_n   = '0;
                end
            end
            default: begin
                state_n = OFF;
                cnt_n   = '0;
            end
        endcase
    end

    always_ff @(posedge clk) begin
        if (!rst_n || !allow_i) begin
            state    <= OFF;
            cnt      <= '0;
            gate_h_o <= 1'b0;
            gate_l_o <= 1'b0;
        end else begin
            state    <= state_n;
            cnt      <= cnt_n;
            gate_h_o <= (state_n == HIGH_ON);
            gate_l_o <= (state_n == LOW_ON);
        end
    end

endmodule
