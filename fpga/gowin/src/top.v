// 28 06 2026

// ================================================================
// TOP MODULE
// ================================================================
//
//   - fft_engine      → generates FFT bin data
//   - baud_gen        → creates UART timing (baud_tick)
//   - fft_uart_tx     → formats and transmits FFT data over UART
//
// DATA FLOW:
//   FFT Engine → bin_word/bin_valid → UART TX → serial output
//
// CLOCK:
//   - All modules run from the same system clock (clk)
//
// OUTPUT:
//   - adc_aclk : Generates the clk signal for ADC sampling rate  
//   - uart_tx : serial data stream containing framed FFT bins ( uart_tx.v )
// ================================================================
// ================================================================
// TOP MODULE
// AD9226 parallel ADC -> 256-sample buffer -> FFT -> UART to PC
// ================================================================

module top (
    input  wire        clk,        // Tang Nano 9K 27 MHz clock, pin 52
    input  wire [11:0] adc_data,   // AD9226 AD0..AD11, pins 25..36

    output wire        adc_aclk,   // FPGA-generated ADC clock, pin 37
    output wire        uart_tx,     // UART to PC, pin 17
    output wire        uart_tx_esp32 // UART to ESP32, pin 38

);

    // =========================================================
    // ADC CLOCK GENERATOR
    // =========================================================
    // Set ADC_SAMPLE_HZ to the ADC sample rate you want.
    //
    // Actual ADC clock depends on integer division:
    //   ADC clock = FPGA_CLK_HZ / (2 * ADC_HALF_PERIOD_COUNT)
    // =========================================================

    wire adc_aclk;          //  actual clk signal sent to adc
    wire adc_sample_tick;   //  1 clk cycle pulse. says adc sample should now be captured

    adc_clk_gen #(
        .FPGA_CLK_HZ(27_000_000),
        .ADC_SAMPLE_HZ(2_000_000)   // Change to modify ADC clk speed
    ) adc_clk_gen_inst (
        .clk        (clk),
        .adc_aclk   (adc_aclk),
        .adc_sample_tick (adc_sample_tick)
    );

    assign uart_tx_esp32 = uart_tx; // Mirror the FPGA UART output for the ESP32.


    // =========================================================
    // FFT ENGINE
    // =========================================================

    wire [15:0] bin_word;
    wire        bin_valid;
    wire        bin_ready;

    fft_engine u_fft_engine (
        .clk             (clk),
        .adc_data        (adc_data),
        .adc_sample_tick (adc_sample_tick),

        .bin_word        (bin_word),
        .bin_valid       (bin_valid),
        .bin_ready       (bin_ready)
    );

    // =========================================================
    // BAUD GENERATOR
    // =========================================================

    wire baud_tick;

    baud_gen #(
        .BAUD_DIV(234)
    ) u_baud (
        .clk       (clk),
        .baud_tick (baud_tick)
    );

    // =========================================================
    // FFT UART TRANSMITTER
    // =========================================================

    fft_uart_tx u_fft_uart_tx (
        .clk       (clk),
        .baud_tick (baud_tick),

        .bin_word  (bin_word),
        .bin_valid (bin_valid),
        .bin_ready (bin_ready),

        .tx        (uart_tx)
    );

endmodule
