// Select ESP32 Wrover Kit (all versions)

#include <TFT_eSPI.h>
#include <SPI.h>
#include <Preferences.h>
#include <math.h>
#include "adf4351.h"

TFT_eSPI tft = TFT_eSPI();
Preferences preferences;

// Tang Nano 9K FFT UART: FPGA pin 38 -> ESP32 GPIO 34.
constexpr int FPGA_UART_RX_PIN = 34;
constexpr uint32_t FPGA_UART_BAUD = 115200;
constexpr int FFT_BIN_COUNT = 256;
constexpr int DISPLAY_BIN_COUNT = FFT_BIN_COUNT / 2;

HardwareSerial fpgaSerial(2);

// Proven standalone ADF4351 wiring and settings.
// TFT and PLL deliberately share SCK GPIO 18 and MOSI GPIO 23.
constexpr int TFT_CS_PIN = 5;
constexpr int TOUCH_CS_PIN = 21;
constexpr int ADF4351_CE_PIN = 25;
constexpr int ADF4351_LE_PIN = 27;
constexpr uint32_t ADF4351_SPI_HZ = 1000000UL;
constexpr uint32_t ADF4351_REFERENCE_HZ = 100000000UL;
constexpr uint32_t ADF4351_TEST_FREQUENCY_HZ = 49500000UL;

ADF4351 vfo(
  ADF4351_LE_PIN,
  SPI_MODE0,
  ADF4351_SPI_HZ,
  MSBFIRST
);

// Set true once to force touch recalibration.
// Change it back to false after uploading.
constexpr bool FORCE_TOUCH_CALIBRATION = false;

enum ScreenPage {
  PAGE_SPECTRUM,
  PAGE_MENU,
  PAGE_CENTRE,
  PAGE_SPAN,
  PAGE_RBW,
  PAGE_SETTINGS
};

ScreenPage currentPage = PAGE_SPECTRUM;

uint16_t touchCalibration[5];

bool touchWasDown = false;
bool scanning = true;
bool markerEnabled = false;
bool peakHoldEnabled = false;
bool waterfallEnabled = false;
bool averagingEnabled = true;
bool gridEnabled = true;

uint32_t centreFrequency = 145000000UL;
uint32_t spanFrequency = 10000000UL;
uint32_t rbwFrequency = 10000UL;

int markerX = 310;

constexpr int SCREEN_W = 480;
constexpr int SCREEN_H = 320;

constexpr int PLOT_X = 10;
constexpr int PLOT_Y = 47;
constexpr int PLOT_W = 460;
constexpr int PLOT_H = 210;

constexpr int TRACE_H = 100;

constexpr int WATERFALL_X = PLOT_X + 1;
constexpr int WATERFALL_Y = PLOT_Y + TRACE_H + 2;
constexpr int WATERFALL_W = PLOT_W - 2;
constexpr int WATERFALL_H = PLOT_H - TRACE_H - 3;

unsigned long previousSpectrumUpdate = 0;

uint8_t spectrumLevels[WATERFALL_W];
uint16_t fftBins[FFT_BIN_COUNT];

enum FpgaRxState {
  FPGA_WAIT_AA,
  FPGA_WAIT_55,
  FPGA_READ_PAYLOAD
};

FpgaRxState fpgaRxState = FPGA_WAIT_AA;
uint16_t fpgaPayloadByte = 0;
uint8_t fpgaHighByte = 0;
bool spectrumFrameReady = false;
unsigned long lastFpgaFrameTime = 0;

uint32_t fpgaByteCount = 0;
uint32_t fpgaHeaderCount = 0;
uint32_t fpgaFrameCount = 0;
uint16_t latestBinMinimum = 0;
uint16_t latestBinMaximum = 0;
unsigned long previousFpgaDiagnostic = 0;

TFT_eSprite waterfallSprite = TFT_eSprite(&tft);
bool waterfallReady = false;

// ------------------------------------------------------------
// Function declarations
// ------------------------------------------------------------

void drawCurrentPage();
void updateSpectrumDisplay();

// ------------------------------------------------------------
// ADF4351 standalone-proven startup
// ------------------------------------------------------------

