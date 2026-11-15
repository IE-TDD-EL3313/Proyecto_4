// -----------------------------------------------------------------------------
// tb_pwm_controller.sv
// Testbench autoverificable del PWM Controller - Seccion 12.1 (issue #11)
//
// Cada prueba imprime PASS o FAIL con el valor esperado y el obtenido.
// Al final se imprime un resumen y la simulacion termina con error si hubo
// algun fallo. Ademas, un monitor revisa en todos los ciclos que nunca esten
// encendidas las dos compuertas de una misma pierna (shoot-through).
// -----------------------------------------------------------------------------
`timescale 1ns / 1ps

module tb_pwm_controller;

    // ---------------- Reloj y DUT ----------------
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
    logic        gate_ah, gate_al, gate_bh, gate_bl, trig;

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
        .pwm_trigger_o (trig)
    );

    // Senales internas usadas como referencia por el testbench
    wire [13:0] c        = dut.u_core.c;
    wire        valley   = dut.u_core.valley;
    wire        s_a      = dut.u_core.s_a;
    wire        s_b      = dut.u_core.s_b;
    wire        gates_on = dut.u_core.gates_on_o;

    localparam logic [3:0] A_CTRL = 4'h0, A_DUTY = 4'h4, A_DEAD = 4'h8, A_PHASE = 4'hC;

    // ---------------- Contadores de resultados ----------------
    int n_pass = 0, n_fail = 0;
    longint cycle = 0;
    always @(posedge clk) cycle <= cycle + 1;

    task automatic check(input string name, input bit cond, input string detail);
        if (cond) begin
            n_pass++;
            $display("  PASS  %-46s %s", name, detail);
        end else begin
            n_fail++;
            $display("  FAIL  %-46s %s", name, detail);
        end
    endtask

    // ---------------- Monitor de shoot-through (siempre activo) ----------------
    int shoot_through = 0;
    always @(posedge clk) begin
        if ((gate_ah && gate_al) || (gate_bh && gate_bl)) shoot_through++;
    end

    // ---------------- Monitor de tiempo muerto ----------------
    // Mide los ciclos entre que una compuerta baja y la complementaria sube.
    longint fall_ah, fall_al, fall_bh, fall_bl;
    int dt_min = 1 << 30, dt_max = 0, dt_count = 0;
    logic ah_q, al_q, bh_q, bl_q;
    always @(posedge clk) begin
        ah_q <= gate_ah; al_q <= gate_al; bh_q <= gate_bh; bl_q <= gate_bl;
        if (ah_q && !gate_ah) fall_ah = cycle;
        if (al_q && !gate_al) fall_al = cycle;
        if (bh_q && !gate_bh) fall_bh = cycle;
        if (bl_q && !gate_bl) fall_bl = cycle;
        if (!al_q && gate_al && fall_ah > 0) dt_note(cycle - fall_ah);
        if (!ah_q && gate_ah && fall_al > 0) dt_note(cycle - fall_al);
        if (!bl_q && gate_bl && fall_bh > 0) dt_note(cycle - fall_bh);
        if (!bh_q && gate_bh && fall_bl > 0) dt_note(cycle - fall_bl);
    end
    task automatic dt_note(input longint d);
        if (d < dt_min) dt_min = d;
        if (d > dt_max) dt_max = d;
        dt_count++;
    endtask
    task automatic dt_reset();
        dt_min = 1 << 30; dt_max = 0; dt_count = 0;
        fall_ah = 0; fall_al = 0; fall_bh = 0; fall_bl = 0;
    endtask

    // ---------------- Monitor de primer encendido (bootstrap) ----------------
    longint first_ah = -1, first_al = -1, first_bh = -1, first_bl = -1;
    always @(posedge clk) begin
        if (gate_ah && first_ah < 0) first_ah = cycle;
        if (gate_al && first_al < 0) first_al = cycle;
        if (gate_bh && first_bh < 0) first_bh = cycle;
        if (gate_bl && first_bl < 0) first_bl = cycle;
    end

    // ---------------- Monitor de anchos por periodo ----------------
    // Ciclos en alto de s_a, s_b, gate_ah y gate_bh entre dos valles.
    int acc_sa, acc_sb, acc_ah, acc_bh;
    int per_sa, per_sb, per_ah, per_bh;
    always @(posedge clk) begin
        if (valley) begin
            per_sa = acc_sa; per_sb = acc_sb; per_ah = acc_ah; per_bh = acc_bh;
            acc_sa = s_a; acc_sb = s_b; acc_ah = gate_ah; acc_bh = gate_bh;
        end else begin
            acc_sa += s_a; acc_sb += s_b; acc_ah += gate_ah; acc_bh += gate_bh;
        end
    end

    // ---------------- Tareas AXI4-Lite (BFM) ----------------
    // order: 0 = AW y W juntos, 1 = AW primero, 2 = W primero
    task automatic axi_write(input logic [3:0] addr, input logic [31:0] data,
                             input logic [3:0] strb = 4'hF, input int order = 0,
                             input int bready_delay = 0);
        bit aw_done = 0, w_done = 0;
        @(negedge clk);
        awaddr = addr; wdata = data; wstrb = strb; bready = 1'b0;
        if (order != 2) awvalid = 1'b1;
        if (order != 1) wvalid  = 1'b1;
        while (!(aw_done && w_done)) begin
            @(posedge clk);
            if (awvalid && awready) aw_done = 1;
            if (wvalid  && wready)  w_done  = 1;
            @(negedge clk);
            if (aw_done) awvalid = 1'b0;
            if (w_done)  wvalid  = 1'b0;
            if (order == 1 && aw_done && !w_done) wvalid  = 1'b1;
            if (order == 2 && w_done && !aw_done) awvalid = 1'b1;
        end
        repeat (bready_delay) @(negedge clk);
        bready = 1'b1;
        do @(posedge clk); while (!bvalid);
        @(negedge clk);
        bready = 1'b0;
    endtask

    task automatic axi_read(input logic [3:0] addr, output logic [31:0] data,
                            input int rready_delay = 0);
        @(negedge clk);
        araddr = addr; arvalid = 1'b1; rready = 1'b0;
        do @(posedge clk); while (!arready);
        @(negedge clk);
        arvalid = 1'b0;
        repeat (rready_delay) @(negedge clk);
        rready = 1'b1;
        do @(posedge clk); while (!rvalid);
        data = rdata;
        @(negedge clk);
        rready = 1'b0;
    endtask

    // ---------------- Utilidades ----------------

    // Espera el flanco de subida del trigger y devuelve el ciclo
    longint last_trig_rise = 0;
    logic trig_q = 0;
    int trig_width = 0, trig_width_last = 0;
    int trig_c_at_rise = -1;
    always @(posedge clk) begin
        trig_q <= trig;
        if (trig && !trig_q) begin
            last_trig_rise = cycle;
            trig_c_at_rise = c;
            trig_width = 1;
        end else if (trig) begin
            trig_width++;
        end else if (trig_q && !trig) begin
            trig_width_last = trig_width;
        end
    end

    task automatic next_trig(output longint t);
        longint prev = last_trig_rise;
        int guard = 0;
        while (last_trig_rise == prev && guard < 100000) begin
            @(posedge clk); guard++;
        end
        t = last_trig_rise;
    endtask

    task automatic wait_periods(input int k);
        longint t;
        repeat (k) next_trig(t);
    endtask

    // Expected threshold: floor(N * (32768 + d) / 65536)
    function automatic int th_of(input int n, input int d);
        longint p = longint'(n) * longint'(32768 + d);
        return int'(p >>> 16);
    endfunction

    function automatic int sat_duty(input int d);
        if (d > 31129)  return 31129;
        if (d < -31129) return -31129;
        return d;
    endfunction

    function automatic int n_of(input int fsel);
        case (fsel)
            0: return 5000;     // 10 kHz
            1: return 3333;     // 15 kHz
            2: return 2500;     // 20 kHz
            default: return 2500;  // 20 kHz (repetido)
        endcase
    endfunction

    // =========================================================================
    initial begin
        logic [31:0] r;
        longint t0, t1, t2;
        int ok, n, d, tha, thb, cnt;
        string s;

        awvalid = 0; wvalid = 0; bready = 0; arvalid = 0; rready = 0;
        awaddr = 0; araddr = 0; wdata = 0; wstrb = 0;
        sync_start = 0; fault = 0;
        rst_n = 1'b0;
        repeat (10) @(posedge clk);
        rst_n = 1'b1;
        repeat (5) @(posedge clk);

        $display("\n==== Testbench PWM Controller ====");

        // ---------------- T1: reset ----------------
        $display("\nT1 - Valores de reset");
        axi_read(A_CTRL, r);  check("T1 CTRL_STATUS = 0", r == 32'h0, $sformatf("leido=0x%08h", r));
        axi_read(A_DUTY, r);  check("T1 DUTY_REF = 0", r == 32'h0, $sformatf("leido=0x%08h", r));
        axi_read(A_DEAD, r);  check("T1 DEADTIME = 100", r == 32'd100, $sformatf("leido=%0d", r));
        axi_read(A_PHASE, r); check("T1 SYNC_PHASE = 1000", r == 32'd1000, $sformatf("leido=%0d", r));
        check("T1 compuertas y trigger en 0", !(gate_ah | gate_al | gate_bh | gate_bl | trig),
              $sformatf("ah=%b al=%b bh=%b bl=%b trig=%b", gate_ah, gate_al, gate_bh, gate_bl, trig));

        // ---------------- T2: escritura y lectura ----------------
        $display("\nT2 - Escritura y lectura de registros (AXI4-Lite)");
        axi_write(A_CTRL, 32'h0000_0004);  // freq_sel = 10, enable = 0
        axi_read(A_CTRL, r);
        check("T2 freq_sel = 10", r[2:1] == 2'b10, $sformatf("leido=0x%08h", r));
        axi_write(A_CTRL, 32'hFFFF_FFFE);  // todo en 1 menos enable
        axi_read(A_CTRL, r);
        check("T2 running (RO) no se puede escribir", r[3] == 1'b0, $sformatf("leido=0x%08h", r));
        check("T2 bits reservados se leen en 0", r[31:7] == 0, $sformatf("leido=0x%08h", r));
        check("T2 gates_en escribible", r[4] == 1'b1, $sformatf("leido=0x%08h", r));
        axi_write(A_CTRL, 32'h0);
        axi_write(A_DUTY, 32'h0000_1234, 4'hF, 1);       // AW primero
        axi_read(A_DUTY, r);
        check("T2 DUTY_REF, AW antes que W", r == 32'h1234, $sformatf("leido=0x%08h", r));
        axi_write(A_DUTY, 32'h0000_0AAA, 4'hF, 2);       // W primero
        axi_read(A_DUTY, r);
        check("T2 DUTY_REF, W antes que AW", r == 32'h0AAA, $sformatf("leido=0x%08h", r));
        axi_write(A_DUTY, 32'h0000_1234);
        axi_write(A_DUTY, 32'h0000_55CC, 4'b0001);       // solo el byte 0
        axi_read(A_DUTY, r);
        check("T2 WSTRB parcial (solo byte 0)", r == 32'h12CC, $sformatf("esperado=0x12CC leido=0x%08h", r));
        axi_write(A_PHASE, 32'h0000_0ABC, 4'hF, 0, 5);   // BREADY retrasado
        axi_read(A_PHASE, r, 6);                         // RREADY retrasado
        check("T2 SYNC_PHASE con BREADY/RREADY retrasados", r == 32'h0ABC, $sformatf("leido=0x%08h", r));
        check("T2 respuestas OKAY", bresp == 2'b00 && rresp == 2'b00, $sformatf("bresp=%b rresp=%b", bresp, rresp));
        axi_write(A_PHASE, 32'd1000);

        // ---------------- T3: saturacion ----------------
        $display("\nT3 - Saturacion de duty_ref y dead_time");
        axi_write(A_DUTY, 32'h0000_7FFF); axi_read(A_DUTY, r);
        check("T3 duty 0x7FFF -> 0x7999", r == 32'h7999, $sformatf("leido=0x%04h", r));
        axi_write(A_DUTY, 32'h0000_8000); axi_read(A_DUTY, r);
        check("T3 duty 0x8000 -> 0x8667", r == 32'h8667, $sformatf("leido=0x%04h", r));
        axi_write(A_DUTY, 32'h0000_8001); axi_read(A_DUTY, r);
        check("T3 duty 0x8001 -> 0x8667", r == 32'h8667, $sformatf("leido=0x%04h", r));
        axi_write(A_DUTY, 32'h0000_4000); axi_read(A_DUTY, r);
        check("T3 duty 0x4000 sin cambio", r == 32'h4000, $sformatf("leido=0x%04h", r));
        axi_write(A_DEAD, 32'd0);    axi_read(A_DEAD, r);
        check("T3 dead_time 0 -> 20", r == 32'd20, $sformatf("leido=%0d", r));
        axi_write(A_DEAD, 32'd2000); axi_read(A_DEAD, r);
        check("T3 dead_time 2000 -> 1023", r == 32'd1023, $sformatf("leido=%0d", r));
        axi_write(A_DEAD, 32'd100);  axi_read(A_DEAD, r);
        check("T3 dead_time 100 sin cambio", r == 32'd100, $sformatf("leido=%0d", r));

        // ---------------- T4: frecuencias ----------------
        $display("\nT4 - Periodo para cada freq_sel (debe ser 2N ciclos)");
        for (int f = 0; f < 4; f++) begin
            axi_write(A_CTRL, (f << 1) | 32'h11);   // enable + gates_en
            wait_periods(2);
            next_trig(t0); next_trig(t1);
            n = n_of(f);
            check($sformatf("T4 freq_sel=%0d (N=%0d, %0d kHz)", f, n, 50000 / n),
                  (t1 - t0) == 2 * n, $sformatf("esperado=%0d medido=%0d", 2 * n, t1 - t0));
        end

        // ---------------- T5: cambio de frecuencia en caliente ----------------
        $display("\nT5 - Cambio de frecuencia con el PWM funcionando");
        axi_write(A_CTRL, 32'h15);              // 20 kHz
        wait_periods(2);
        next_trig(t0);
        repeat (1300) @(posedge clk);          // a mitad de periodo
        axi_write(A_CTRL, 32'h13);              // 15 kHz
        next_trig(t1); next_trig(t2);
        check("T5 periodo en curso termina completo (5000)", (t1 - t0) == 5000, $sformatf("medido=%0d", t1 - t0));
        check("T5 siguiente periodo ya es el nuevo (6666)", (t2 - t1) == 6666, $sformatf("medido=%0d", t2 - t1));

        // ---------------- T6: modulacion unipolar ----------------
        $display("\nT6 - Modulacion unipolar (20 kHz, anchos de pulso por periodo)");
        axi_write(A_DEAD, 32'd100);
        axi_write(A_CTRL, 32'h15);
        for (int k = 0; k < 5; k++) begin
            case (k)
                0: d = -31129;
                1: d = -16384;
                2: d = 0;
                3: d = 16384;
                default: d = 31129;
            endcase
            axi_write(A_DUTY, d & 32'hFFFF);
            wait_periods(3);
            n = 2500; tha = th_of(n, d); thb = th_of(n, -d);
            // Con la portadora 0..N..1, c < th dura exactamente 2*th - 1 ciclos
            ok = (per_sa == 2 * tha - 1) && (per_sb == 2 * thb - 1);
            check($sformatf("T6 m=%+.3f s_a/s_b = 2*th - 1", d / 32768.0), ok,
                  $sformatf("s_a=%0d (esp %0d)  s_b=%0d (esp %0d)", per_sa, 2 * tha - 1, per_sb, 2 * thb - 1));
            // d_A + d_B = 1 salvo el redondeo de los umbrales (<= 4 ciclos = 0.08 %)
            check($sformatf("T6 m=%+.3f d_A + d_B = 1", d / 32768.0),
                  (per_sa + per_sb >= 2 * n - 4) && (per_sa + per_sb <= 2 * n),
                  $sformatf("suma=%0d de %0d", per_sa + per_sb, 2 * n));
            check($sformatf("T6 m=%+.3f gate_ah = s_a - dead_time", d / 32768.0),
                  (per_ah >= per_sa - 100 - 1) && (per_ah <= per_sa - 100 + 1),
                  $sformatf("gate_ah=%0d s_a=%0d", per_ah, per_sa));
        end

        // ---------------- T7: tiempo muerto ----------------
        $display("\nT7 - Tiempo muerto y ausencia de shoot-through");
        axi_write(A_DUTY, 32'h4000);
        for (int k = 0; k < 3; k++) begin
            case (k)
                0: d = 20;
                1: d = 100;
                default: d = 500;
            endcase
            axi_write(A_DEAD, d);
            wait_periods(2);
            dt_reset();
            wait_periods(3);
            check($sformatf("T7 dead_time=%0d ciclos", d),
                  dt_count > 0 && dt_min == d && dt_max == d,
                  $sformatf("transiciones=%0d min=%0d max=%0d", dt_count, dt_min, dt_max));
        end
        // Pulsos mas cortos que el tiempo muerto (20 kHz, m=0.95, dt=1023)
        axi_write(A_CTRL, 32'h15);
        axi_write(A_DUTY, 32'h7999);
        axi_write(A_DEAD, 32'd1023);
        wait_periods(6);
        check("T7 pulsos < tiempo muerto: B no conmuta", per_bh == 0, $sformatf("gate_bh alto=%0d ciclos", per_bh));
        check("T7 sin shoot-through en toda la simulacion", shoot_through == 0, $sformatf("eventos=%0d", shoot_through));
        axi_write(A_DEAD, 32'd100);
        axi_write(A_DUTY, 32'h4000);
        axi_write(A_CTRL, 32'h15);
        wait_periods(2);

        // ---------------- T8: trigger ----------------
        $display("\nT8 - pwm_trigger_o");
        next_trig(t0); next_trig(t1);
        @(posedge clk); repeat (6) @(posedge clk);
        check("T8 un pulso por periodo", (t1 - t0) == 5000, $sformatf("separacion=%0d", t1 - t0));
        check("T8 ancho de 4 ciclos", trig_width_last == 4, $sformatf("ancho=%0d", trig_width_last));
        check("T8 alineado con el valle (sube con c=1)", trig_c_at_rise == 1, $sformatf("c al subir=%0d", trig_c_at_rise));
        axi_write(A_CTRL, 32'h04);              // enable = 0
        t0 = last_trig_rise;
        repeat (12000) @(posedge clk);
        check("T8 sin trigger con el PWM deshabilitado", last_trig_rise == t0 && !trig, "");

        // ---------------- T9: enable ----------------
        $display("\nT9 - Habilitar y deshabilitar");
        axi_write(A_CTRL, 32'h15);
        wait_periods(2);
        repeat (700) @(posedge clk);
        check("T9 compuertas conmutando antes de deshabilitar", gates_on, "");
        @(negedge clk);
        force dut.u_regs.enable_o = 1'b0;      // cambio exacto en un ciclo conocido
        t0 = cycle;
        @(posedge clk); @(posedge clk); #1;
        ok = !(gate_ah | gate_al | gate_bh | gate_bl);
        check("T9 compuertas en 0 en <= 2 ciclos", ok, $sformatf("ah=%b al=%b bh=%b bl=%b", gate_ah, gate_al, gate_bh, gate_bl));
        release dut.u_regs.enable_o;
        axi_write(A_CTRL, 32'h14);
        axi_read(A_CTRL, r);
        check("T9 running = 0", r[3] == 1'b0, $sformatf("CTRL=0x%08h", r));

        // ---------------- T10: gates_en ----------------
        $display("\nT10 - gates_en: portadora y trigger sin compuertas (WARMUP)");
        axi_write(A_CTRL, 32'h05);              // enable, 20 kHz, gates_en = 0
        wait_periods(1);
        next_trig(t0); next_trig(t1);
        check("T10 trigger corre con gates_en = 0", (t1 - t0) == 5000, $sformatf("periodo=%0d", t1 - t0));
        check("T10 compuertas en 0 con gates_en = 0", per_ah == 0 && per_bh == 0 && !(gate_al | gate_bl),
              $sformatf("ah=%0d bh=%0d", per_ah, per_bh));
        repeat (1200) @(posedge clk);           // a mitad de periodo
        axi_write(A_CTRL, 32'h15);              // gates_en = 1
        t0 = cycle;
        while (!gates_on && cycle - t0 < 10000) @(posedge clk);
        check("T10 compuertas se liberan en el pico (c = N)", c == 2499,
              $sformatf("c en el ciclo siguiente=%0d (esperado N-1=2499)", c));
        wait_periods(2);
        repeat (700) @(posedge clk);
        axi_write(A_CTRL, 32'h05);              // gates_en = 0
        @(posedge clk); @(posedge clk); #1;
        check("T10 compuertas se bloquean de inmediato", !(gate_ah | gate_al | gate_bh | gate_bl), "");
        t0 = last_trig_rise;
        wait_periods(1);
        check("T10 trigger sigue despues del bloqueo", last_trig_rise != t0, "");

        // ---------------- T11: sync_start_i ----------------
        $display("\nT11 - sync_start_i (arranque en fase con el AXI Timer)");
        // enable=1, gates_en=0, 20 kHz, SYNC_PHASE = 1000
        repeat (333) @(posedge clk);
        @(negedge clk); sync_start = 1'b1;
        @(negedge clk); sync_start = 1'b0;
        check("T11 portadora cargada en 1000", c == 1000, $sformatf("c=%0d", c));
        t0 = cycle;
        cnt = 0;
        while (c != 0 && cnt < 10000) begin @(negedge clk); cnt++; end
        check("T11 valle 4000 ciclos despues (1500 subiendo + 2500 bajando)", cnt == 4000,
              $sformatf("medido=%0d", cnt));
        // Pulsos periodicos cada 5000 ciclos: la portadora no debe saltar
        ok = 1;
        for (int k = 0; k < 4; k++) begin
            while (cycle < t0 + 5000 * (k + 1) - 1) @(negedge clk);
            sync_start = 1'b1;
            if (c != 999) ok = 0;               // valor natural antes de recargar
            @(negedge clk); sync_start = 1'b0;
            if (c != 1000) ok = 0;
        end
        check("T11 pulsos periodicos sin salto ni deriva", ok, $sformatf("c=%0d", c));
        // Con compuertas activas sync_start_i se ignora
        axi_write(A_CTRL, 32'h15);
        wait_periods(2);
        repeat (100) @(posedge clk);
        @(negedge clk); n = c; sync_start = 1'b1;
        @(negedge clk); sync_start = 1'b0;
        check("T11 ignorado con compuertas activas", c != 1000 && (c == n + 1 || c == n - 1),
              $sformatf("antes=%0d despues=%0d", n, c));
        // SYNC_PHASE configurable
        axi_write(A_CTRL, 32'h05);
        axi_write(A_PHASE, 32'd1234);
        @(negedge clk); sync_start = 1'b1;
        @(negedge clk); sync_start = 1'b0;
        check("T11 SYNC_PHASE configurable (1234)", c == 1234, $sformatf("c=%0d", c));
        axi_write(A_PHASE, 32'd1000);

        // ---------------- T12: fault_i ----------------
        $display("\nT12 - fault_i");
        axi_write(A_CTRL, 32'h15);
        wait_periods(2);
        repeat (700) @(posedge clk);
        @(negedge clk); fault = 1'b1;
        repeat (4) @(posedge clk); #1;
        check("T12 compuertas en 0 en <= 4 ciclos", !(gate_ah | gate_al | gate_bh | gate_bl), "");
        axi_read(A_CTRL, r);
        check("T12 bit fault = 1 y gates_en borrado", r[6] == 1'b1 && r[4] == 1'b0, $sformatf("CTRL=0x%08h", r));
        @(negedge clk); fault = 1'b0;
        wait_periods(2);
        check("T12 no se rearma solo al quitar la falla", !gates_on && per_ah == 0, "");
        axi_write(A_CTRL, 32'h15);
        wait_periods(2);
        check("T12 rearme explicito con gates_en = 1", gates_on && per_ah > 0, "");

        // ---------------- T13: arranque para el bootstrap ----------------
        $display("\nT13 - Arranque: primero conducen los MOSFET inferiores (bootstrap)");
        for (int k = 0; k < 3; k++) begin
            case (k)
                0: d = 0;
                1: d = 14582;    // m = 0.445, punto nominal a 24 V y 1 A
                default: d = 31129;
            endcase
            axi_write(A_CTRL, 32'h05);          // compuertas bloqueadas
            axi_write(A_DUTY, d & 32'hFFFF);
            wait_periods(2);
            repeat (300 + 700 * k) @(posedge clk);
            first_ah = -1; first_al = -1; first_bh = -1; first_bl = -1;
            axi_write(A_CTRL, 32'h15);          // gates_en = 1
            wait_periods(3);
            ok = first_al > 0 && first_bl > 0 &&
                 (first_ah - first_al) >= 2000 && (first_bh - first_bl) >= 2000;
            check($sformatf("T13 m=%+.3f inferiores >= 20 us antes", d / 32768.0), ok,
                  $sformatf("bajo A->alto A: %0d ciclos (%.1f us), bajo B->alto B: %0d ciclos (%.1f us)",
                            first_ah - first_al, (first_ah - first_al) / 100.0,
                            first_bh - first_bl, (first_bh - first_bl) / 100.0));
        end

        check("Global: sin shoot-through en toda la simulacion", shoot_through == 0, $sformatf("eventos=%0d", shoot_through));

        // ---------------- Resumen ----------------
        $display("\n==== Resumen: %0d pruebas, %0d PASS, %0d FAIL ====\n", n_pass + n_fail, n_pass, n_fail);
        if (n_fail != 0) $fatal(1, "Hay pruebas que fallaron");
        $finish;
    end

    // Tiempo maximo de simulacion
    initial begin
        #50ms;
        $display("TIMEOUT de simulacion");
        $fatal(1, "timeout");
    end

endmodule
