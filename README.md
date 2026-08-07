# FPGA RF Spectrum Analyser

A portable RF spectrum analyser project built around an FPGA DSP pipeline, high-speed ADC sampling, a tunable RF front end, and an ESP32 touchscreen interface.

## Project goals

- Sample an analogue IF/baseband signal with an AD9226 ADC.
- Process samples on a Tang Nano 9K FPGA.
- Apply windowing and a 256-point FFT in hardware.
- Stream spectrum data over UART.
- Display spectrum and waterfall data on an ESP32 touchscreen.
- Add a tunable RF front end using a PLL/local oscillator and mixer or IQ demodulator.
- Improve resolution with filtering, decimation, and larger FFTs where practical.
- Develop the platform into a portable passive RF signal-analysis instrument.

## Current hardware

- Tang Nano 9K FPGA board (GW1NR-9)
- AD9226 12-bit high-speed ADC module
- ESP32 WROOM32 with 3.5-inch ILI9488/XPT2046 touchscreen
- ADF4351 PLL modules for tunable RF test/LO generation
- ADL5801 mixer module
- LTC5584 IQ demodulator module planned for I/Q reception
- 10 MHz analogue low-pass filter
- RF filters, LNA modules, SMA attenuators and test sources

## Current FPGA pipeline

```text
ADC -> sample capture -> Hann window -> 256-point FFT -> magnitude -> UART framing
```

Current implementation details:

- 12-bit ADC input
- 16-bit signed FFT samples
- Gowin 256-point FFT IP
- Real input with imaginary input set to zero
- Spectrum bins transmitted over UART
- UART frame sync bytes: `AA 55`
- Host-side Python receiver used for development and verification

## Display

The current ESP32 touchscreen interface includes:

- Spectrum trace
- Waterfall display
- Centre-frequency control
- Span control
- RBW menu
- Marker
- Peak hold
- Scan start/stop
- Settings page

## Repository layout

```text
fpga/       FPGA RTL, constraints, simulation and Gowin project files
esp32/      Touchscreen/display firmware
python/     PC-side test and serial tools
hardware/   Schematics, PCB files and hardware notes
docs/       Architecture and DSP documentation
images/     Project photographs and diagrams
```

## Development status

The ADC-to-FPGA FFT chain has been demonstrated with test signals, and the ESP32 spectrum/waterfall UI is working with generated test data. The next major integration step is to feed real FPGA FFT-bin data into the ESP32 display and then complete the RF/IQ front end.

## Planned work

- Import and clean up the working FPGA source tree.
- Add ADC/FPGA pin constraints and clock documentation.
- Connect FPGA FFT-bin output to the ESP32 spectrum display.
- Define a robust FPGA-to-ESP32 data protocol.
- Integrate I/Q sampling using two ADC channels.
- Add digital filtering and decimation modes.
- Characterise the analogue 10 MHz anti-alias filter.
- Integrate the tunable RF front end.
- Add calibration and dB-level scaling.
- Document test results with plots and photographs.

## Safety and intended use

This project is intended for lawful RF measurement, experimentation, education and passive signal analysis. Users are responsible for complying with applicable radio, privacy and communications laws.