bool initialisePllAtTestFrequency() {
  pinMode(TFT_CS_PIN, OUTPUT);
  pinMode(TOUCH_CS_PIN, OUTPUT);
  pinMode(ADF4351_CE_PIN, OUTPUT);
  pinMode(ADF4351_LE_PIN, OUTPUT);

  // Ensure neither display device is selected during PLL writes.
  digitalWrite(TFT_CS_PIN, HIGH);
  digitalWrite(TOUCH_CS_PIN, HIGH);
  digitalWrite(ADF4351_CE_PIN, HIGH);
  digitalWrite(ADF4351_LE_PIN, LOW);

  // Keep the exact settings from the successful standalone test.
  vfo.pwrlevel = 3;
  vfo.RD2refdouble = 0;
  vfo.RD1Rdiv2 = 0;
  vfo.ClkDiv = 150;
  vfo.BandSelClock = 80;
  vfo.RCounter = 4;
  vfo.ChanStep = steps[2];

  int referenceResult = vfo.setrf(ADF4351_REFERENCE_HZ);

  if (referenceResult != 0) {
    Serial.print("ERROR: ADF4351 setrf() failed, code ");
    Serial.println(referenceResult);
    return false;
  }

  digitalWrite(TFT_CS_PIN, HIGH);
  digitalWrite(TOUCH_CS_PIN, HIGH);

  vfo.init();
  vfo.enable();
  delay(10);

  int frequencyResult = vfo.setf(ADF4351_TEST_FREQUENCY_HZ);

  // The PLL shares SCK/MOSI with the active TFT. LE must idle LOW
  // so display traffic cannot be latched as an ADF4351 register.
  digitalWrite(ADF4351_LE_PIN, LOW);

  if (frequencyResult != 0) {
    Serial.print("ERROR: ADF4351 setf() failed, code ");
    Serial.println(frequencyResult);
    return false;
  }

  Serial.print("ADF4351 frequency set to ");
  Serial.print(ADF4351_TEST_FREQUENCY_HZ);
  Serial.println(" Hz");
  Serial.println("PLL stage-1 setup complete");
  return true;
}

// ------------------------------------------------------------
// Utility functions
// ------------------------------------------------------------

bool hitButton(
  uint16_t touchX,
  uint16_t touchY,
  int x,
  int y,
  int width,
  int height
) {
  return touchX >= x &&
         touchX < x + width &&
         touchY >= y &&
         touchY < y + height;
}

String formatFrequency(uint32_t frequency) {
  char text[24];

  if (frequency >= 1000000UL) {
    snprintf(
      text,
      sizeof(text),
      "%.3f MHz",
      static_cast<double>(frequency) / 1000000.0
    );
  } else if (frequency >= 1000UL) {
    snprintf(
      text,
      sizeof(text),
      "%.1f kHz",
      static_cast<double>(frequency) / 1000.0
    );
  } else {
    snprintf(
      text,
      sizeof(text),
      "%lu Hz",
      static_cast<unsigned long>(frequency)
    );
  }

  return String(text);
}

void drawTitle(const String& title) {
  tft.fillRect(0, 0, SCREEN_W, 40, TFT_DARKGREY);
  tft.drawFastHLine(0, 39, SCREEN_W, TFT_CYAN);

  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.drawString(title, SCREEN_W / 2, 20);

  tft.setTextDatum(TL_DATUM);
}

void drawButton(
  int x,
  int y,
  int width,
  int height,
  const String& label,
  bool active = false
) {
  uint16_t fillColour = active ? TFT_GREEN : TFT_BLUE;
  uint16_t textColour = active ? TFT_BLACK : TFT_WHITE;

  tft.fillRoundRect(x, y, width, height, 7, fillColour);
  tft.drawRoundRect(x, y, width, height, 7, TFT_WHITE);

  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(textColour, fillColour);

  tft.drawString(
    label,
    x + width / 2,
    y + height / 2
  );

  tft.setTextDatum(TL_DATUM);
}

void changePage(ScreenPage page) {
  currentPage = page;
  drawCurrentPage();
}

// ------------------------------------------------------------
// Touch calibration
// ------------------------------------------------------------

