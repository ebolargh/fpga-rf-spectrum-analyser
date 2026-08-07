module fft_uart_tx (
    input  wire        clk,
    input  wire        baud_tick,

    input  wire [15:0] bin_word,
    input  wire        bin_valid,
    output wire        bin_ready,

    output wire        tx
);

    wire      uart_ready;
    reg       uart_valid = 1'b0;
    reg [7:0] uart_data  = 8'h00;

    uart_tx u_uart (
        .clk      (clk),
        .baud_tick(baud_tick),
        .data     (uart_data),
        .valid    (uart_valid),
        .ready    (uart_ready),
        .tx       (tx)
    );

    reg [15:0] current_word = 16'd0;
    reg        have_word    = 1'b0;

    assign bin_ready = !have_word;

    localparam SEND_HIGH      = 3'd0;
    localparam WAIT_HIGH_BUSY = 3'd1;
    localparam WAIT_HIGH_DONE = 3'd2;
    localparam SEND_LOW       = 3'd3;
    localparam WAIT_LOW_BUSY  = 3'd4;
    localparam WAIT_LOW_DONE  = 3'd5;

    reg [2:0] state = SEND_HIGH;

    always @(posedge clk) begin
        uart_valid <= 1'b0;

        if (bin_valid && !have_word) begin
            current_word <= bin_word;
            have_word    <= 1'b1;
        end

        case (state)
            SEND_HIGH: begin
                if (have_word && uart_ready) begin
                    uart_data  <= current_word[15:8];
                    uart_valid <= 1'b1;
                    state      <= WAIT_HIGH_BUSY;
                end
            end

            WAIT_HIGH_BUSY: begin
                if (!uart_ready)
                    state <= WAIT_HIGH_DONE;
            end

            WAIT_HIGH_DONE: begin
                if (uart_ready)
                    state <= SEND_LOW;
            end

            SEND_LOW: begin
                if (have_word && uart_ready) begin
                    uart_data  <= current_word[7:0];
                    uart_valid <= 1'b1;
                    state      <= WAIT_LOW_BUSY;
                end
            end

            WAIT_LOW_BUSY: begin
                if (!uart_ready)
                    state <= WAIT_LOW_DONE;
            end

            WAIT_LOW_DONE: begin
                if (uart_ready) begin
                    have_word <= 1'b0;
                    state     <= SEND_HIGH;
                end
            end

            default: begin
                state <= SEND_HIGH;
            end
        endcase
    end

endmodule
