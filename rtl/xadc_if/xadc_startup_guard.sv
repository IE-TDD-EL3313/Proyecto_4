`timescale 1ns/1ps
// Coordinated hardware/software reset of the event-mode XADC and its wrapper.
// Startup allowance is configurable; analog settling still requires board validation.
module xadc_startup_guard #(
    parameter integer RESET_CYCLES=8,
    parameter integer STARTUP_CYCLES=100000
) (
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_RESET rst_n, FREQ_HZ 100000000" *) input wire clk,
    input wire rst_n,
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_HIGH" *) input wire reset_request_i,
    input wire xadc_busy_i,
    output wire xadc_reset_o,
    output reg ready_o
);
    reg [31:0] count;
    always @(posedge clk) begin
        if (!rst_n || reset_request_i) begin count<=0;ready_o<=0;end
        else if (count<STARTUP_CYCLES) count<=count+1'b1;
        else if (!xadc_busy_i) ready_o<=1;
    end
    assign xadc_reset_o=!rst_n || reset_request_i || count<RESET_CYCLES;
endmodule
