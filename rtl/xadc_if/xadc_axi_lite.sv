`timescale 1ns/1ps
// One outstanding read and write; AW and W are independently buffered.
module xadc_axi_lite #(
    parameter integer CONVST_CYCLES = 4,
    parameter integer SETTLE_CYCLES = 20,
    parameter integer TIMEOUT_CYCLES = 800,
    parameter [6:0] DRP_ADDRESS = 7'h10
) (
    input wire s_axi_aclk, s_axi_aresetn,
    input wire [7:0] s_axi_awaddr,
    input wire s_axi_awvalid,
    output wire s_axi_awready,
    input wire [31:0] s_axi_wdata,
    input wire [3:0] s_axi_wstrb,
    input wire s_axi_wvalid,
    output wire s_axi_wready,
    output reg [1:0] s_axi_bresp,
    output reg s_axi_bvalid,
    input wire s_axi_bready,
    input wire [7:0] s_axi_araddr,
    input wire s_axi_arvalid,
    output wire s_axi_arready,
    output reg [31:0] s_axi_rdata,
    output reg [1:0] s_axi_rresp,
    output reg s_axi_rvalid,
    input wire s_axi_rready,
    input wire pwm_trigger_i, xadc_ready_i, xadc_busy_i, xadc_eoc_i, xadc_drdy_i,
    input wire [15:0] xadc_do_i,
    output wire convst_o, den_o, dwe_o,
    output wire [6:0] daddr_o
);
    reg aw_held, w_held;
    reg [7:0] awaddr;
    reg [31:0] wdata;
    reg [3:0] wstrb;
    reg enable, sync_mode, start_sw, clear_overrun, clear_timeout;
    wire valid, busy, overrun, timeout;
    wire [11:0] data;
    wire [31:0] seq, sample_time, ticks;
    assign s_axi_awready = !aw_held && !s_axi_bvalid;
    assign s_axi_wready = !w_held && !s_axi_bvalid;
    assign s_axi_arready = !s_axi_rvalid;
    xadc_acquisition #(.CONVST_CYCLES(CONVST_CYCLES), .SETTLE_CYCLES(SETTLE_CYCLES),
        .TIMEOUT_CYCLES(TIMEOUT_CYCLES), .DRP_ADDRESS(DRP_ADDRESS)) acquisition (
        .clk(s_axi_aclk), .rst_n(s_axi_aresetn), .enable_i(enable), .sync_mode_i(sync_mode),
        .start_sw_i(start_sw), .pwm_trigger_i(pwm_trigger_i),
        .clear_overrun_i(clear_overrun), .clear_timeout_i(clear_timeout),
        .xadc_ready_i(xadc_ready_i), .xadc_busy_i(xadc_busy_i),
        .xadc_eoc_i(xadc_eoc_i), .xadc_drdy_i(xadc_drdy_i), .xadc_do_i(xadc_do_i),
        .convst_o(convst_o), .den_o(den_o), .dwe_o(dwe_o), .daddr_o(daddr_o),
        .valid_o(valid), .busy_o(busy), .overrun_o(overrun), .timeout_o(timeout),
        .data_o(data), .seq_o(seq), .time_o(sample_time), .ticks_o(ticks));

    always @(posedge s_axi_aclk) begin
        if (!s_axi_aresetn) begin
            aw_held<=0; w_held<=0; awaddr<=0; wdata<=0; wstrb<=0;
            s_axi_bvalid<=0; s_axi_bresp<=0;
            s_axi_rvalid<=0; s_axi_rresp<=0; s_axi_rdata<=0;
            enable<=0; sync_mode<=0; start_sw<=0; clear_overrun<=0; clear_timeout<=0;
        end else begin
            start_sw<=0; clear_overrun<=0; clear_timeout<=0;
            if (s_axi_awvalid && s_axi_awready) begin aw_held<=1; awaddr<=s_axi_awaddr; end
            if (s_axi_wvalid && s_axi_wready) begin w_held<=1; wdata<=s_axi_wdata; wstrb<=s_axi_wstrb; end
            if (s_axi_bvalid && s_axi_bready) s_axi_bvalid<=0;
            if (aw_held && w_held && !s_axi_bvalid) begin
                aw_held<=0; w_held<=0; s_axi_bvalid<=1; s_axi_bresp<=0;
                case (awaddr)
                    8'h00: if (wstrb[0]) begin
                        if (wdata[1] != sync_mode && (enable || busy)) s_axi_bresp<=2'b10;
                        else begin enable<=wdata[0]; sync_mode<=wdata[1]; start_sw<=wdata[2]; end
                    end
                    8'h14: if (wstrb[0]) begin clear_overrun<=wdata[2]; clear_timeout<=wdata[3]; end
                    default: s_axi_bresp<=2'b10;
                endcase
            end
            if (s_axi_rvalid && s_axi_rready) s_axi_rvalid<=0;
            if (s_axi_arvalid && s_axi_arready) begin
                s_axi_rvalid<=1; s_axi_rresp<=0;
                case (s_axi_araddr)
                    8'h00: s_axi_rdata<={30'b0,sync_mode,enable};
                    8'h04: s_axi_rdata<={28'b0,timeout,overrun,busy,valid};
                    8'h08: s_axi_rdata<={20'b0,data};
                    8'h0c: s_axi_rdata<=seq;
                    8'h10: s_axi_rdata<=sample_time;
                    8'h14: s_axi_rdata<=0;
                    8'h18: s_axi_rdata<=ticks;
                    default: begin s_axi_rdata<=0; s_axi_rresp<=2'b10; end
                endcase
            end
        end
    end
endmodule