void calibrateTouch() {
  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);

  tft.drawString(
    "Touch calibration",
    SCREEN_W / 2,
    80
  );

  tft.setTextFont(2);

  tft.drawString(
    "Touch each highlighted corner",
    SCREEN_W / 2,
    120
  );

  tft.calibrateTouch(
    touchCalibration,
    TFT_MAGENTA,
    TFT_BLACK,
    15
  );

  preferences.begin("touch", false);

  preferences.putBytes(
    "calibration",
    touchCalibration,
    sizeof(touchCalibration)
  );

  preferences.end();

  tft.setTouch(touchCalibration);
}

void initialiseTouch() {
  preferences.begin("touch", true);

  size_t storedSize =
    preferences.getBytesLength("calibration");

  bool calibrationExists =
    storedSize == sizeof(touchCalibration);

  if (calibrationExists && !FORCE_TOUCH_CALIBRATION) {
    preferences.getBytes(
      "calibration",
      touchCalibration,
      sizeof(touchCalibration)
    );

    preferences.end();

    tft.setTouch(touchCalibration);
  } else {
    preferences.end();
    calibrateTouch();
  }
}

// ------------------------------------------------------------
// Spectrum header and buttons
// ------------------------------------------------------------

void drawSpectrumHeader() {
  tft.fillRect(
    0,
    0,
    SCREEN_W,
    42,
    TFT_DARKGREY
  );

  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);

  tft.setCursor(8, 5);
  tft.print("Centre: ");
  tft.print(formatFrequency(centreFrequency));

  tft.setCursor(270, 5);
  tft.print("Span: ");
  tft.print(formatFrequency(spanFrequency));

  tft.setCursor(8, 23);
  tft.print("RBW: ");
  tft.print(formatFrequency(rbwFrequency));

  tft.setCursor(270, 23);
  tft.print(scanning ? "SCANNING" : "STOPPED");

  tft.drawFastHLine(
    0,
    41,
    SCREEN_W,
    TFT_CYAN
  );
}

void drawSpectrumButtons() {
  drawButton(5, 276, 108, 38, "MENU");

  drawButton(
    122,
    276,
    108,
    38,
    "MARKER",
    markerEnabled
  );

  drawButton(
    239,
    276,
    108,
    38,
    "PEAK",
    peakHoldEnabled
  );

  drawButton(
    356,
    276,
    118,
    38,
    "WATER",
    waterfallEnabled
  );
}

// ------------------------------------------------------------
// FPGA UART receiver and FFT-bin scaling
// ------------------------------------------------------------

void convertFftBinsToDisplay() {
  float logBins[DISPLAY_BIN_COUNT];
  float minimumLevel = 1000.0f;
  float maximumLevel = 0.0f;

  // Ignore the DC bin when calculating the automatic display range.
  logBins[0] = 0.0f;

  for (int bin = 1; bin < DISPLAY_BIN_COUNT; bin++) {
    float level = log10f(static_cast<float>(fftBins[bin]) + 1.0f);
    logBins[bin] = level;
    minimumLevel = min(minimumLevel, level);
    maximumLevel = max(maximumLevel, level);
  }

  float displayRange = maximumLevel - minimumLevel;

  if (displayRange < 0.05f) {
    displayRange = 0.05f;
  }

  for (int x = 0; x < WATERFALL_W; x++) {
    float binPosition =
      static_cast<float>(x) *
      static_cast<float>(DISPLAY_BIN_COUNT - 1) /
      static_cast<float>(WATERFALL_W - 1);

    int lowerBin = static_cast<int>(binPosition);
    int upperBin = min(lowerBin + 1, DISPLAY_BIN_COUNT - 1);
    float fraction = binPosition - static_cast<float>(lowerBin);

    float interpolatedLevel =
      logBins[lowerBin] +
      (logBins[upperBin] - logBins[lowerBin]) * fraction;

    int newLevel;

    if (lowerBin == 0) {
      newLevel = 0;
    } else {
      newLevel = constrain(
        static_cast<int>(
          255.0f *
          (interpolatedLevel - minimumLevel) /
          displayRange
        ),
        0,
        255
      );
    }

    if (averagingEnabled) {
      spectrumLevels[x] =
        static_cast<uint8_t>(
          (
            static_cast<uint16_t>(spectrumLevels[x]) * 3U +
            static_cast<uint16_t>(newLevel)
          ) / 4U
        );
    } else {
      spectrumLevels[x] = static_cast<uint8_t>(newLevel);
    }
  }

  if (peakHoldEnabled || markerEnabled) {
    int strongestX = 1;

    for (int x = 2; x < WATERFALL_W; x++) {
      if (spectrumLevels[x] > spectrumLevels[strongestX]) {
        strongestX = x;
      }
    }

    markerX = strongestX;
  }
}

