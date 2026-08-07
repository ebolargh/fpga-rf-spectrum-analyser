#include <Arduino.h>
#include <SPI.h>
#include "adf4351.h"

// Standalone ADF4351 test using a SPI bus separate from the TFT.
//
// Wiring:
// ADF4351 CE   -> ESP32 GPIO 25
// ADF4351 LE   -> ESP32 GPIO 27
// ADF4351 DATA -> ESP32 GPIO 33
// ADF4351 CLK  -> ESP32 GPIO 32
// ADF4351 GND  -> ESP32 GND
//
// MUX and LD are unused.
// Leave all TFT, touch and FPGA UART wiring unchanged.

constexpr int ADF4351_CE_PIN = 25;
constexpr int ADF4351_LE_PIN = 27;
constexpr int ADF4351_DATA_PIN = 33;
constexpr int ADF4351_CLK_PIN = 32;

constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t SPI_FREQUENCY_HZ = 1000000UL;
constexpr uint32_t REFERENCE_FREQUENCY_HZ = 100000000UL;
constexpr uint32_t OUTPUT_FREQUENCY_HZ = 49500000UL;

ADF4351 vfo(
  ADF4351_LE_PIN,
  SPI_MODE0,
  SPI_FREQUENCY_HZ,
  MSBFIRST
);

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);

  Serial.println();
  Serial.println("ADF4351 standalone separate-bus test");

  pinMode(ADF4351_CE_PIN, OUTPUT);
  pinMode(ADF4351_LE_PIN, OUTPUT);

  digitalWrite(ADF4351_CE_PIN, HIGH);
  digitalWrite(ADF4351_LE_PIN, LOW);

  SPI.begin(
    ADF4351_CLK_PIN,
    -1,
    ADF4351_DATA_PIN,
    ADF4351_LE_PIN
  );

  Serial.println("SPI started: CLK 32, DATA 33, LE 27");
  Serial.println("ADF4351 CE set HIGH on GPIO 25");

  // These are the exact settings from the proven working PLL test.
  vfo.pwrlevel = 3;
  vfo.RD2refdouble = 0;
  vfo.RD1Rdiv2 = 0;
  vfo.ClkDiv = 150;
  vfo.BandSelClock = 80;
  vfo.RCounter = 4;
  vfo.ChanStep = steps[2];

  int referenceResult = vfo.setrf(REFERENCE_FREQUENCY_HZ);

  if (referenceResult != 0) {
    Serial.print("ERROR: setrf() failed, code ");
    Serial.println(referenceResult);
    return;
  }

  Serial.println("Reference set to 100 MHz");

  vfo.init();
  vfo.enable();
  delay(10);

  int frequencyResult = vfo.setf(OUTPUT_FREQUENCY_HZ);

  if (frequencyResult != 0) {
    Serial.print("ERROR: setf() failed, code ");
    Serial.println(frequencyResult);
    return;
  }

  Serial.print("ADF4351 frequency set to ");
  Serial.print(OUTPUT_FREQUENCY_HZ);
  Serial.println(" Hz");

  Serial.print("Library calculated frequency: ");
  Serial.println(vfo.cfreq);

  Serial.println("PLL setup complete");
}

void loop() {
  delay(1000);
}
