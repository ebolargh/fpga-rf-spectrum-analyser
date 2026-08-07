import serial
import matplotlib.pyplot as plt

PORT = "COM9"
BAUD = 115200
BINS = 256
SAMPLE_RATE = 2_250_000

ser = serial.Serial(PORT, BAUD, timeout=1)

plt.ion()
fig, ax = plt.subplots(figsize=(14, 5))


def read_fft_frame():
    while True:
        b1 = ser.read(1)
        if b1 == b"\xAA":
            b2 = ser.read(1)
            if b2 == b"\x55":
                data = ser.read(BINS * 2)
                if len(data) == BINS * 2:
                    return [
                        (data[i] << 8) | data[i + 1]
                        for i in range(0, len(data), 2)
                    ]


while True:
    bins = read_fft_frame()

    # use only positive frequencies
    mags = bins[:128]

    # optional: remove DC spike
    mags[0] = 0

    freqs = [
        i * SAMPLE_RATE / BINS / 1000
        for i in range(128)
    ]

    ax.clear()
    ax.plot(freqs, mags)
    ax.set_title("FPGA FFT Spectrum")
    ax.set_xlabel("Frequency (kHz)")
    ax.set_ylabel("Magnitude")
    ax.set_xlim(0, SAMPLE_RATE / 2 / 1000)
    ax.set_xticks(range(0, 1001, 50))
    ymax = max(mags) + 100
    ax.set_ylim(0, max(ymax, 100))

    plt.pause(0.01)