void completeFpgaFrame() {
  latestBinMinimum = fftBins[1];
  latestBinMaximum = fftBins[1];

  for (int bin = 2; bin < DISPLAY_BIN_COUNT; bin++) {
    latestBinMinimum = min(latestBinMinimum, fftBins[bin]);
    latestBinMaximum = max(latestBinMaximum, fftBins[bin]);
  }

  fpgaFrameCount++;
  convertFftBinsToDisplay();
  spectrumFrameReady = true;
  lastFpgaFrameTime = millis();
}

void processFpgaByte(uint8_t value) {
  switch (fpgaRxState) {
    case FPGA_WAIT_AA:
      if (value == 0xAA) {
        fpgaRxState = FPGA_WAIT_55;
      }
      break;

    case FPGA_WAIT_55:
      if (value == 0x55) {
        fpgaHeaderCount++;
        fpgaPayloadByte = 0;
        fpgaRxState = FPGA_READ_PAYLOAD;
      } else if (value != 0xAA) {
        fpgaRxState = FPGA_WAIT_AA;
      }
      break;

    case FPGA_READ_PAYLOAD:
      if ((fpgaPayloadByte & 1U) == 0U) {
        fpgaHighByte = value;
      } else {
        uint16_t binIndex = fpgaPayloadByte >> 1;

        fftBins[binIndex] =
          (static_cast<uint16_t>(fpgaHighByte) << 8) |
          static_cast<uint16_t>(value);
      }

      fpgaPayloadByte++;

      if (fpgaPayloadByte >= FFT_BIN_COUNT * 2U) {
        fpgaRxState = FPGA_WAIT_AA;
        completeFpgaFrame();
      }
      break;
  }
}

void readFpgaSerial() {
  while (fpgaSerial.available() > 0) {
    uint8_t value =
      static_cast<uint8_t>(fpgaSerial.read());

    fpgaByteCount++;
    processFpgaByte(value);
  }
}

void printFpgaDiagnostic() {
  if (millis() - previousFpgaDiagnostic < 1000) {
    return;
  }

  previousFpgaDiagnostic = millis();

  Serial.print("FPGA bytes=");
  Serial.print(fpgaByteCount);
  Serial.print(" headers=");
  Serial.print(fpgaHeaderCount);
  Serial.print(" frames=");
  Serial.print(fpgaFrameCount);
  Serial.print(" bins(min/max)=");
  Serial.print(latestBinMinimum);
  Serial.print('/');
  Serial.print(latestBinMaximum);
  Serial.print(" rxLevel=");
  Serial.print(digitalRead(FPGA_UART_RX_PIN));
  Serial.print(" pllLE=");
  Serial.print(digitalRead(ADF4351_LE_PIN));
  Serial.print(" lastFrameAgeMs=");

  if (fpgaFrameCount == 0) {
    Serial.println("NONE");
  } else {
    Serial.println(millis() - lastFpgaFrameTime);
  }
}

// ------------------------------------------------------------
// Waterfall colour conversion
// ------------------------------------------------------------

uint16_t waterfallColour(uint8_t level) {
  if (level < 50) {
    return tft.color565(
      0,
      0,
      level * 4
    );
  }

  if (level < 110) {
    return tft.color565(
      0,
      (level - 50) * 4,
      255
    );
  }

  if (level < 180) {
    return tft.color565(
      (level - 110) * 3,
      255,
      255
    );
  }

  return tft.color565(
    255,
    255,
    (level - 180) * 3
  );
}

// ------------------------------------------------------------
// Spectrum trace
// ------------------------------------------------------------

