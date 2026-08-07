// =========================================================
// ADC CLOCK GENERATOR
// =========================================================
// For ADC_SAMPLE_HZ below FPGA_CLK_HZ, an integer divider creates
// the ADC clock and one-cycle sample ticks.
//
// For ADC_SAMPLE_HZ equal to FPGA_CLK_HZ, direct-clock mode is used:
//   adc_aclk        = clk
//   adc_sample_tick = active on every FPGA clock cycle
//
// This allows the Tang Nano 9K 27 MHz clock to sample the AD9226 at
// 27 MSPS without calculating an invalid divider value of zero.
// =========================================================

module adc_clk_gen #(
    parameter integer FPGA_CLK_HZ   = 27_000_000,
    parameter integer ADC_SAMPLE_HZ = 2_000_000
)(
    input  wire clk,
    output wire adc_aclk,
    output wire adc_sample_tick
);

    generate
        if (ADC_SAMPLE_HZ >= FPGA_CLK_HZ) begin : gen_direct_clock
            // Direct 27 MSPS mode. The AD9226 is clocked directly from
            // the FPGA system clock and one sample is accepted per cycle.
            assign adc_aclk = clk;
            assign adc_sample_tick = 1'b1;
        end else begin : gen_divided_clock
            localparam integer ADC_HALF_PERIOD_COUNT =
                FPGA_CLK_HZ / (ADC_SAMPLE_HZ * 2);

            reg [31:0] adc_clk_count = 32'd0;
            reg adc_clk = 1'b0;
            reg sample_tick = 1'b0;

            assign adc_aclk = adc_clk;
            assign adc_sample_tick = sample_tick;

            always @(posedge clk) begin
                sample_tick <= 1'b0;

                if (adc_clk_count == ADC_HALF_PERIOD_COUNT - 1) begin
                    adc_clk_count <= 32'd0;
                    adc_clk <= ~adc_clk;

                    if (adc_clk == 1'b0) begin
                        sample_tick <= 1'b1;
                    end
                end else begin
                    adc_clk_count <= adc_clk_count + 32'd1;
                end
            end
        end
    endgenerate

endmodule
