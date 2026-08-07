// ========================= baud_gen.v =========================
// Generates a 1-cycle tick at the desired baud rate.
// Default: 27 MHz / 115200 ≈ 234
module baud_gen #(
    parameter integer BAUD_DIV = 234
) (
    input  wire clk,
    output reg  baud_tick
);
    reg [15:0] cnt = 16'd0;

    always @(posedge clk) begin
        if (cnt == (BAUD_DIV-1)) begin
            cnt       <= 16'd0;
            baud_tick <= 1'b1;
        end else begin
            cnt       <= cnt + 16'd1;
            baud_tick <= 1'b0;
        end
    end
endmodule
