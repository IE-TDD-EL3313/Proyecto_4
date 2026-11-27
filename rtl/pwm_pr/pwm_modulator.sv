// Seccion C: modulacion PWM unipolar (sintetizable).
// duty_ref: entero CON SIGNO en unidades de 1/1000, rango [-1000,+1000].
// Los umbrales de ambos brazos se actualizan juntos con update_event.
//
// Calculo segmentado (pipeline) para cumplir 100 MHz:
//   duty = floor(period_max * (DUTY_SCALE +/- ref_sat) / (2*DUTY_SCALE))
// La division entre la constante 2*DUTY_SCALE se reemplaza por una
// multiplicacion por su reciproco y un desplazamiento:
//   x / 2000 == (x * RECIP) >> RECIP_SHIFT
// Con RECIP_SHIFT = 40 el resultado es exacto (igual a la division entera)
// para todo x < 4.9e9; el producto maximo con WIDTH=16 es 131 070 000.
//
// Etapas (todas se registran en cada ciclo de reloj):
//   S1: saturacion de duty_ref y ganancias gain_a/gain_b; copia de period_max
//   S2: prod = period_max * gain
//   S3: prod * RECIP
//   S4: registro extra para que Vivado use los registros internos del DSP
// Latencia: PIPE_LAT = 4 ciclos.
//
// update_event llega en el mismo flanco que el nuevo period_max. Por eso los
// umbrales se capturan con update_event retrasado PIPE_LAT ciclos
// (update_apply): asi el duty aplicado siempre corresponde al period_max
// vigente, incluso en el primer periodo despues de un cambio de frecuencia.
module pwm_modulator #(
    parameter int unsigned WIDTH = pwm_config_pkg::PWM_WIDTH,
    parameter int unsigned DUTY_SCALE = pwm_config_pkg::DUTY_SCALE
)(
    input  logic                   clk,
    input  logic                   rst_n,
    input  logic                   enable,
    input  logic signed [15:0]     duty_ref,
    input  logic [WIDTH-1:0]       period_max,
    input  logic [WIDTH-1:0]       carrier_count,
    input  logic                   update_event,
    output logic [WIDTH-1:0]       duty_a,
    output logic [WIDTH-1:0]       duty_b,
    output logic                   pwm_raw_a,
    output logic                   pwm_raw_b
);
    // Anchos y constantes del calculo
    localparam int unsigned GAIN_W      = $clog2(2*DUTY_SCALE + 1); // 11 para 1000
    localparam int unsigned PROD_W      = WIDTH + GAIN_W;           // 27 para WIDTH=16
    localparam int unsigned RECIP_SHIFT = 40;
    localparam longint unsigned RECIP =
        ((64'd1 << RECIP_SHIFT) + 64'(DUTY_SCALE)) / (64'd2 * 64'(DUTY_SCALE));
    localparam int unsigned RECIP_W     = $clog2(RECIP + 1);        // 30 para 1000
    localparam int unsigned MUL_W       = PROD_W + RECIP_W;         // 57
    localparam int unsigned PIPE_LAT    = 4;

    // ------------------------------------------------------------------------
    // S1: saturacion y ganancias (combinacional -> registro)
    // ------------------------------------------------------------------------
    logic signed [16:0]   ref_ext;   // duty_ref extendido con signo
    logic signed [16:0]   ref_sat;
    logic [GAIN_W-1:0]    gain_a_c, gain_b_c;

    assign ref_ext = duty_ref;

    always_comb begin
        if (ref_ext > $signed(17'(DUTY_SCALE)))
            ref_sat = 17'(DUTY_SCALE);
        else if (ref_ext < -$signed(17'(DUTY_SCALE)))
            ref_sat = -$signed(17'(DUTY_SCALE));
        else
            ref_sat = ref_ext;

        gain_a_c = GAIN_W'($signed(17'(DUTY_SCALE)) + ref_sat);
        gain_b_c = GAIN_W'($signed(17'(DUTY_SCALE)) - ref_sat);
    end

    // Registros de datos del pipeline: sin reset, para que Vivado pueda
    // ubicarlos dentro de los DSP48. Solo se usan cuando update_apply = 1,
    // y update_apply si tiene reset, asi que nunca se aplica un valor X.
    logic [GAIN_W-1:0]  gain_a_s1, gain_b_s1;
    logic [WIDTH-1:0]   period_s1;
    logic [PROD_W-1:0]  prod_a_s2, prod_b_s2;
    logic [MUL_W-1:0]   mul_a_s3, mul_b_s3;
    logic [MUL_W-1:0]   mul_a_s4, mul_b_s4;

    always_ff @(posedge clk) begin
        // S1
        gain_a_s1 <= gain_a_c;
        gain_b_s1 <= gain_b_c;
        period_s1 <= period_max;
        // S2
        prod_a_s2 <= PROD_W'(period_s1) * PROD_W'(gain_a_s1);
        prod_b_s2 <= PROD_W'(period_s1) * PROD_W'(gain_b_s1);
        // S3
        mul_a_s3  <= MUL_W'(prod_a_s2) * MUL_W'(RECIP);
        mul_b_s3  <= MUL_W'(prod_b_s2) * MUL_W'(RECIP);
        // S4
        mul_a_s4  <= mul_a_s3;
        mul_b_s4  <= mul_b_s3;
    end

    logic [WIDTH-1:0] duty_a_next, duty_b_next;
    assign duty_a_next = mul_a_s4[RECIP_SHIFT +: WIDTH];
    assign duty_b_next = mul_b_s4[RECIP_SHIFT +: WIDTH];

    // ------------------------------------------------------------------------
    // update_event retrasado PIPE_LAT ciclos
    // ------------------------------------------------------------------------
    logic [PIPE_LAT-1:0] update_pipe;
    logic                update_apply;
    assign update_apply = update_pipe[PIPE_LAT-1];

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            update_pipe <= '0;
        else if (!enable)
            update_pipe <= '0;
        else
            update_pipe <= {update_pipe[PIPE_LAT-2:0], update_event};
    end

    // Registro doble: un cambio de duty_ref no modifica el PWM hasta que se
    // aplica el update_event (retrasado).
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            duty_a <= '0;
            duty_b <= '0;
        end else if (!enable) begin
            duty_a <= '0;
            duty_b <= '0;
        end else if (update_apply) begin
            duty_a <= duty_a_next;
            duty_b <= duty_b_next;
        end
    end

    // Ordenes PRELIMINARES: la seccion D aplicara tiempo muerto.
    always_comb begin
        pwm_raw_a = enable && rst_n && (carrier_count < duty_a);
        pwm_raw_b = enable && rst_n && (carrier_count < duty_b);
    end
endmodule
