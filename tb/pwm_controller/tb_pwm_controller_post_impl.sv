// -----------------------------------------------------------------------------
// tb_pwm_controller_post_impl.sv
// Testbench autoverificable para la simulacion POST-IMPLEMENTACION del PWM.
//
// A diferencia de tb_pwm_controller.sv, este testbench solo usa los puertos del
// periferico (AXI4-Lite, compuertas, triggers, sync_start_i y fault_i). Asi
// funciona tambien sobre la netlist implementada, donde las senales internas
// cambian de nombre. Todas las medidas se hacen en ciclos de reloj de 10 ns.
// -----------------------------------------------------------------------------
`timescale 1ns / 1ps

module tb_pwm_controller_post_impl;

    logic clk = 1'b0;
    always #5 clk = ~clk;  // 100 MHz

    logic        rst_n;
    logic [3:0]  awaddr, araddr;
    logic        awvalid, awready, wvalid, wready, bvalid, bready;
    logic        arvalid, arready, rvalid, rready;
    logic [31:0] wdata, rdata;
    logic [3:0]  wstrb;
    logic [1:0]  bresp, rresp;
    logic        sync_start, fault;
    logic        gate_ah, gate_al, gate_bh, gate_bl, trig, adc_trig;

    pwm_controller_axi dut (
        .s_axi_aclk    (clk),
        .s_axi_aresetn (rst_n),
        .s_axi_awaddr  (awaddr),
        .s_axi_awprot  (3'b000),
        .s_axi_awvalid (awvalid),
        .s_axi_awready (awready),
        .s_axi_wdata   (wdata),
        .s_axi_wstrb   (wstrb),
        .s_axi_wvalid  (wvalid),
        .s_axi_wready  (wready),
        .s_axi_bresp   (bresp),
        .s_axi_bvalid  (bvalid),
        .s_axi_bready  (bready),
        .s_axi_araddr  (araddr),
        .s_axi_arprot  (3'b000),
        .s_axi_arvalid (arvalid),
        .s_axi_arready (arready),
        .s_axi_rdata   (rdata),
        .s_axi_rresp   (rresp),
        .s_axi_rvalid  (rvalid),
        .s_axi_rready  (rready),
        .sync_start_i  (sync_start),
        .fault_i       (fault),
        .gate_ah_o     (gate_ah),
        .gate_al_o     (gate_al),
        .gate_bh_o     (gate_bh),
        .gate_bl_o     (gate_bl),
        .pwm_trigger_o (trig),
        .adc_trigger_o (adc_trig)
    );

    localparam logic [3:0] A_CTRL = 4'h0, A_DUTY = 4'h4, A_DEAD = 4'h8, A_PHASE = 4'hC;

    int n_pass = 0, n_fail = 0;
    longint cycle = 0;
    always @(posedge clk) cycle <= cycle + 1;

    task automatic check(input string name, input bit cond, input string detail);
        if (cond) begin n_pass++; $display("  PASS  %-48s %s", name, detail); end
        else      begin n_fail++; $display("  FAIL  %-48s %s", name, detail); end
    endtask

    // ---------------- Monitores sobre los puertos ----------------
    int shoot_through = 0;
    logic ah_q, al_q, bh_q, bl_q, trig_q, adc_q;
    // Tiempo muerto medido en ns sobre los flancos reales de los pines, para
    // incluir el retardo y el skew de cada salida en la netlist temporizada.
    realtime fall_ah, fall_al, fall_bh, fall_bl;
    realtime dt_min = 1.0e9, dt_max = 0.0;
    int dt_count = 0;
    longint last_trig_rise = 0;
    int trig_width = 0, trig_width_last = 0;
    int n_trig_rise = 0, n_adc_rise = 0;
    int acc_ah = 0, per_ah = 0, acc_bh = 0, per_bh = 0;
    longint first_ah = -1, first_al = -1, first_bh = -1, first_bl = -1;

    always @(posedge clk) begin
        if ((gate_ah && gate_al) || (gate_bh && gate_bl)) shoot_through++;
        ah_q <= gate_ah; al_q <= gate_al; bh_q <= gate_bh; bl_q <= gate_bl;
        trig_q <= trig; adc_q <= adc_trig;
        // Trigger: flanco, ancho y ancho por periodo de las compuertas altas
        if (trig && !trig_q) begin
            last_trig_rise = cycle; trig_width = 1; n_trig_rise++;
            per_ah = acc_ah; per_bh = acc_bh; acc_ah = 0; acc_bh = 0;
        end else if (trig) trig_width++;
        else if (trig_q) trig_width_last = trig_width;
        if (adc_trig && !adc_q) n_adc_rise++;
        acc_ah += gate_ah; acc_bh += gate_bh;
        if (gate_ah && first_ah < 0) first_ah = cycle;
        if (gate_al && first_al < 0) first_al = cycle;
        if (gate_bh && first_bh < 0) first_bh = cycle;
        if (gate_bl && first_bl < 0) first_bl = cycle;
    end

    always @(negedge gate_ah) fall_ah = $realtime;
    always @(negedge gate_al) fall_al = $realtime;
    always @(negedge gate_bh) fall_bh = $realtime;
    always @(negedge gate_bl) fall_bl = $realtime;
    always @(posedge gate_al) if (fall_ah > 0) dt_note($realtime - fall_ah);
    always @(posedge gate_ah) if (fall_al > 0) dt_note($realtime - fall_al);
    always @(posedge gate_bl) if (fall_bh > 0) dt_note($realtime - fall_bh);
    always @(posedge gate_bh) if (fall_bl > 0) dt_note($realtime - fall_bl);

    task automatic dt_note(input realtime d);
        if (d < dt_min) dt_min = d;
        if (d > dt_max) dt_max = d;
        dt_count++;
    endtask
    task automatic dt_reset();
        dt_min = 1.0e9; dt_max = 0.0; dt_count = 0;
        fall_ah = 0; fall_al = 0; fall_bh = 0; fall_bl = 0;
    endtask

    // ---------------- Tareas AXI4-Lite ----------------
    task automatic axi_write(input logic [3:0] addr, input logic [31:0] data,
                             input logic [3:0] strb = 4'hF);
        bit aw_done = 0, w_done = 0;
        @(negedge clk);
        awaddr = addr; wdata = data; wstrb = strb; bready = 1'b0;
        awvalid = 1'b1; wvalid = 1'b1;
        while (!(aw_done && w_done)) begin
            @(posedge clk);
            if (awvalid && awready) aw_done = 1;
            if (wvalid  && wready)  w_done  = 1;
            @(negedge clk);
            if (aw_done) awvalid = 1'b0;
            if (w_done)  wvalid  = 1'b0;
        end
        bready = 1'b1;
        do @(posedge clk); while (!bvalid);
        @(negedge clk);
        bready = 1'b0;
    endtask

    task automatic axi_read(input logic [3:0] addr, output logic [31:0] data);
        @(negedge clk);
        araddr = addr; arvalid = 1'b1; rready = 1'b0;
        do @(posedge clk); while (!arready);
        @(negedge clk);
        arvalid = 1'b0; rready = 1'b1;
        do @(posedge clk); while (!rvalid);
        data = rdata;
        @(negedge clk);
        rready = 1'b0;
    endtask

    task automatic next_trig(output longint t);
        longint prev = last_trig_rise;
        int guard = 0;
        while (last_trig_rise == prev && guard < 100000) begin @(posedge clk); guard++; end
        t = last_trig_rise;
    endtask

    task automatic wait_periods(input int k);
        longint t;
        repeat (k) next_trig(t);
    endtask

    function automatic int th_of(input int n, input int d);
        longint p = longint'(n) * longint'(32768 + d);
        return int'(p >>> 16);
    endfunction

    // =========================================================================
    initial begin
        logic [31:0] r;
        longint t0, t1, ts;
        int ok, n, exp_ah;

        awvalid = 0; wvalid = 0; bready = 0; arvalid = 0; rready = 0;
        awaddr = 0; araddr = 0; wdata = 0; wstrb = 0;
        sync_start = 0; fault = 0;
        rst_n = 1'b0;
        repeat (30) @(posedge clk);   // > 100 ns de GSR en la simulacion temporizada
        @(negedge clk) rst_n = 1'b1;  // lejos del flanco de subida (setup/hold)
        repeat (10) @(posedge clk);

        $display("\n==== Testbench post-implementacion PWM Controller ====");

        $display("\nP1 - Reset y registros");
        axi_read(A_CTRL, r);  check("P1 CTRL_STATUS = 0", r == 0, $sformatf("leido=0x%08h", r));
        axi_read(A_DEAD, r);  check("P1 DEADTIME = 100", r == 100, $sformatf("leido=%0d", r));
        axi_read(A_PHASE, r); check("P1 SYNC_PHASE = 1000", r == 1000, $sformatf("leido=%0d", r));
        check("P1 compuertas y triggers en 0", !(gate_ah|gate_al|gate_bh|gate_bl|trig|adc_trig), "");
        axi_write(A_DUTY, 32'h0000_7FFF); axi_read(A_DUTY, r);
        check("P1 saturacion duty 0x7FFF -> 0x7999", r == 32'h7999, $sformatf("leido=0x%04h", r));
        axi_write(A_DEAD, 32'd2000); axi_read(A_DEAD, r);
        check("P1 saturacion dead_time 2000 -> 1023", r == 1023, $sformatf("leido=%0d", r));
        axi_write(A_DEAD, 32'd100);
        axi_write(A_DUTY, 32'h0000_4000);

        $display("\nP2 - Frecuencias (periodo de pwm_trigger_o = 2N ciclos)");
        for (int f = 0; f < 3; f++) begin
            case (f) 0: n = 5000; 1: n = 3333; default: n = 2500; endcase
            axi_write(A_CTRL, (f << 1) | 32'h11);
            wait_periods(2);
            next_trig(t0); next_trig(t1);
            check($sformatf("P2 freq_sel=%0d (%0d kHz)", f, 50000 / n), (t1 - t0) == 2 * n,
                  $sformatf("esperado=%0d medido=%0d", 2 * n, t1 - t0));
        end

        $display("\nP3 - Modulacion, tiempo muerto y trigger (20 kHz, m = 0.5)");
        axi_write(A_CTRL, 32'h15);
        wait_periods(3);
        dt_reset();
        wait_periods(3);
        exp_ah = 2 * th_of(2500, 16384) - 1 - 100;
        check("P3 ancho de gate_ah por periodo", per_ah >= exp_ah - 2 && per_ah <= exp_ah + 2,
              $sformatf("esperado=%0d medido=%0d", exp_ah, per_ah));
        // 100 ciclos = 1000 ns; se acepta el skew entre pines (< 1 ciclo)
        check("P3 tiempo muerto = 1 us (+-10 ns)", dt_count > 0 && dt_min >= 990.0 && dt_max <= 1010.0,
              $sformatf("transiciones=%0d min=%.3f ns max=%.3f ns", dt_count, dt_min, dt_max));
        check("P3 ancho de pwm_trigger_o = 4 ciclos", trig_width_last == 4, $sformatf("ancho=%0d", trig_width_last));

        $display("\nP4 - enable y gates_en");
        axi_write(A_CTRL, 32'h05);                   // gates_en = 0
        repeat (3) @(posedge clk); #1;
        check("P4 gates_en = 0 apaga las compuertas", !(gate_ah|gate_al|gate_bh|gate_bl), "");
        n_trig_rise = 0;
        wait_periods(2);
        check("P4 trigger sigue con gates_en = 0", n_trig_rise >= 2 && per_ah == 0, $sformatf("triggers=%0d", n_trig_rise));
        axi_write(A_CTRL, 32'h04);                   // enable = 0
        t0 = last_trig_rise;
        repeat (12000) @(posedge clk);
        axi_read(A_CTRL, r);
        check("P4 enable = 0 detiene el trigger y running", last_trig_rise == t0 && r[3] == 0, $sformatf("CTRL=0x%08h", r));

        $display("\nP5 - Sincronizacion (sync_arm, sync_start_i, adc_trigger_o)");
        axi_write(A_PHASE, 32'd1000);
        axi_write(A_CTRL, 32'h85);                   // enable + sync_arm, 20 kHz
        n_trig_rise = 0; n_adc_rise = 0;
        wait_periods(3);
        check("P5 adc_trigger_o bloqueado antes de sincronizar", n_trig_rise >= 3 && n_adc_rise == 0,
              $sformatf("pwm=%0d adc=%0d", n_trig_rise, n_adc_rise));
        repeat (777) @(posedge clk);
        @(negedge clk); sync_start = 1'b1; ts = cycle;
        @(negedge clk); sync_start = 1'b0;
        next_trig(t1);
        check("P5 trigger a 4001 ciclos del sync (fase 1000)", (t1 - ts) >= 4000 && (t1 - ts) <= 4003,
              $sformatf("medido=%0d", t1 - ts));
        axi_read(A_CTRL, r);
        check("P5 sync_done = 1 y sync_arm = 0", r[8] == 1 && r[7] == 0, $sformatf("CTRL=0x%08h", r));
        n_trig_rise = 0; n_adc_rise = 0;
        wait_periods(3);
        check("P5 adc_trigger_o = pwm_trigger_o tras sincronizar", n_adc_rise == n_trig_rise && n_adc_rise >= 3,
              $sformatf("pwm=%0d adc=%0d", n_trig_rise, n_adc_rise));

        $display("\nP6 - Arranque con precarga del bootstrap");
        first_ah = -1; first_al = -1; first_bh = -1; first_bl = -1;
        axi_write(A_CTRL, 32'h15);                   // gates_en = 1
        while (first_al < 0) @(posedge clk);
        axi_read(A_CTRL, r);
        check("P6 precharge = 1 durante la precarga", r[9] == 1 && r[10] == 0, $sformatf("CTRL=0x%08h", r));
        wait_periods(3);
        ok = first_al > 0 && first_bl > 0 && (first_ah - first_al) >= 2000 && (first_bh - first_bl) >= 2000;
        check("P6 inferiores >= 20 us antes que superiores", ok,
              $sformatf("A: %0d ciclos, B: %0d ciclos", first_ah - first_al, first_bh - first_bl));
        axi_read(A_CTRL, r);
        check("P6 power_ready = 1 al terminar", r[9] == 0 && r[10] == 1, $sformatf("CTRL=0x%08h", r));

        $display("\nP7 - fault_i");
        repeat (700) @(posedge clk);
        @(negedge clk); fault = 1'b1;
        repeat (5) @(posedge clk); #1;
        check("P7 compuertas en 0 tras la falla", !(gate_ah|gate_al|gate_bh|gate_bl), "");
        axi_read(A_CTRL, r);
        check("P7 bit fault = 1 y gates_en borrado", r[6] == 1 && r[4] == 0, $sformatf("CTRL=0x%08h", r));
        @(negedge clk); fault = 1'b0;

        check("Global: sin shoot-through", shoot_through == 0, $sformatf("eventos=%0d", shoot_through));

        $display("\n==== Resumen post-implementacion: %0d pruebas, %0d PASS, %0d FAIL ====\n",
                 n_pass + n_fail, n_pass, n_fail);
        if (n_fail != 0) $fatal(1, "Hay pruebas que fallaron");
        $finish;
    end

    initial begin
        #10ms;
        $display("TIMEOUT de simulacion");
        $fatal(1, "timeout");
    end

endmodule