void drawTrace(int traceHeight) {
  tft.fillRect(
    PLOT_X + 1,
    PLOT_Y + 1,
    PLOT_W - 2,
    traceHeight - 2,
    TFT_BLACK
  );

  if (gridEnabled) {
    for (int division = 1; division < 10; division++) {
      int x =
        PLOT_X +
        division * (PLOT_W / 10);

      tft.drawFastVLine(
        x,
        PLOT_Y + 1,
        traceHeight - 2,
        TFT_DARKGREY
      );
    }

    for (int division = 1; division < 4; division++) {
      int y =
        PLOT_Y +
        division * (traceHeight / 4);

      tft.drawFastHLine(
        PLOT_X + 1,
        y,
        PLOT_W - 2,
        TFT_DARKGREY
      );
    }
  }

  int previousX = PLOT_X + 1;
  int previousY = PLOT_Y + traceHeight - 2;

  for (int x = 0; x < WATERFALL_W; x++) {
    int amplitude = map(
      spectrumLevels[x],
      0,
      255,
      2,
      traceHeight - 4
    );

    int screenX = PLOT_X + 1 + x;

    int screenY =
      PLOT_Y +
      traceHeight -
      2 -
      amplitude;

    if (x > 0) {
      tft.drawLine(
        previousX,
        previousY,
        screenX,
        screenY,
        TFT_YELLOW
      );
    }

    previousX = screenX;
    previousY = screenY;
  }

  if (markerEnabled) {
    tft.drawFastVLine(
      PLOT_X + markerX,
      PLOT_Y + 1,
      traceHeight - 2,
      TFT_RED
    );

    tft.fillTriangle(
      PLOT_X + markerX - 5,
      PLOT_Y + 2,
      PLOT_X + markerX + 5,
      PLOT_Y + 2,
      PLOT_X + markerX,
      PLOT_Y + 10,
      TFT_RED
    );
  }

  tft.drawRect(
    PLOT_X,
    PLOT_Y,
    PLOT_W,
    traceHeight,
    TFT_LIGHTGREY
  );
}

// ------------------------------------------------------------
// Waterfall drawing
// ------------------------------------------------------------

void addWaterfallLine() {
  if (!waterfallReady) {
    return;
  }

  // Move the existing waterfall upward by one pixel.
  waterfallSprite.scroll(0, -1);

  int bottomRow = WATERFALL_H - 1;

  for (int x = 0; x < WATERFALL_W; x++) {
    waterfallSprite.drawPixel(
      x,
      bottomRow,
      waterfallColour(spectrumLevels[x])
    );
  }

  waterfallSprite.pushSprite(
    WATERFALL_X,
    WATERFALL_Y
  );
}

void updateSpectrumDisplay() {
  if (waterfallEnabled && waterfallReady) {
    drawTrace(TRACE_H);

    tft.drawRect(
      PLOT_X,
      WATERFALL_Y - 1,
      PLOT_W,
      WATERFALL_H + 2,
      TFT_LIGHTGREY
    );

    addWaterfallLine();
  } else {
    drawTrace(PLOT_H);
  }
}

void drawSpectrumPage() {
  tft.fillScreen(TFT_BLACK);

  drawSpectrumHeader();
  drawSpectrumButtons();

  if (waterfallReady) {
    waterfallSprite.fillSprite(TFT_BLACK);
  }

  updateSpectrumDisplay();
}

// ------------------------------------------------------------
// Main menu
// ------------------------------------------------------------

void drawMenuPage() {
  tft.fillScreen(TFT_BLACK);
  drawTitle("MAIN MENU");

  drawButton(
    15,
    55,
    215,
    48,
    scanning ? "STOP SCAN" : "START SCAN",
    scanning
  );

  drawButton(
    250,
    55,
    215,
    48,
    "CENTRE FREQUENCY"
  );

  drawButton(15, 115, 215, 48, "SPAN");
  drawButton(250, 115, 215, 48, "RBW");

  drawButton(
    15,
    175,
    215,
    48,
    "WATERFALL",
    waterfallEnabled
  );

  drawButton(
    250,
    175,
    215,
    48,
    "SETTINGS"
  );

  drawButton(135, 250, 210, 48, "BACK");
}

// ------------------------------------------------------------
// Centre-frequency page
// ------------------------------------------------------------

void drawCentrePage() {
  tft.fillScreen(TFT_BLACK);
  drawTitle("CENTRE FREQUENCY");

  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);

  tft.drawString(
    formatFrequency(centreFrequency),
    SCREEN_W / 2,
    70
  );

  tft.setTextDatum(TL_DATUM);

  drawButton(15, 105, 140, 48, "-10 MHz");
  drawButton(170, 105, 140, 48, "-1 MHz");
  drawButton(325, 105, 140, 48, "-100 kHz");

  drawButton(15, 170, 140, 48, "+100 kHz");
  drawButton(170, 170, 140, 48, "+1 MHz");
  drawButton(325, 170, 140, 48, "+10 MHz");

  drawButton(135, 255, 210, 48, "DONE");
}

