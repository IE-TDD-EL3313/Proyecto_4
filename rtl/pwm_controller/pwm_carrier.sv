// -----------------------------------------------------------------------------
// pwm_carrier.sv
// Generador de portadora triangular (contador up/down) - Seccion 7.2
//
// La portadora recorre 0, 1, ..., N, N-1, ..., 1, 0, 1, ...
// Un periodo completo dura 2N ciclos de reloj: f_sw = f_clk / (2N).
// sync_load_i carga la portadora en sync_phase_i (subiendo) para alinear
// la fase con el AXI Timer (contrato C4).
// -----------------------------------------------------------------------------
`timescale 1ns / 1ps

module pwm_carrier (
    input  logic        clk,
    input  logic        rst_n,
    input  logic        enable_i,      // habilita el generador
    input  logic [13:0] n_i,           // pico de la portadora (registro sombra)
    input  logic        sync_load_i,   // cargar fase (pulso de un ciclo)
    input  logic [13:0] sync_phase_i,  // valor de fase a cargar
    output logic [13:0] c_o,           // valor actual de la portadora
    output logic        valley_o,      // 1 cuando c = 0 (inicio de periodo)
    output logic        peak_o,        // 1 cuando c = N (mitad de periodo)
    output logic        running_o      // 1 mientras el generador esta activo
);

    logic up;  // direccion: 1 = sube, 0 = baja

    always_ff @(posedge clk) begin
        if (!rst_n || !enable_i) begin
            c_o       <= '0;
            up        <= 1'b1;
            running_o <= 1'b0;
        end else if (!running_o) begin
            // Primer ciclo despues de habilitar: arranca en el valle
            running_o <= 1'b1;
            c_o       <= '0;
            up        <= 1'b1;
        end else if (sync_load_i) begin
            // Carga de fase para sincronizar con el AXI Timer
            if (sync_phase_i >= n_i) begin
                c_o <= n_i;
                up  <= 1'b0;
            end else begin
                c_o <= sync_phase_i;
                up  <= 1'b1;
            end
        end else if (up) begin
            if (c_o >= n_i - 14'd1) begin
                c_o <= n_i;
                up  <= 1'b0;
            end else begin
                c_o <= c_o + 14'd1;
            end
        end else begin
            if (c_o <= 14'd1) begin
                c_o <= '0;
                up  <= 1'b1;
            end else begin
                c_o <= c_o - 14'd1;
            end
        end
    end

    assign valley_o = running_o && (c_o == 14'd0);
    assign peak_o   = running_o && (c_o == n_i);

endmodule
