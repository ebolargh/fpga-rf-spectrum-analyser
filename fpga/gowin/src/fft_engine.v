// =========================================================
// FFT ENGINE
// AD9226 parallel ADC samples -> 256-sample buffer -> FFT -> bins
// =========================================================

module fft_engine (
    input  wire        clk,
    input  wire [11:0] adc_data,
    input  wire        adc_sample_tick,

    output reg [15:0]  bin_word  = 16'd0,
    output reg         bin_valid = 1'b0,
    input  wire        bin_ready
);

    reg signed [15:0] xn_re_i = 16'sd0;
    reg signed [15:0] xn_im_i = 16'sd0;
    reg               start_i = 1'b0;

    wire rst_i;
    assign rst_i = 1'b0;

    reg signed [15:0] sample_buffer [0:255];

    reg [7:0] write_idx   = 8'd0;
    reg [7:0] read_idx    = 8'd0;
    reg       buffer_full = 1'b0;

    wire signed [15:0] converted_sample;
    assign converted_sample = $signed({1'b0, adc_data}) - 16'sd2048;

    function signed [15:0] apply_window;
        input signed [15:0] sample;
        input [7:0] idx;

        reg signed [15:0] win;
        reg signed [31:0] product;

        begin
            if (idx < 8'd128)
                win = {1'b0, idx, 7'd0};
            else
                win = {1'b0, (8'd255 - idx), 7'd0};

            product = sample * win;
            apply_window = product[30:15];
        end
    endfunction

    wire [7:0] idx_o;
    wire [18:0] xk_re_o;
    wire [18:0] xk_im_o;
    wire sod_o;
    wire ipd_o;
    wire eod_o;
    wire busy_o;
    wire soud_o;
    wire opd_o;
    wire eoud_o;

    FFT_Top FFT_core (
        .idx   (idx_o),
        .xk_re (xk_re_o),
        .xk_im (xk_im_o),
        .ipd   (ipd_o),
        .eod   (eod_o),
        .opd   (opd_o),
        .eoud  (eoud_o),
        .soud  (soud_o),
        .busy  (busy_o),
        .sod   (sod_o),
        .xn_re (xn_re_i),
        .xn_im (xn_im_i),
        .start (start_i),
        .clk   (clk),
        .rst   (rst_i)
    );

    reg [19:0] bins [0:255];

    function [18:0] abs19;
        input [18:0] x;
        begin
            if (x[18])
                abs19 = ~x + 19'd1;
            else
                abs19 = x;
        end
    endfunction

    wire [19:0] mag_sum;
    assign mag_sum = {1'b0, abs19(xk_re_o)} + {1'b0, abs19(xk_im_o)};

    localparam ST_IDLE        = 3'd0;
    localparam ST_START       = 3'd1;
    localparam ST_WAIT_SOD    = 3'd2;
    localparam ST_FEED_INPUT  = 3'd3;
    localparam ST_WAIT_EOUD   = 3'd4;
    localparam ST_SEND_HEADER = 3'd5;
    localparam ST_SEND_BINS   = 3'd6;
    localparam ST_DONE        = 3'd7;

    reg [2:0] state    = ST_IDLE;
    reg [7:0] send_idx = 8'd0;

    localparam [15:0] HEADER_WORD = 16'hAA55;

    always @(posedge clk) begin
        start_i   <= 1'b0;
        bin_valid <= 1'b0;

        if (opd_o) begin
            bins[idx_o] <= mag_sum;
        end

        case (state)
            ST_IDLE: begin
                if (!buffer_full && adc_sample_tick) begin
                    sample_buffer[write_idx] <= converted_sample;

                    if (write_idx == 8'd255) begin
                        write_idx   <= 8'd0;
                        buffer_full <= 1'b1;
                    end else begin
                        write_idx <= write_idx + 8'd1;
                    end
                end

                if (buffer_full) begin
                    read_idx <= 8'd0;
                    xn_re_i  <= apply_window(sample_buffer[0], 8'd0);
                    xn_im_i  <= 16'sd0;
                    state    <= ST_START;
                end
            end

            ST_START: begin
                start_i <= 1'b1;
                state   <= ST_WAIT_SOD;
            end

            ST_WAIT_SOD: begin
                if (sod_o) begin
                    xn_re_i  <= apply_window(sample_buffer[0], 8'd0);
                    xn_im_i  <= 16'sd0;
                    read_idx <= 8'd1;
                    state    <= ST_FEED_INPUT;
                end
            end

            ST_FEED_INPUT: begin
                if (ipd_o) begin
                    xn_re_i  <= apply_window(sample_buffer[read_idx], read_idx);
                    xn_im_i  <= 16'sd0;
                    read_idx <= read_idx + 8'd1;
                end

                if (eod_o) begin
                    buffer_full <= 1'b0;
                    write_idx   <= 8'd0;
                    state       <= ST_WAIT_EOUD;
                end
            end

            ST_WAIT_EOUD: begin
                if (eoud_o) begin
                    send_idx <= 8'd0;
                    state    <= ST_SEND_HEADER;
                end
            end

            ST_SEND_HEADER: begin
                bin_valid <= 1'b1;
                bin_word  <= HEADER_WORD;

                if (bin_valid && bin_ready) begin
                    send_idx <= 8'd0;
                    state    <= ST_SEND_BINS;
                end
            end

            ST_SEND_BINS: begin
                bin_valid <= 1'b1;
                bin_word  <= bins[send_idx][19:4];

                if (bin_valid && bin_ready) begin
                    if (send_idx == 8'd255) begin
                        state <= ST_DONE;
                    end else begin
                        send_idx <= send_idx + 8'd1;
                    end
                end
            end

            ST_DONE: begin
                bin_valid <= 1'b0;
                bin_word  <= 16'd0;
                state     <= ST_IDLE;
            end

            default: begin
                state <= ST_IDLE;
            end
        endcase
    end

endmodule
