`timescale 1ns/1ps
// DCLK-domain acquisition engine. XADC configuration/analog pins are external.
module xadc_acquisition #(
    parameter integer CONVST_CYCLES = 4,
    parameter integer SETTLE_CYCLES = 20,
    parameter integer TIMEOUT_CYCLES = 800,
    parameter [6:0] DRP_ADDRESS = 7'h10
) (
    input wire clk, rst_n,
    input wire enable_i, sync_mode_i, start_sw_i, pwm_trigger_i,
    input wire clear_overrun_i, clear_timeout_i,
    input wire xadc_ready_i, xadc_busy_i, xadc_eoc_i, xadc_drdy_i,
    input wire [15:0] xadc_do_i,
    output reg convst_o, den_o,
    output wire dwe_o,
    output wire [6:0] daddr_o,
    output reg valid_o, overrun_o, timeout_o,
    output wire busy_o,
    output reg [11:0] data_o,
    output reg [31:0] seq_o, time_o, ticks_o
);
    localparam IDLE=0, PULSE=1, WAIT_EOC=2, ISSUE_READ=3,
               WAIT_DRP=4, PUBLISH=5, ERROR=6;
    reg [2:0] state;
    reg trigger_previous, trigger_armed;
    reg [31:0] quiet_cycles, pulse_cycles, watchdog, accepted_time;
    reg [11:0] pending_data;
    wire trigger_edge = trigger_armed && pwm_trigger_i && !trigger_previous;
    wire request = sync_mode_i ? trigger_edge : start_sw_i;
    wire available = xadc_ready_i && !xadc_busy_i && !xadc_drdy_i &&
                     !xadc_eoc_i && quiet_cycles >= SETTLE_CYCLES;
    assign busy_o = state != IDLE;
    assign dwe_o = 1'b0;
    assign daddr_o = DRP_ADDRESS;

    always @(posedge clk) begin
        if (!rst_n) begin
            state <= IDLE; trigger_previous <= 0; trigger_armed <= 0;
            quiet_cycles <= 0; pulse_cycles <= 0; watchdog <= 0;
            accepted_time <= 0; pending_data <= 0;
            convst_o <= 0; den_o <= 0; valid_o <= 0;
            overrun_o <= 0; timeout_o <= 0;
            data_o <= 0; seq_o <= 0; time_o <= 0; ticks_o <= 0;
        end else begin
            ticks_o <= ticks_o + 1'b1;
            trigger_previous <= pwm_trigger_i;
            if (!pwm_trigger_i) trigger_armed <= 1;
            den_o <= 0;
            if (clear_overrun_i) overrun_o <= 0;
            if (clear_timeout_i) timeout_o <= 0;
            if (xadc_busy_i || convst_o || !xadc_ready_i) quiet_cycles <= 0;
            else if (quiet_cycles < SETTLE_CYCLES) quiet_cycles <= quiet_cycles + 1'b1;

            // New faults dominate W1C. Disabled requests have no side effects.
            if (enable_i && ((start_sw_i && sync_mode_i) ||
                (request && (state != IDLE || !available)))) overrun_o <= 1;

            if (state != IDLE && state != ERROR && watchdog >= TIMEOUT_CYCLES-1) begin
                state <= ERROR; timeout_o <= 1; convst_o <= 0; den_o <= 0;
            end else begin
                if (state != IDLE && state != ERROR) watchdog <= watchdog + 1'b1;
                case (state)
                    IDLE: begin
                        convst_o <= 0;
                        watchdog <= 0;
                        if (enable_i && request && available) begin
                            accepted_time <= ticks_o;
                            convst_o <= 1;
                            pulse_cycles <= 1;
                            state <= PULSE;
                        end
                    end
                    PULSE: begin
                        if (pulse_cycles >= CONVST_CYCLES) begin
                            convst_o <= 0;
                            state <= WAIT_EOC;
                        end else pulse_cycles <= pulse_cycles + 1'b1;
                    end
                    WAIT_EOC: if (xadc_eoc_i) state <= ISSUE_READ;
                    ISSUE_READ: if (!xadc_drdy_i) begin
                        den_o <= 1;
                        state <= WAIT_DRP;
                    end
                    WAIT_DRP: if (xadc_drdy_i) begin
                        pending_data <= xadc_do_i[15:4];
                        state <= PUBLISH;
                    end
                    PUBLISH: begin
                        data_o <= pending_data;
                        time_o <= accepted_time;
                        seq_o <= seq_o + 1'b1;
                        valid_o <= 1;
                        state <= IDLE;
                    end
                    ERROR: begin convst_o <= 0; den_o <= 0; end
                    default: begin state <= ERROR; timeout_o <= 1; end
                endcase
            end
        end
    end
endmodule
