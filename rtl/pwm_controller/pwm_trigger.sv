// -----------------------------------------------------------------------------
// pwm_trigger.sv
// Generador de pwm_trigger_o - Seccion 7.5
//
// Pulso de TRIG_WIDTH ciclos que empieza un ciclo despues del valle de la
// portadora. Un pulso por periodo; ninguno si el generador esta detenido.
// -----------------------------------------------------------------------------
`timescale 1ns / 1ps

module pwm_trigger #(
    parameter int TRIG_WIDTH = 4
) (
    input  logic clk,
    input  logic rst_n,
    input  logic running_i,
    input  logic valley_i,
    output logic trigger_o
);

    logic [3:0] cnt;

    always_ff @(posedge clk) begin
        if (!rst_n || !running_i) begin
            cnt       <= '0;
            trigger_o <= 1'b0;
        end else if (valley_i) begin
            cnt       <= 4'(TRIG_WIDTH);
            trigger_o <= 1'b1;
        end else begin
            if (cnt != 4'd0) cnt <= cnt - 4'd1;
            trigger_o <= (cnt > 4'd1);
        end
    end

endmodule