// ------------------------------------------------------------
// Span page
// ------------------------------------------------------------

void drawSpanPage() {
  tft.fillScreen(TFT_BLACK);
  drawTitle("SPAN");

  drawButton(
    15,
    60,
    140,
    48,
    "100 kHz",
    spanFrequency == 100000UL
  );

  drawButton(
    170,
    60,
    140,
    48,
    "500 kHz",
    spanFrequency == 500000UL
  );

  drawButton(
    325,
    60,
    140,
    48,
    "1 MHz",
    spanFrequency == 1000000UL
  );

  drawButton(
    15,
    125,
    140,
    48,
    "2 MHz",
    spanFrequency == 2000000UL
  );

  drawButton(
    170,
    125,
    140,
    48,
    "5 MHz",
    spanFrequency == 5000000UL
  );

  drawButton(
    325,
    125,
    140,
    48,
    "10 MHz",
    spanFrequency == 10000000UL
  );

  drawButton(135, 245, 210, 48, "BACK");
}

// ------------------------------------------------------------
// RBW page
// ------------------------------------------------------------

void drawRbwPage() {
  tft.fillScreen(TFT_BLACK);
  drawTitle("RESOLUTION BANDWIDTH");

  drawButton(
    15,
    60,
    140,
    48,
    "1 kHz",
    rbwFrequency == 1000UL
  );

  drawButton(
    170,
    60,
    140,
    48,
    "3 kHz",
    rbwFrequency == 3000UL
  );

  drawButton(
    325,
    60,
    140,
    48,
    "10 kHz",
    rbwFrequency == 10000UL
  );

  drawButton(
    15,
    125,
    140,
    48,
    "30 kHz",
    rbwFrequency == 30000UL
  );

  drawButton(
    170,
    125,
    140,
    48,
    "100 kHz",
    rbwFrequency == 100000UL
  );

  drawButton(
    325,
    125,
    140,
    48,
    "300 kHz",
    rbwFrequency == 300000UL
  );

  drawButton(135, 245, 210, 48, "BACK");
}

// ------------------------------------------------------------
// Settings page
// ------------------------------------------------------------

void drawSettingsPage() {
  tft.fillScreen(TFT_BLACK);
  drawTitle("SETTINGS");

  drawButton(
    15,
    60,
    215,
    48,
    "PEAK HOLD",
    peakHoldEnabled
  );

  drawButton(
    250,
    60,
    215,
    48,
    "AVERAGING",
    averagingEnabled
  );

  drawButton(
    15,
    125,
    215,
    48,
    "GRID",
    gridEnabled
  );

  drawButton(
    250,
    125,
    215,
    48,
    "CALIBRATE TOUCH"
  );

  drawButton(
    15,
    190,
    215,
    48,
    "MARKER",
    markerEnabled
  );

  drawButton(
    250,
    190,
    215,
    48,
    "WATERFALL",
    waterfallEnabled
  );

  drawButton(135, 260, 210, 45, "BACK");
}

// ------------------------------------------------------------
// Touch handling
// ------------------------------------------------------------

void handleSpectrumTouch(uint16_t x, uint16_t y) {
  if (hitButton(x, y, 5, 276, 108, 38)) {
    changePage(PAGE_MENU);
    return;
  }

  if (hitButton(x, y, 122, 276, 108, 38)) {
    markerEnabled = !markerEnabled;
    drawSpectrumButtons();
    updateSpectrumDisplay();
    return;
  }

  if (hitButton(x, y, 239, 276, 108, 38)) {
    peakHoldEnabled = !peakHoldEnabled;

    if (peakHoldEnabled) {
      markerEnabled = true;
      markerX = 300;
    }

    drawSpectrumButtons();
    updateSpectrumDisplay();
    return;
  }

  if (hitButton(x, y, 356, 276, 118, 38)) {
    waterfallEnabled = !waterfallEnabled;

    if (waterfallReady) {
      waterfallSprite.fillSprite(TFT_BLACK);
    }

    drawSpectrumPage();
    return;
  }
}

