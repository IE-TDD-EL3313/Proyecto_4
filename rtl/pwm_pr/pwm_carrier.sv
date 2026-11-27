// Seccion B: base temporal PWM, contador triangular centrado.
// Sintetizable. freq_sel: 00 N0, 01 N1, 10 N2, 11 N0 (reservado).
module pwm_carrier #(
    parameter int unsigned CLK_HZ   = pwm_config_pkg::CLK_HZ,
    parameter int unsigned FREQ0_HZ = pwm_config_pkg::FREQ0_HZ,
    parameter int unsigned FREQ1_HZ = pwm_config_pkg::FREQ1_HZ,
    parameter int unsigned FREQ2_HZ = pwm_config_pkg::FREQ2_HZ,
    parameter int unsigned WIDTH    = pwm_config_pkg::PWM_WIDTH
)(
    input  logic                 clk,
    input  logic                 rst_n,
    input  logic                 enable,
    input  logic [1:0]           freq_sel,
    output logic [WIDTH-1:0]     carrier_count,
    output logic [WIDTH-1:0]     period_max,
    output logic                 update_event,
    output logic                 running
);
    localparam logic [WIDTH-1:0] N0 = WIDTH'(CLK_HZ / (2 * FREQ0_HZ));
    localparam logic [WIDTH-1:0] N1 = WIDTH'(CLK_HZ / (2 * FREQ1_HZ));
    localparam logic [WIDTH-1:0] N2 = WIDTH'(CLK_HZ / (2 * FREQ2_HZ));

    logic [WIDTH-1:0] period_selected;
    logic dir_up;

    always_comb begin
        case (freq_sel)
            2'b00: period_selected = N0;
            2'b01: period_selected = N1;
            2'b10: period_selected = N2;
            default: period_selected = N0; // 11: codigo reservado
        endcase
    end

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            carrier_count <= '0;
            period_max    <= N0;
            dir_up        <= 1'b1;
            update_event  <= 1'b0;
            running       <= 1'b0;
        end else if (!enable) begin
            carrier_count <= '0;
            period_max    <= period_selected;
            dir_up        <= 1'b1;
            update_event  <= 1'b0;
            running       <= 1'b0;
        end else begin
            running      <= 1'b1;
            update_event <= 1'b0;
            if (dir_up) begin
                if (carrier_count >= period_max) begin
                    dir_up        <= 1'b0;
                    carrier_count <= carrier_count - 1'b1;
                end else begin
                    carrier_count <= carrier_count + 1'b1;
                end
            end else begin
                if (carrier_count <= 1) begin
                    carrier_count <= '0;
                    dir_up        <= 1'b1;
                    period_max    <= period_selected;
                    update_event  <= 1'b1;
                end else begin
                    carrier_count <= carrier_count - 1'b1;
                end
            end
        end
    end
endmodule
