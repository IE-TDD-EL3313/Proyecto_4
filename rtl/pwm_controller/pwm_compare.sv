// -----------------------------------------------------------------------------
// pwm_compare.sv
// Comparador unipolar - Seccion 7.3
//
// duty_i es el indice de modulacion m en Q1.15 con signo (ya saturado).
//   th_A = N * (2^15 + duty) / 2^16      ->  pierna A se compara con +m
//   th_B = N * (2^15 - duty) / 2^16      ->  pierna B se compara con -m
//   s_A  = (c < th_A),   s_B = (c < th_B)
// Resultado: d_A = (1+m)/2, d_B = (1-m)/2, v_AB promedio = m * Vdc.
// -----------------------------------------------------------------------------
`timescale 1ns / 1ps

module pwm_compare (
    input  logic               clk,
    input  logic               rst_n,
    input  logic        [13:0] c_i,      // portadora
    input  logic        [13:0] n_i,      // pico de la portadora
    input  logic signed [15:0] duty_i,   // Q1.15 (registro sombra)
    output logic               s_a_o,    // 1 = conduce el transistor alto de A
    output logic               s_b_o     // 1 = conduce el transistor alto de B
);

    logic signed [16:0] duty_ext;
    logic        [16:0] ua, ub;          // 2^15 +/- duty  (siempre positivos)
    logic        [30:0] prod_a, prod_b;  // N * (2^15 +/- duty)
    logic        [13:0] th_a, th_b;

    assign duty_ext = {duty_i[15], duty_i};
    assign ua       = 17'(17'sd32768 + duty_ext);
    assign ub       = 17'(17'sd32768 - duty_ext);
    assign prod_a   = n_i * ua;
    assign prod_b   = n_i * ub;

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            th_a  <= '0;
            th_b  <= '0;
            s_a_o <= 1'b0;
            s_b_o <= 1'b0;
        end else begin
            th_a  <= prod_a[29:16];
            th_b  <= prod_b[29:16];
            s_a_o <= (c_i < th_a);
            s_b_o <= (c_i < th_b);
        end
    end

endmodule
