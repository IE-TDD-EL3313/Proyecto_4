// ============================================================================
// SECCION A - Registros del periferico PWM con interfaz esclava AXI4-Lite
// ============================================================================
// Este modulo NO implementa MicroBlaze ni genera directamente las ondas PWM.
// Recibe transacciones AXI4-Lite de un maestro (MicroBlaze o testbench), guarda
// la configuracion y entrega cuatro señales hacia las secciones B, C y D.
//
// Mapa de registros (offsets respecto de la direccion base del IP):
//   0x00 CTRL/STATUS: bit 0 enable (RW), bits 2:1 freq_sel (RW),
//                     bit 3 running (solo lectura, viene del nucleo PWM).
//   0x04 DUTY_REF:    bits 15:0 referencia con signo, complemento a dos (RW).
//   0x08 DEADTIME:    bits 15:0 duracion en ciclos de reloj (RW).
//
// AW = direccion de escritura; W = dato; B = respuesta de escritura.
// AR = direccion de lectura;   R = dato + respuesta de lectura.
// Un canal transfiere informacion en un flanco ascendente cuando VALID y READY
// son 1 simultaneamente. Las direcciones AW y los datos W pueden llegar
// separados en el tiempo, por eso se guardan en registros temporales.
//
// Este es RTL sintetizable. Las instrucciones de simulacion estan en el TB.
// ============================================================================
module pwm_axi_regs #(
    // Valor inicial del registro DEADTIME tras un reset. A 100 MHz, 50 ciclos
    // equivalen a 500 ns; es provisional, depende de los drivers reales.
    parameter [15:0] DEADTIME_RESET = 16'd50
)(
    input  wire         s_axi_aclk,      // Reloj del bus AXI y del periferico
    input  wire         s_axi_aresetn,   // Reset activo en 0

    // Canal AW: el maestro indica QUE direccion desea escribir.
    input  wire [5:0]   s_axi_awaddr,
    input  wire         s_axi_awvalid,
    output wire         s_axi_awready,

    // Canal W: el maestro entrega EL DATO y que bytes son validos.
    input  wire [31:0]  s_axi_wdata,
    input  wire [3:0]   s_axi_wstrb,    // Un bit por byte del bus de 32 bits
    input  wire         s_axi_wvalid,
    output wire         s_axi_wready,

    // Canal B: el periferico confirma si la escritura termino bien.
    output reg  [1:0]   s_axi_bresp,
    output reg          s_axi_bvalid,
    input  wire         s_axi_bready,

    // Canal AR: el maestro indica QUE direccion desea leer.
    input  wire [5:0]   s_axi_araddr,
    input  wire         s_axi_arvalid,
    output wire         s_axi_arready,

    // Canal R: el periferico devuelve el contenido y resultado de lectura.
    output reg  [31:0]  s_axi_rdata,
    output reg  [1:0]   s_axi_rresp,
    output reg          s_axi_rvalid,
    input  wire         s_axi_rready,

    // Interfaz funcional hacia el nucleo PWM.
    input  wire         running_i,       // Estado generado por la seccion B
    output wire         enable_o,        // Habilita B y las salidas de D
    output wire [1:0]   freq_sel_o,      // Selector de 3 frecuencias en B
    output wire signed [15:0] duty_ref_o,// Referencia de modulacion hacia C
    output wire [15:0]  deadtime_count_o // Tiempo muerto hacia D
);

    // Offsets de los registros. No son direcciones absolutas del sistema.
    localparam [5:0] ADDR_CTRL = 6'h00;
    localparam [5:0] ADDR_DUTY = 6'h04;
    localparam [5:0] ADDR_DEAD = 6'h08;

    // Codigos estandar de respuesta AXI.
    localparam [1:0] RESP_OKAY   = 2'b00; // Operacion correcta
    localparam [1:0] RESP_SLVERR = 2'b10; // Direccion no implementada

    // Registros de configuracion visibles desde software.
    reg [2:0]  ctrl_reg; // Solo bits [2:0]; running NO se almacena aqui
    reg [15:0] duty_reg;
    reg [15:0] dead_reg;

    // Buffers para permitir que AW y W lleguen en ciclos diferentes.
    reg [5:0]  awaddr_hold;
    reg [31:0] wdata_hold;
    reg [3:0]  wstrb_hold;
    reg        aw_pending; // Ya llego una direccion, falta procesarla
    reg        w_pending;  // Ya llego un dato, falta procesarlo

    // READY=1 solo si existe espacio para aceptar una nueva transferencia.
    // Aqui se permite como maximo una escritura pendiente y una respuesta B.
    assign s_axi_awready = s_axi_aresetn && !aw_pending && !s_axi_bvalid;
    assign s_axi_wready  = s_axi_aresetn && !w_pending  && !s_axi_bvalid;
    assign s_axi_arready = s_axi_aresetn && !s_axi_rvalid;

    // Los registros internos alimentan continuamente las otras secciones.
    assign enable_o         = ctrl_reg[0];
    assign freq_sel_o       = ctrl_reg[2:1];
    assign duty_ref_o       = $signed(duty_reg);
    assign deadtime_count_o = dead_reg;

    // Funcion combinacional: actualiza unicamente los bytes habilitados.
    // Ej.: WSTRB=4'b0001 cambia solo bits [7:0] de un registro.
    function automatic [31:0] apply_strobes(
        input [31:0] old_val,
        input [31:0] new_val,
        input [3:0]  strobes
    );
        integer i;
        begin
            apply_strobes = old_val;
            for (i = 0; i < 4; i = i + 1)
                if (strobes[i])
                    apply_strobes[i*8 +: 8] = new_val[i*8 +: 8];
        end
    endfunction

    reg [31:0] merged; // Resultado temporal del enmascaramiento de bytes

    // ------------------------------------------------------------------------
    // LOGICA DE ESCRITURA: canales AW, W y B
    // ------------------------------------------------------------------------
    always @(posedge s_axi_aclk or negedge s_axi_aresetn) begin
        if (!s_axi_aresetn) begin
            ctrl_reg     <= 3'b000;       // PWM deshabilitado y freq_sel=0
            duty_reg     <= 16'h0000;     // Referencia inicial cero
            dead_reg     <= DEADTIME_RESET;
            awaddr_hold  <= 6'b0;
            wdata_hold   <= 32'b0;
            wstrb_hold   <= 4'b0;
            aw_pending   <= 1'b0;
            w_pending    <= 1'b0;
            s_axi_bvalid <= 1'b0;
            s_axi_bresp  <= RESP_OKAY;
        end else begin
            // Guardar direccion si se completo handshake AW.
            if (s_axi_awvalid && s_axi_awready) begin
                awaddr_hold <= s_axi_awaddr;
                aw_pending  <= 1'b1;
            end

            // Guardar dato y mascara si se completo handshake W.
            if (s_axi_wvalid && s_axi_wready) begin
                wdata_hold <= s_axi_wdata;
                wstrb_hold <= s_axi_wstrb;
                w_pending  <= 1'b1;
            end

            // Ejecutar UNA escritura cuando ya tenemos direccion y dato.
            // Ocurre despues de guardarlos, sin exigir AWVALID y WVALID juntos.
            if (aw_pending && w_pending && !s_axi_bvalid) begin
                case (awaddr_hold)
                    ADDR_CTRL: begin
                        merged = apply_strobes({29'b0, ctrl_reg},
                                               wdata_hold, wstrb_hold);
                        // Se ignora bit 3: running_i es estado de solo lectura.
                        ctrl_reg <= merged[2:0];
                        s_axi_bresp <= RESP_OKAY;
                    end
                    ADDR_DUTY: begin
                        merged = apply_strobes({16'b0, duty_reg},
                                               wdata_hold, wstrb_hold);
                        duty_reg <= merged[15:0];
                        s_axi_bresp <= RESP_OKAY;
                    end
                    ADDR_DEAD: begin
                        merged = apply_strobes({16'b0, dead_reg},
                                               wdata_hold, wstrb_hold);
                        dead_reg <= merged[15:0];
                        s_axi_bresp <= RESP_OKAY;
                    end
                    default: begin
                        // No se modifica ningun registro.
                        s_axi_bresp <= RESP_SLVERR;
                    end
                endcase
                aw_pending   <= 1'b0;
                w_pending    <= 1'b0;
                s_axi_bvalid <= 1'b1;
            end

            // Mantener BVALID hasta que el maestro acepte la respuesta.
            if (s_axi_bvalid && s_axi_bready)
                s_axi_bvalid <= 1'b0;
        end
    end

    // ------------------------------------------------------------------------
    // LOGICA DE LECTURA: canales AR y R
    // ------------------------------------------------------------------------
    always @(posedge s_axi_aclk or negedge s_axi_aresetn) begin
        if (!s_axi_aresetn) begin
            s_axi_rvalid <= 1'b0;
            s_axi_rdata  <= 32'b0;
            s_axi_rresp  <= RESP_OKAY;
        end else begin
            // El maestro solicita una lectura; decodificar direccion.
            if (s_axi_arvalid && s_axi_arready) begin
                s_axi_rvalid <= 1'b1;
                case (s_axi_araddr)
                    ADDR_CTRL: begin
                        // Estado running en bit 3 y CTRL en bits [2:0].
                        s_axi_rdata <= {28'b0, running_i, ctrl_reg};
                        s_axi_rresp <= RESP_OKAY;
                    end
                    ADDR_DUTY: begin
                        // Se devuelve la codificacion de 16 bits sin extender signo.
                        s_axi_rdata <= {16'b0, duty_reg};
                        s_axi_rresp <= RESP_OKAY;
                    end
                    ADDR_DEAD: begin
                        s_axi_rdata <= {16'b0, dead_reg};
                        s_axi_rresp <= RESP_OKAY;
                    end
                    default: begin
                        s_axi_rdata <= 32'b0;
                        s_axi_rresp <= RESP_SLVERR;
                    end
                endcase
            end else if (s_axi_rvalid && s_axi_rready) begin
                // RDATA y RRESP permanecen estables mientras RREADY sea 0.
                s_axi_rvalid <= 1'b0;
            end
        end
    end
endmodule
