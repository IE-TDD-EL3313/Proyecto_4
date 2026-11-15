// -----------------------------------------------------------------------------
// pwm_axi_regs.sv
// Esclavo AXI4-Lite y banco de registros del PWM - Secciones 7.1 y 8.2
//
// Offset  Registro     Campos
// 0x00    CTRL_STATUS  [0] enable (RW)   [2:1] freq_sel (RW)  [3] running (RO)
//                      freq_sel: 00=10 kHz 01=15 kHz 10=20 kHz 11=20 kHz
//                      [4] gates_en (RW) [5] gates_on (RO)    [6] fault (RO)
// 0x04    DUTY_REF     [15:0] duty_ref Q1.15 con signo, saturado a +/-DUTY_MAX
// 0x08    DEADTIME     [9:0] ciclos de 10 ns, saturado a [DT_MIN, DT_MAX]
// 0x0C    SYNC_PHASE   [13:0] posicion de la portadora al recibir sync_start_i
//
// - AW y W se aceptan en cualquier orden. Respuestas siempre OKAY.
// - Direcciones no mapeadas: lectura 0, escritura ignorada.
// - Una falla (fault) borra gates_en: el software debe volver a habilitarlas.
// -----------------------------------------------------------------------------
`timescale 1ns / 1ps

module pwm_axi_regs #(
    parameter int                 ADDR_W      = 4,
    parameter logic signed [15:0] DUTY_MAX    = 16'sd31129,  // ~0.95 en Q1.15
    parameter logic        [9:0]  DT_MIN      = 10'd20,
    parameter logic        [9:0]  DT_MAX      = 10'd1023,
    parameter logic        [9:0]  DT_RESET    = 10'd100,
    parameter logic        [13:0] PHASE_RESET = 14'd1000
) (
    input  logic                clk,
    input  logic                rst_n,
    // Canal AW
    input  logic [ADDR_W-1:0]   awaddr,
    input  logic                awvalid,
    output logic                awready,
    // Canal W
    input  logic [31:0]         wdata,
    input  logic [3:0]          wstrb,
    input  logic                wvalid,
    output logic                wready,
    // Canal B
    output logic [1:0]          bresp,
    output logic                bvalid,
    input  logic                bready,
    // Canal AR
    input  logic [ADDR_W-1:0]   araddr,
    input  logic                arvalid,
    output logic                arready,
    // Canal R
    output logic [31:0]         rdata,
    output logic [1:0]          rresp,
    output logic                rvalid,
    input  logic                rready,
    // Registros hacia el nucleo
    output logic                enable_o,
    output logic                gates_en_o,
    output logic [1:0]          freq_sel_o,
    output logic signed [15:0]  duty_o,
    output logic [9:0]          dead_time_o,
    output logic [13:0]         sync_phase_o,
    // Estado desde el nucleo
    input  logic                running_i,
    input  logic                gates_on_i,
    input  logic                fault_i
);

    localparam logic [1:0] REG_CTRL  = 2'd0;
    localparam logic [1:0] REG_DUTY  = 2'd1;
    localparam logic [1:0] REG_DEAD  = 2'd2;
    localparam logic [1:0] REG_PHASE = 2'd3;

    // ---------------- Vista de 32 bits de cada registro ----------------
    logic [31:0] view_ctrl, view_duty, view_dead, view_phase;
    assign view_ctrl  = {25'd0, fault_i, gates_on_i, gates_en_o, running_i, freq_sel_o, enable_o};
    assign view_duty  = {16'd0, duty_o};
    assign view_dead  = {22'd0, dead_time_o};
    assign view_phase = {18'd0, sync_phase_o};

    logic [31:0] view_wr, view_rd;  // registro direccionado en escritura y lectura
    logic [1:0]  idx_wr, idx_rd;
    assign idx_rd = araddr[3:2];
    always_comb begin
        case (idx_wr)
            REG_CTRL: view_wr = view_ctrl;
            REG_DUTY: view_wr = view_duty;
            REG_DEAD: view_wr = view_dead;
            default:  view_wr = view_phase;
        endcase
        case (idx_rd)
            REG_CTRL: view_rd = view_ctrl;
            REG_DUTY: view_rd = view_duty;
            REG_DEAD: view_rd = view_dead;
            default:  view_rd = view_phase;
        endcase
    end

    function automatic logic [31:0] apply_strb(input logic [31:0] old_v,
                                               input logic [31:0] new_v,
                                               input logic [3:0]  strb);
        for (int i = 0; i < 4; i++)
            apply_strb[8*i +: 8] = strb[i] ? new_v[8*i +: 8] : old_v[8*i +: 8];
    endfunction

    function automatic logic signed [15:0] sat_duty(input logic signed [15:0] d);
        if      (d >  DUTY_MAX) sat_duty =  DUTY_MAX;
        else if (d < -DUTY_MAX) sat_duty = -DUTY_MAX;
        else                    sat_duty =  d;
    endfunction

    function automatic logic [9:0] sat_dead(input logic [31:0] t);
        if      (t > 32'(DT_MAX)) sat_dead = DT_MAX;
        else if (t < 32'(DT_MIN)) sat_dead = DT_MIN;
        else                      sat_dead = t[9:0];
    endfunction

    // ---------------- Canal de escritura ----------------
    logic                have_aw, have_w;
    logic [ADDR_W-1:0]   aw_addr_q;
    logic [31:0]         w_data_q;
    logic [3:0]          w_strb_q;

    logic aw_hs, w_hs, do_write;
    logic [ADDR_W-1:0] wr_addr;
    logic [31:0]       wr_data;
    logic [3:0]        wr_strb;
    logic [31:0]       wr_val;

    assign awready  = !have_aw && !bvalid;
    assign wready   = !have_w  && !bvalid;
    assign aw_hs    = awvalid && awready;
    assign w_hs     = wvalid  && wready;
    assign wr_addr  = have_aw ? aw_addr_q : awaddr;
    assign wr_data  = have_w  ? w_data_q  : wdata;
    assign wr_strb  = have_w  ? w_strb_q  : wstrb;
    assign do_write = (have_aw || aw_hs) && (have_w || w_hs) && !bvalid;
    assign idx_wr   = wr_addr[3:2];
    assign wr_val   = apply_strb(view_wr, wr_data, wr_strb);
    assign bresp    = 2'b00;  // OKAY

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            have_aw      <= 1'b0;
            have_w       <= 1'b0;
            aw_addr_q    <= '0;
            w_data_q     <= '0;
            w_strb_q     <= '0;
            bvalid       <= 1'b0;
            enable_o     <= 1'b0;
            gates_en_o   <= 1'b0;
            freq_sel_o   <= 2'b00;
            duty_o       <= '0;
            dead_time_o  <= DT_RESET;
            sync_phase_o <= PHASE_RESET;
        end else begin
            if (aw_hs) begin
                have_aw   <= 1'b1;
                aw_addr_q <= awaddr;
            end
            if (w_hs) begin
                have_w   <= 1'b1;
                w_data_q <= wdata;
                w_strb_q <= wstrb;
            end

            if (do_write) begin
                have_aw <= 1'b0;
                have_w  <= 1'b0;
                bvalid  <= 1'b1;
                case (wr_addr[3:2])
                    REG_CTRL: begin
                        enable_o   <= wr_val[0];
                        freq_sel_o <= wr_val[2:1];
                        gates_en_o <= wr_val[4];
                    end
                    REG_DUTY:  duty_o       <= sat_duty(wr_val[15:0]);
                    REG_DEAD:  dead_time_o  <= sat_dead(wr_val);
                    REG_PHASE: sync_phase_o <= wr_val[13:0];
                    default: ;
                endcase
            end else if (bvalid && bready) begin
                bvalid <= 1'b0;
            end

            // La falla domina sobre una escritura simultanea
            if (fault_i) gates_en_o <= 1'b0;
        end
    end

    // ---------------- Canal de lectura ----------------
    assign arready = !rvalid;
    assign rresp   = 2'b00;  // OKAY

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            rvalid <= 1'b0;
            rdata  <= '0;
        end else if (arvalid && arready) begin
            rvalid <= 1'b1;
            rdata  <= view_rd;
        end else if (rvalid && rready) begin
            rvalid <= 1'b0;
        end
    end

endmodule
