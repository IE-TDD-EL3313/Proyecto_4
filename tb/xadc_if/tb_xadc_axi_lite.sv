`timescale 1ns/1ps
module tb_xadc_axi_lite;
    reg clk=0; always #5 clk=~clk;
    reg rst_n=0;
    reg [7:0] awaddr=0, araddr=0;
    reg awvalid=0, wvalid=0, bready=0, arvalid=0, rready=0;
    reg [31:0] wdata=0;
    reg [3:0] wstrb=0;
    wire awready,wready,bvalid,arready,rvalid;
    wire [1:0] bresp,rresp;
    wire [31:0] rdata;
    reg trigger=0, ready=1;
    reg model_busy=0, model_eoc=0, model_drdy=0;
    reg late_eoc=0, late_drdy=0;
    reg respond=1, respond_drp=1;
    reg [15:0] result_word=16'habc0;
    wire convst,den,dwe; wire [6:0] daddr;
    integer conversions=0, den_count=0, conv_countdown=0, drp_countdown=0;
    reg old_convst=0, old_den=0;
    integer checks=0, read_hold_cycles=3, high_cycles=0;
    reg [31:0] value, before_seq, held_data;
    xadc_axi_lite #(.SETTLE_CYCLES(4),.CONVST_CYCLES(2),.TIMEOUT_CYCLES(64)) dut (
        .s_axi_aclk(clk),.s_axi_aresetn(rst_n),
        .s_axi_awaddr(awaddr),.s_axi_awvalid(awvalid),.s_axi_awready(awready),
        .s_axi_wdata(wdata),.s_axi_wstrb(wstrb),.s_axi_wvalid(wvalid),.s_axi_wready(wready),
        .s_axi_bresp(bresp),.s_axi_bvalid(bvalid),.s_axi_bready(bready),
        .s_axi_araddr(araddr),.s_axi_arvalid(arvalid),.s_axi_arready(arready),
        .s_axi_rdata(rdata),.s_axi_rresp(rresp),.s_axi_rvalid(rvalid),.s_axi_rready(rready),
        .pwm_trigger_i(trigger),.xadc_ready_i(ready),.xadc_busy_i(model_busy),
        .xadc_eoc_i(model_eoc|late_eoc),.xadc_drdy_i(model_drdy|late_drdy),
        .xadc_do_i(result_word),.convst_o(convst),.den_o(den),.dwe_o(dwe),.daddr_o(daddr));

    // Behavioral DRP responder only; this is not the AMD analog simulation model.
    always @(negedge clk) begin
        model_eoc=0; model_drdy=0;
        if (!rst_n) begin
            model_busy=0; conv_countdown=0; drp_countdown=0; old_convst=0; old_den=0; high_cycles=0;
        end else begin
            if (convst && !old_convst) begin
                conversions=conversions+1;
                if (respond) begin model_busy=1; conv_countdown=12; end
            end
            if (convst) high_cycles=high_cycles+1;
            if (!convst && old_convst) begin
                if (high_cycles!=2) $fatal(1,"CONVST pulse width mismatch");
                high_cycles=0;
            end
            if (conv_countdown>0) begin
                conv_countdown=conv_countdown-1;
                if (conv_countdown==0) begin model_busy=0; model_eoc=1; end
            end
            if (den) begin
                if (old_den) $fatal(1,"DEN lasted more than one DCLK");
                if (dwe || daddr!=7'h10) $fatal(1,"DRP read/address invalid");
                den_count=den_count+1;
                if (respond_drp) drp_countdown=4;
            end
            if (drp_countdown>0) begin
                drp_countdown=drp_countdown-1;
                if (drp_countdown==0) model_drdy=1;
            end
            old_convst=convst; old_den=den;
        end
    end
    task check(input bit condition,input string description);
        begin if (!condition) $fatal(1,"FAIL: %s",description); checks=checks+1; end
    endtask
    task reset_dut;
        begin
            @(negedge clk); rst_n=0; repeat(3) @(negedge clk);
            rst_n=1; repeat(8) @(negedge clk);
        end
    endtask
    task write_reg(input [7:0] addr,input [31:0] data,input [3:0] strobes,
                   input integer aw_delay,input integer w_delay,input [1:0] expected);
        reg [1:0] held;
        begin
            fork
                begin
                    repeat(aw_delay+1) @(negedge clk);
                    awaddr=addr; awvalid=1;
                    @(posedge clk); while(!awready) @(posedge clk);
                    @(negedge clk); awvalid=0;
                end
                begin
                    repeat(w_delay+1) @(negedge clk);
                    wdata=data; wstrb=strobes; wvalid=1;
                    @(posedge clk); while(!wready) @(posedge clk);
                    @(negedge clk); wvalid=0;
                end
            join
            while(!bvalid) @(negedge clk);
            held=bresp;
            check(bresp==expected,"BRESP");
            repeat(3) begin @(negedge clk); check(bvalid && bresp==held,"B stable under backpressure"); end
            bready=1; @(negedge clk); bready=0;
        end
    endtask
    task read_reg(input [7:0] addr,output [31:0] data,input [1:0] expected);
        reg [31:0] held;
        begin
            @(negedge clk); araddr=addr; arvalid=1;
            @(posedge clk); while(!arready) @(posedge clk);
            @(negedge clk); arvalid=0;
            while(!rvalid) @(negedge clk);
            held=rdata; check(rresp==expected,"RRESP");
            repeat(read_hold_cycles) begin @(negedge clk); check(rvalid && rdata==held,"RDATA stable under backpressure"); end
            data=held; rready=1; @(negedge clk); rready=0;
        end
    endtask
    task pwm_pulse;
        begin @(negedge clk); trigger=1; repeat(4) @(negedge clk); trigger=0; end
    endtask
    initial begin
        #200000; $fatal(1,"testbench global timeout");
    end
    initial begin
        reset_dut();
        read_reg('h00,value,0); check(value==0,"CTRL reset");
        read_reg('h04,value,0); check(value==0,"STATUS reset");
        read_reg('h08,value,0); check(value==0,"DATA reset");
        write_reg('h00,1,1,0,4,0); // AW first
        write_reg('h00,0,1,4,0,0); // W first
        write_reg('h00,1,0,0,0,0); // null strobe
        read_reg('h00,value,0); check(value==0,"null strobe ignored");
        write_reg('h00,1,2,0,0,0);
        read_reg('h00,value,0); check(value==0,"upper byte ignored");
        write_reg('h08,99,15,0,0,2);
        write_reg('h01,99,15,0,0,2);
        read_reg('h1c,value,2); read_reg('h09,value,2);
        write_reg('h00,5,1,0,0,0); // enable + START_SW same write
        repeat(30) @(negedge clk);
        read_reg('h08,value,0); check(value=='habc,"12-bit alignment");
        read_reg('h0c,value,0); check(value==1,"one publication");
        read_reg('h00,value,0); check(value==1,"START_SW reads zero");
        read_reg('h10,value,0); check(value>0,"timestamp captured");
        read_reg('h18,held_data,0); repeat(5) @(negedge clk);
        read_reg('h18,value,0); check(value>held_data,"TICKS live");
        write_reg('h00,3,1,0,0,2);
        read_reg('h00,value,0); check(value==1,"mode change while enabled rejected");
        write_reg('h00,0,1,0,0,0);
        write_reg('h00,3,1,0,0,0);
        before_seq=conversions; pwm_pulse(); repeat(35) @(negedge clk);
        check(conversions==before_seq+1,"four-cycle trigger converts once");
        read_reg('h0c,value,0); check(value==2,"PWM publishes");
        write_reg('h00,7,1,0,0,0);
        read_reg('h04,value,0); check(value[2],"software start rejected in sync mode");
        write_reg('h14,4,1,0,0,0);
        read_reg('h04,value,0); check(!value[2],"W1C overrun");
        ready=0; pwm_pulse(); repeat(5) @(negedge clk);
        read_reg('h04,value,0); check(value[2],"unavailable ADC rejects request");
        fork
            begin write_reg('h14,4,1,0,0,0); end
            begin
                wait(dut.clear_overrun); @(negedge clk); trigger=1;
                @(negedge clk); trigger=0;
            end
        join
        read_reg('h04,value,0); check(value[2],"new overrun dominates coincident W1C");
        ready=1;
        late_drdy=1; before_seq=conversions; pwm_pulse(); repeat(5) @(negedge clk);
        check(conversions==before_seq,"DRDY held high prevents new conversion"); late_drdy=0;

        // Trigger high across reset and enable must not count as a new edge.
        trigger=1; before_seq=conversions; reset_dut();
        write_reg('h00,3,1,0,0,0); repeat(10) @(negedge clk);
        check(conversions==before_seq,"residual high trigger ignored");
        trigger=0; repeat(3) @(negedge clk); pwm_pulse(); repeat(30) @(negedge clk);
        check(conversions==before_seq+1,"low rearms trigger");

        // Read response remains fixed even when a new sample is published.
        result_word=16'h1230;
        read_hold_cycles=30;
        fork
            begin pwm_pulse(); end
            begin read_reg('h08,held_data,0); end
        join
        read_hold_cycles=3;
        repeat(25) @(negedge clk);
        read_reg('h08,value,0); check(value=='h123,"new sample visible after response");

        respond=0; pwm_pulse(); repeat(80) @(negedge clk);
        read_reg('h04,value,0); check(value[3] && value[1],"EOC timeout locks ERROR");
        read_reg('h0c,before_seq,0);
        write_reg('h14,8,1,0,0,0);
        late_eoc=1; late_drdy=1; repeat(3) @(negedge clk); late_eoc=0; late_drdy=0;
        read_reg('h04,value,0); check(!value[3] && value[1],"W1C does not rearm ERROR");
        read_reg('h0c,value,0); check(value==before_seq,"late responses never publish");
        respond=1; respond_drp=0; reset_dut();
        write_reg('h00,5,1,0,0,0); repeat(85) @(negedge clk);
        read_reg('h04,value,0); check(value[3] && value[1],"DRDY timeout");
        write_reg('h00,0,1,0,0,0);
        write_reg('h00,2,1,0,0,2);
        read_reg('h00,value,0); check(value==0,"busy rejects mode change with ENABLE already zero");
        respond_drp=1; reset_dut();
        write_reg('h00,5,1,0,0,0); reset_dut(); repeat(25) @(negedge clk);
        read_reg('h0c,value,0); check(value==0,"reset during conversion cancels publication");
        read_reg('h04,value,0); check(value==0,"reset during conversion clears flags");
        write_reg('h00,5,1,0,0,0); write_reg('h00,0,1,0,0,0);
        repeat(25) @(negedge clk);
        read_reg('h0c,value,0); check(value==1,"disable lets accepted conversion finish");
        write_reg('h00,4,1,0,0,0); repeat(20) @(negedge clk);
        read_reg('h0c,value,0); check(value==1,"disabled START ignored");
        // Reach the wrap without simulating 2^32 conversions.
        @(negedge clk); dut.acquisition.seq_o=32'hffffffff;
        write_reg('h00,5,1,0,0,0); repeat(30) @(negedge clk);
        read_reg('h0c,value,0); check(value==0,"SEQ wraps modulo 2^32");
        @(negedge clk); dut.acquisition.ticks_o=32'hfffffff8;
        repeat(16) @(negedge clk);
        read_reg('h18,value,0); check(value<100,"TICKS wraps modulo 2^32");
        check(den_count>0,"DRP exercised");
        $display("PASS xadc_axi_lite: %0d assertions",checks); $finish;
    end
endmodule
