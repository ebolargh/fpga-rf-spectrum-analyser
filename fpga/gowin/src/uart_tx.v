// ========================= uart_tx.v =========================
// Packet_framer gives parallel bytes. This converts them to a serial UART stream
// 8N1 UART transmitter, LSB-first.
// 8 data bits, No parity, 1 stop bit.
// Byte interface: valid/data in, ready out.
// One byte is accepted when (valid && ready).

module uart_tx (
    input  wire clk,
    input  wire baud_tick,

    input  wire [7:0] data,
    input  wire       valid,
    output wire       ready,

    output wire tx
);

    reg [9:0] shifter = 10'b1111111111;
    reg [3:0] bit_idx = 4'd0;
    reg       busy    = 1'b0;

    assign ready = !busy;
    assign tx    = shifter[0];

    always @(posedge clk) begin
        if (!busy && valid) begin
            shifter <= {1'b1, data, 1'b0};
            bit_idx <= 4'd0;
            busy    <= 1'b1;
        end
        else if (busy && baud_tick) begin
            shifter <= {1'b1, shifter[9:1]};

            if (bit_idx == 4'd9) begin
                busy <= 1'b0;
            end else begin
                bit_idx <= bit_idx + 4'd1;
            end
        end
    end

endmodule
