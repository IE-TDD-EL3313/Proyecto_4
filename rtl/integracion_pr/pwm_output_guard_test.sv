`timescale 1ns / 1ps

// ============================================================
// Proyecto 4 - Taller de Diseño Digital
// Nexys 4 Rev. B
//
// Conexion directa de salidas PWM para diagnostico con osciloscopio.
//
// IMPORTANTE:
// USO EXCLUSIVO CON OSCILOSCOPIO.
// No conectar drivers ni etapa de potencia.
// Este modulo no incorpora una proteccion independiente.
// ============================================================

module pwm_output_guard_test (
    input  wire pwm_ah_i,
    input  wire pwm_al_i,
    input  wire pwm_bh_i,
    input  wire pwm_bl_i,

    output wire gate_ah_o,
    output wire gate_al_o,
    output wire gate_bh_o,
    output wire gate_bl_o
);

    assign gate_ah_o = pwm_ah_i;
    assign gate_al_o = pwm_al_i;
    assign gate_bh_o = pwm_bh_i;
    assign gate_bl_o = pwm_bl_i;

endmodule
