// =========================================================
// ADC CLOCK GENERATOR
// =========================================================
// Set ADC_SAMPLE_HZ to the ADC sample rate you want.
// Example:
//   1_000_000 = about 1 MHz
//   2_000_000 = about 2 MHz
//   5_000_000 = about 5 MHz
//
// Actual ADC clock depends on integer division:
//   ADC clock = FPGA_CLK_HZ / (2 * ADC_HALF_PERIOD_COUNT)
// =========================================================

module adc_clk_gen#(
    parameter integer FPGA_CLK_HZ    = 27_000_000,
    parameter integer ADC_SAMPLE_HZ  = 2_000_000
    )(
        input wire clk,
        output wire adc_aclk,
        output reg adc_sample_tick = 1'b0
    );

    localparam integer ADC_HALF_PERIOD_COUNT =
        FPGA_CLK_HZ / (ADC_SAMPLE_HZ * 2);

    reg [31:0] adc_clk_count  = 32'd0;
    reg adc_clk = 1'b0;

    assign adc_aclk = adc_clk;

    always @(posedge clk) begin
        adc_sample_tick <= 1'b0;

        if (adc_clk_count == ADC_HALF_PERIOD_COUNT - 1) begin
            adc_clk_count <= 32'd0;
            adc_clk <= ~adc_clk;

            if (adc_clk == 1'b0) begin
                adc_sample_tick <= 1'b1;
            end
        end else begin
            adc_clk_count <= adc_clk_count + 32'd1;
        end
    end
endmodule