void handleMenuTouch(uint16_t x, uint16_t y) {
  if (hitButton(x, y, 15, 55, 215, 48)) {
    scanning = !scanning;
    drawMenuPage();
    return;
  }

  if (hitButton(x, y, 250, 55, 215, 48)) {
    changePage(PAGE_CENTRE);
    return;
  }

  if (hitButton(x, y, 15, 115, 215, 48)) {
    changePage(PAGE_SPAN);
    return;
  }

  if (hitButton(x, y, 250, 115, 215, 48)) {
    changePage(PAGE_RBW);
    return;
  }

  if (hitButton(x, y, 15, 175, 215, 48)) {
    waterfallEnabled = !waterfallEnabled;

    if (waterfallReady) {
      waterfallSprite.fillSprite(TFT_BLACK);
    }

    drawMenuPage();
    return;
  }

  if (hitButton(x, y, 250, 175, 215, 48)) {
    changePage(PAGE_SETTINGS);
    return;
  }

  if (hitButton(x, y, 135, 250, 210, 48)) {
    changePage(PAGE_SPECTRUM);
  }
}

void handleCentreTouch(uint16_t x, uint16_t y) {
  if (hitButton(x, y, 15, 105, 140, 48)) {
    centreFrequency =
      centreFrequency > 10000000UL
        ? centreFrequency - 10000000UL
        : 0;
  } else if (hitButton(x, y, 170, 105, 140, 48)) {
    centreFrequency =
      centreFrequency > 1000000UL
        ? centreFrequency - 1000000UL
        : 0;
  } else if (hitButton(x, y, 325, 105, 140, 48)) {
    centreFrequency =
      centreFrequency > 100000UL
        ? centreFrequency - 100000UL
        : 0;
  } else if (hitButton(x, y, 15, 170, 140, 48)) {
    centreFrequency += 100000UL;
  } else if (hitButton(x, y, 170, 170, 140, 48)) {
    centreFrequency += 1000000UL;
  } else if (hitButton(x, y, 325, 170, 140, 48)) {
    centreFrequency += 10000000UL;
  } else if (hitButton(x, y, 135, 255, 210, 48)) {
    changePage(PAGE_MENU);
    return;
  }

  drawCentrePage();
}

void handleSpanTouch(uint16_t x, uint16_t y) {
  if (hitButton(x, y, 15, 60, 140, 48)) {
    spanFrequency = 100000UL;
  } else if (hitButton(x, y, 170, 60, 140, 48)) {
    spanFrequency = 500000UL;
  } else if (hitButton(x, y, 325, 60, 140, 48)) {
    spanFrequency = 1000000UL;
  } else if (hitButton(x, y, 15, 125, 140, 48)) {
    spanFrequency = 2000000UL;
  } else if (hitButton(x, y, 170, 125, 140, 48)) {
    spanFrequency = 5000000UL;
  } else if (hitButton(x, y, 325, 125, 140, 48)) {
    spanFrequency = 10000000UL;
  } else if (hitButton(x, y, 135, 245, 210, 48)) {
    changePage(PAGE_MENU);
    return;
  }

  drawSpanPage();
}

void handleRbwTouch(uint16_t x, uint16_t y) {
  if (hitButton(x, y, 15, 60, 140, 48)) {
    rbwFrequency = 1000UL;
  } else if (hitButton(x, y, 170, 60, 140, 48)) {
    rbwFrequency = 3000UL;
  } else if (hitButton(x, y, 325, 60, 140, 48)) {
    rbwFrequency = 10000UL;
  } else if (hitButton(x, y, 15, 125, 140, 48)) {
    rbwFrequency = 30000UL;
  } else if (hitButton(x, y, 170, 125, 140, 48)) {
    rbwFrequency = 100000UL;
  } else if (hitButton(x, y, 325, 125, 140, 48)) {
    rbwFrequency = 300000UL;
  } else if (hitButton(x, y, 135, 245, 210, 48)) {
    changePage(PAGE_MENU);
    return;
  }

  drawRbwPage();
}

