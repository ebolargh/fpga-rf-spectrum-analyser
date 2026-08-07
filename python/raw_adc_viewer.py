import serial
import matplotlib.pyplot as plt

PORT = "COM9"
BAUD = 115200
N = 256

ser = serial.Serial(PORT, BAUD, timeout=1)

plt.ion()
fig, ax = plt.subplots()

while True:

    # Find sync.
    # FPGA header seems to appear as D5 55 or AA 55 depending on alignment.
    while True:
        b1 = ser.read(1)

        if len(b1) == 0:
            continue

        b2 = ser.read(1)

        if len(b2) == 0:
            continue

        if (b1[0] == 0xAA or b1[0] == 0xD5) and b2[0] == 0x55:
            break

    data = ser.read(N * 2)

    if len(data) != N * 2:
        continue

    samples = []

    for i in range(0, len(data), 2):
        hi = data[i]
        lo = data[i + 1]

        value = (hi << 8) | lo

        # 12 BIT
        value = value & 0x0FFF
        # 10 BIT
#        value = value & 0x03FF


        samples.append(value)

    if len(samples) == 0:
        continue

    print(samples[:32])
    print(
        "min:", min(samples),
        "max:", max(samples),
        "avg:", sum(samples) // len(samples)
    )

    ax.clear()
    ax.plot(samples)
    ax.set_ylim(1000, 3095)
    ax.set_title("Raw AD9226 ADC Samples")
    ax.set_xlabel("Sample")
    ax.set_ylabel("ADC Value")

    plt.pause(0.01)
