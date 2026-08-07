# System Architecture

## Signal path

The analyser is being developed as a modular RF/DSP instrument.

```text
Antenna / RF input
       |
       v
RF filtering / attenuation / LNA
       |
       v
Mixer or IQ demodulator <--- Tunable LO (ADF4351)
       |
       v
Analogue anti-alias filtering
       |
       v
AD9226 ADC
       |
       v
Tang Nano 9K FPGA
  - sample capture
  - windowing
  - FFT
  - magnitude processing
  - framing
       |
       v
UART / digital link
       |
       v
ESP32 touchscreen
  - spectrum
  - waterfall
  - controls
  - markers
```

## FPGA

The FPGA performs deterministic real-time DSP. The current design uses a 256-point Gowin FFT core with Hann-windowed ADC samples and sends processed bins to the host over UART.

## ADC

The current converter is an AD9226 12-bit ADC module. The design currently operates below the ADC's maximum sampling capability because the FPGA processing and interface are being developed incrementally.

A 10 MHz analogue low-pass filter is used ahead of the ADC for the current sampling configuration.

## RF conversion

The analyser uses a tunable local oscillator to translate an RF region of interest into the ADC input bandwidth. An ADL5801 mixer has been used during early development. An LTC5584 IQ demodulator is planned to provide I and Q baseband signals and remove positive/negative frequency ambiguity.

## User interface

An ESP32 drives an ILI9488 touchscreen. The interface already implements spectrum and waterfall views plus centre frequency, span, RBW, marker and peak-hold controls.

## Design philosophy

The project is deliberately modular. RF conversion, filtering, ADC/FPGA DSP and user-interface sections can be developed and tested independently before integration.