void handleSettingsTouch(uint16_t x, uint16_t y) {
  if (hitButton(x, y, 15, 60, 215, 48)) {
    peakHoldEnabled = !peakHoldEnabled;
  } else if (hitButton(x, y, 250, 60, 215, 48)) {
    averagingEnabled = !averagingEnabled;
  } else if (hitButton(x, y, 15, 125, 215, 48)) {
    gridEnabled = !gridEnabled;
  } else if (hitButton(x, y, 250, 125, 215, 48)) {
    calibrateTouch();
  } else if (hitButton(x, y, 15, 190, 215, 48)) {
    markerEnabled = !markerEnabled;
  } else if (hitButton(x, y, 250, 190, 215, 48)) {
    waterfallEnabled = !waterfallEnabled;

    if (waterfallReady) {
      waterfallSprite.fillSprite(TFT_BLACK);
    }
  } else if (hitButton(x, y, 135, 260, 210, 45)) {
    changePage(PAGE_MENU);
    return;
  }

  drawSettingsPage();
}

void handleTouch(uint16_t x, uint16_t y) {
  switch (currentPage) {
    case PAGE_SPECTRUM:
      handleSpectrumTouch(x, y);
      break;

    case PAGE_MENU:
      handleMenuTouch(x, y);
      break;

    case PAGE_CENTRE:
      handleCentreTouch(x, y);
      break;

    case PAGE_SPAN:
      handleSpanTouch(x, y);
      break;

    case PAGE_RBW:
      handleRbwTouch(x, y);
      break;

    case PAGE_SETTINGS:
      handleSettingsTouch(x, y);
      break;
  }
}

// ------------------------------------------------------------
// Page drawing
// ------------------------------------------------------------

void drawCurrentPage() {
  switch (currentPage) {
    case PAGE_SPECTRUM:
      drawSpectrumPage();
      break;

    case PAGE_MENU:
      drawMenuPage();
      break;

    case PAGE_CENTRE:
      drawCentrePage();
      break;

    case PAGE_SPAN:
      drawSpanPage();
      break;

    case PAGE_RBW:
      drawRbwPage();
      break;

    case PAGE_SETTINGS:
      drawSettingsPage();
      break;
  }
}

// ------------------------------------------------------------
// Arduino setup and loop
// ------------------------------------------------------------

void setup() {
  Serial.begin(115200);

  // TFT is initialized first; UART follows; PLL is programmed last.

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  initialiseTouch();

  // Required initialization order:
  // 1. TFT/touch configures the shared SPI bus.
  // 2. UART2 is assigned to FPGA RX GPIO 34.
  // 3. ADF4351 is programmed last.
  pinMode(FPGA_UART_RX_PIN, INPUT);
  fpgaSerial.setRxBufferSize(2048);
  fpgaSerial.begin(
    FPGA_UART_BAUD,
    SERIAL_8N1,
    FPGA_UART_RX_PIN,
    -1
  );

  Serial.print("FPGA UART2 initialized on GPIO ");
  Serial.println(FPGA_UART_RX_PIN);

  if (!initialisePllAtTestFrequency()) {
    Serial.println("WARNING: ADF4351 did not initialise correctly");
  }

  waterfallSprite.setColorDepth(16);

  waterfallReady =
    waterfallSprite.createSprite(
      WATERFALL_W,
      WATERFALL_H
    ) != nullptr;

  if (waterfallReady) {
    waterfallSprite.setScrollRect(
      0,
      0,
      WATERFALL_W,
      WATERFALL_H,
      TFT_BLACK
    );

    waterfallSprite.fillSprite(TFT_BLACK);
  } else {
    Serial.println("Could not allocate waterfall sprite");
    waterfallEnabled = false;
  }

  drawCurrentPage();
}

void loop() {
  readFpgaSerial();
  printFpgaDiagnostic();

  uint16_t touchX = 0;
  uint16_t touchY = 0;

  bool touchDown =
    tft.getTouch(&touchX, &touchY);

  // Trigger once when the finger first touches the display.
  if (touchDown && !touchWasDown) {
    handleTouch(touchX, touchY);
  }

  touchWasDown = touchDown;

  if (
    currentPage == PAGE_SPECTRUM &&
    scanning &&
    spectrumFrameReady &&
    millis() - previousSpectrumUpdate >= 40
  ) {
    previousSpectrumUpdate = millis();
    spectrumFrameReady = false;
    updateSpectrumDisplay();
  }

  delay(5);
}
