`timescale 1ns/1ps
// Connected RTL with synthetic XADC conversion responses and periodic Timer pulses.
module tb_control_chain;
  reg clk=0;always #5 clk=~clk;
  reg rst_n=0,pwm_en=0,gates=0,timer_en=0,adc_en=0,soft_reset=0,fault=0;
  reg [1:0] frequency=0;
  integer period=10000,counter=0;
  reg timer_pulse=0;
  always @(posedge clk) begin
    if(!rst_n || !timer_en)begin counter<=0;timer_pulse<=0;end
    else if(counter==period-1)begin counter<=0;timer_pulse<=1;end
    else begin counter<=counter+1;timer_pulse<=0;end
  end
  wire ah,al,bh,bl,trig,running,gates_on,synced,precharge,power_ready,fault_status;
  pwm_core pwm(.clk(clk),.rst_n(rst_n),.enable_i(pwm_en),.gates_en_i(gates),
    .sync_arm_i(1'b1),.freq_sel_i(frequency),.duty_i(16'sd0),.dead_time_i(10'd100),
    .sync_phase_i(14'd1000),.sync_start_i(timer_pulse),.fault_i(fault),
    .gate_ah_o(ah),.gate_al_o(al),.gate_bh_o(bh),.gate_bl_o(bl),.pwm_trigger_o(trig),
    .running_o(running),.gates_on_o(gates_on),.fault_o(fault_status),
    .sync_done_o(synced),.precharge_o(precharge),.power_ready_o(power_ready),.phase_limit_o());
  wire converter_reset,ready;
  reg busy=0,eoc=0,drdy=0,conv_q=0;
  integer remaining=0;
  xadc_startup_guard #(.STARTUP_CYCLES(40)) guard(.clk(clk),.rst_n(rst_n),
    .reset_request_i(soft_reset),.xadc_busy_i(busy),.xadc_reset_o(converter_reset),.ready_o(ready));
  wire convst,den,dwe,valid,overrun,timeout,acq_busy;
  wire [6:0] drp_address;
  wire [11:0] data;
  wire [31:0] seq,sample_time,ticks;
  xadc_acquisition #(.DRP_ADDRESS(7'h13)) adc(.clk(clk),.rst_n(rst_n && !soft_reset),
    .enable_i(adc_en),.sync_mode_i(1'b1),.start_sw_i(1'b0),.pwm_trigger_i(trig && synced),
    .clear_overrun_i(1'b0),.clear_timeout_i(1'b0),.xadc_ready_i(ready),
    .xadc_busy_i(busy),.xadc_eoc_i(eoc),.xadc_drdy_i(drdy),.xadc_do_i(16'h8000),
    .convst_o(convst),.den_o(den),.dwe_o(dwe),.daddr_o(drp_address),
    .valid_o(valid),.overrun_o(overrun),.timeout_o(timeout),.busy_o(acq_busy),
    .data_o(data),.seq_o(seq),.time_o(sample_time),.ticks_o(ticks));
  always @(posedge clk) begin
    if(converter_reset)begin busy<=0;eoc<=0;drdy<=0;conv_q<=0;remaining<=0;end
    else begin
      conv_q<=convst;eoc<=0;drdy<=den;
      if(convst && !conv_q)begin busy<=1;remaining<=100;end
      else if(remaining>0)begin remaining<=remaining-1;
        if(remaining==1)begin busy<=0;eoc<=1;end
      end
    end
  end
  integer checks=0,fd,old_time=0;
  task automatic check(input bit cond,input string label);
    if(!cond)$fatal(1,"%s",label);checks++;
  endtask
  always @(negedge clk) if(rst_n)begin
    if((ah && al)||(bh && bl))$fatal(1,"shoot-through");
  end
  initial begin
    fd=$fopen("build/host/control_chain.csv","w");
    check(fd!=0,"trace file");
    $fdisplay(fd,"frequency,period,irq,now,seq,sample_ticks,valid,pwm_status");
    repeat(5)@(negedge clk);rst_n=1;
    for(integer f=0;f<3;f++)begin
      timer_en=0;pwm_en=0;gates=0;adc_en=0;fault=0;
      frequency=2'(f);period=f==0?10000:f==1?6666:5000;
      soft_reset=1;@(negedge clk);soft_reset=0;
      check(!valid && !ready,"software reset clears publication and availability");
      wait(ready);repeat(25)@(negedge clk);
      check(drp_address==7'h13,"VAUX3 DRP address");
      adc_en=1;pwm_en=1;
      // Carrier can run before Timer, but the ADC must not receive this trigger.
      repeat(300)@(negedge clk);check(!valid && seq==0,"no pre-sync sample");
      timer_en=1;old_time=0;
      for(integer k=0;k<6;k++)begin
        @(posedge timer_pulse);repeat(20)@(negedge clk);
        $fdisplay(fd,"%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d",f,period,k,ticks,seq,sample_time,valid,
          (running?8:0)|(pwm_en?1:0)|(gates?16:0)|(gates_on?32:0)|(fault_status?64:0)|
          (synced?256:0)|(precharge?512:0)|(power_ready?1024:0));
        check(synced,"one-shot completed");
        if(k==0)check(!valid && !(ah|al|bh|bl),"first IRQ waits safely");
        else begin
          check(valid && !overrun && !timeout && data==2048,"atomic valid sample");
          check(seq==k,"one publication per Timer period");
          check(ticks-sample_time>=900 && ticks-sample_time<=1200,"sample phase window");
          if(k>1)check(sample_time-old_time==period,"no interval drift");
          old_time=sample_time;
          if(k==1)begin check(!power_ready,"still blocked in WARMUP");gates=1;end
          else check(power_ready,"precharge finished before closed-loop execution");
        end
      end
      fault=1;repeat(5)@(negedge clk);
      check(!(ah|al|bh|bl) && !power_ready,"fault blocks all gates");
      $display("PASS chain: frequency code %0d, period %0d",f,period);
    end
    $fclose(fd);$display("PASS connected control chain: %0d assertions",checks);$finish;
  end
  initial begin #5ms;$fatal(1,"chain timeout");end
endmodule
