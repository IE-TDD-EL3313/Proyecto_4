`timescale 1ns / 1ps

// ============================================================
// Proyecto 4 - Taller de Diseño Digital
// Nexys 4 Rev. B
//
// Inhibición de salidas PWM para validación inicial.
//
// IMPORTANTE:
// Esta versión mantiene las cuatro salidas permanentemente
// deshabilitadas. No constituye todavía una protección
// completa para operación con el puente de potencia.
// ============================================================

module pwm_output_guard (
    input  wire pwm_ah_i,
    input  wire pwm_al_i,
    input  wire pwm_bh_i,
    input  wire pwm_bl_i,

    output wire gate_ah_o,
    output wire gate_al_o,
    output wire gate_bh_o,
    output wire gate_bl_o
);

    assign gate_ah_o = 1'b0;
    assign gate_al_o = 1'b0;
    assign gate_bh_o = 1'b0;
    assign gate_bl_o = 1'b0;

endmodule
