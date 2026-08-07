#include <Arduino.h>     // Basic Arduino functions
#include <SPI.h>         // SPI communication library
#include "adf4351.h"     // ADF4351 synthesizer library

// Chip Select (LE) pin connected to ADF4351
#define PIN_SS 5

// Create an ADF4351 object
//
// Parameters:
// PIN_SS      = Chip Select pin
// SPI_MODE0   = SPI mode used by ADF4351
// 1000000UL   = SPI clock speed (1 MHz)
// MSBFIRST    = Send most significant bit first
ADF4351 vfo(PIN_SS, SPI_MODE0, 1000000UL, MSBFIRST);

void setup() {
  // ------------------------------------------------------------------
  // Enable the 100 MHz reference oscillator
  // Pin 25 controls power to the oscillator
  // HIGH = oscillator enabled
  // ------------------------------------------------------------------
  pinMode(25, OUTPUT);
  digitalWrite(25, HIGH);

  // Open serial port so we can see debug messages
  Serial.begin(115200);

  // Give serial port time to start
  delay(1000);

  Serial.println("1 start");

  // ------------------------------------------------------------------
  // Start SPI
  //
  // SPI.begin(SCK, MISO, MOSI, SS)
  //
  // SCK  = GPIO18
  // MISO = not used (-1)
  // MOSI = GPIO23
  // SS   = GPIO5
  // ------------------------------------------------------------------
  SPI.begin(18, -1, 23, PIN_SS);

  Serial.println("2 spi ok");

  // ------------------------------------------------------------------
  // ADF4351 configuration
  // ------------------------------------------------------------------

  // RF output power
  //
  // 0 = lowest
  // 3 = highest
  //
  // Usually about +5 dBm at setting 3
  vfo.pwrlevel = 3;

  // Reference doubler
  //
  // 0 = disabled
  // 1 = doubles reference frequency
  //
  // 100 MHz -> 200 MHz if enabled
  vfo.RD2refdouble = 0;

  // Reference divide by 2
  //
  // 0 = disabled
  // 1 = enabled
  //
  // 100 MHz -> 50 MHz if enabled
  vfo.RD1Rdiv2 = 0;

  // Clock divider used internally
  // Mainly affects lock detect timing
  vfo.ClkDiv = 150;

  // Band select clock divider
  // Controls VCO calibration speed
  vfo.BandSelClock = 80;

  // Reference counter (R divider)
  // Phase detector frequency = Reference Frequency / RCounter
  vfo.RCounter = 4;

  // Channel spacing step size
  // This determines frequency resolution.
  vfo.ChanStep = steps[2];

  Serial.println("3 config ok");

  // ------------------------------------------------------------------
  // Reference oscillator frequency 
  // ------------------------------------------------------------------
  //
  // DO NOT CHANGE REF FREQ!!!!
  //
  if (vfo.setrf(100000000UL) == 0)
    Serial.println("4 ref set to 100 MHz");
  else
    Serial.println("4 ref error");

  Serial.println("5 init");

  // Calculate and load ADF4351 registers
  vfo.init();

  Serial.println("6 enable");

  // Enable RF output
  vfo.enable();

  delay(10);

  Serial.println("7 set frequency");

  // ------------------------------------------------------------------
  // Set RF output frequency
  // 150000000 Hz = 150 MHz
  // ------------------------------------------------------------------
  int result = vfo.setf(49500000UL);
  Serial.println("15");

    Serial.println(result);
    Serial.println(vfo.cfreq);
    Serial.print("8 frequency set: ");
}

void loop()
{
  delay(1000);
}
