#include <TFT_eSPI.h>
#include <SPI.h>
#include <Preferences.h>
#include <math.h>

TFT_eSPI tft = TFT_eSPI();
Preferences preferences;

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

TFT_eSprite waterfallSprite = TFT_eSprite(&tft);
bool waterfallReady = false;

// ------------------------------------------------------------
// Function declarations
// ------------------------------------------------------------

void drawCurrentPage();
void updateSpectrumDisplay();

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
// Fake spectrum generation
// ------------------------------------------------------------

void generateFakeSpectrum() {
  float movement = millis() / 500.0f;

  for (int x = 0; x < WATERFALL_W; x++) {
    float noise = random(4, 18);

    float peak1Position =
      120.0f + sinf(movement) * 5.0f;

    float peak2Position = 300.0f;
    float peak3Position = 390.0f;

    float peak1 =
      100.0f *
      expf(
        -0.5f *
        powf(
          (x - peak1Position) / 9.0f,
          2
        )
      );

    float peak2 =
      155.0f *
      expf(
        -0.5f *
        powf(
          (x - peak2Position) / 5.0f,
          2
        )
      );

    float peak3 =
      75.0f *
      expf(
        -0.5f *
        powf(
          (x - peak3Position) / 14.0f,
          2
        )
      );

    int level =
      static_cast<int>(
        noise + peak1 + peak2 + peak3
      );

    spectrumLevels[x] =
      constrain(level, 0, 255);
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
  generateFakeSpectrum();

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

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  initialiseTouch();

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

  randomSeed(micros());

  drawCurrentPage();
}

void loop() {
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
    millis() - previousSpectrumUpdate >= 150
  ) {
    previousSpectrumUpdate = millis();
    updateSpectrumDisplay();
  }

  delay(5);
}
