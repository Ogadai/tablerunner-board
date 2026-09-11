#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <Adafruit_NeoPixel.h>

#define LED_BUILTIN 8
#define SERIAL_BAUDRATE 115200

// Unique UUIDs matching your Next.js frontend application
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_NAME            "TABLERUNNER-ASC"

#define NUM_PIXELS          240
#define PIXELS_PIN          1
#define BLINKING_LED        0
#define BRIGHTNESS          200

// --- ANIMATION SYSTEM CONFIGURATION ---
#define MAX_TRACKS          10  // Maximum number of comma-separated animation tracks
#define MAX_LEDS_PER_TRACK  24  // Maximum number of LEDs controlled by a single track
#define MAX_COLORS_PER_TRACK 8  // Maximum number of cycling colors per track
#define ANIMATION_INTERVAL 500  // Color transition interval in milliseconds

struct AnimationTrack {
  uint16_t leds[MAX_LEDS_PER_TRACK];
  uint16_t ledCount = 0;
  uint32_t colors[MAX_COLORS_PER_TRACK]; // Pre-converted NeoPixel Color values
  uint16_t colorCount = 0;
  uint16_t currentColorIndex = 0;
};

AnimationTrack animTracks[MAX_TRACKS];
uint16_t activeTrackCount = 0;
bool animationActive = false;
unsigned long lastAnimationUpdate = 0;
// --------------------------------------

unsigned long deviceConnectingStart = 0;
bool deviceConnected = false;
bool enableSerial = true;

/* the LED pixel array controller */
Adafruit_NeoPixel ledArray(NUM_PIXELS, PIXELS_PIN, NEO_GRB + NEO_KHZ800);

void serialPrintLn(const char *format, ...) {
  if (enableSerial) {
    char buffer[128];
  
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    Serial.println(buffer);
  }
}

// Helper to convert a hex string directly into a NeoPixel packed 32-bit color WITH GAMMA CORRECTION
uint32_t parseHexToColor(const String& hexColorStr) {
  long rgb = strtol(hexColorStr.c_str(), NULL, 16);
  
  uint8_t rawRed   = rgb >> 16;
  uint8_t rawGreen = (rgb & 0x00ff00) >> 8;
  uint8_t rawBlue  = (rgb & 0x0000ff);

  // FIXED: Apply Adafruit's optimized mathematical gamma translation matrix
  uint8_t gammaRed   = ledArray.gamma8(rawRed);
  uint8_t gammaGreen = ledArray.gamma8(rawGreen);
  uint8_t gammaBlue  = ledArray.gamma8(rawBlue);

  return ledArray.Color(gammaRed, gammaGreen, gammaBlue);
}

// FIXED: Now safely re-uses parseHexToColor to inherit global gamma calibration curves
void setHEXColor(uint16_t index, const String& hexColorStr) {
  if (index < NUM_PIXELS) {
    uint32_t packedColor = parseHexToColor(hexColorStr);
    ledArray.setPixelColor(index, packedColor);
  } else {
    serialPrintLn("Warning: Index %d out of bounds", index);
  }
}

void setHEXColorforLEDs(const String& indexStr, const String& hexColorStr) {
  uint16_t index = 0;
  uint16_t length = indexStr.length();
  uint16_t ledCount = 0;

  while (index < length) {
    int16_t endIndex = indexStr.indexOf('/', index);
    if (endIndex == -1) {
      endIndex = length;
    }
    String ledStr = indexStr.substring(index, endIndex);
    uint16_t led = atol(ledStr.c_str());

    setHEXColor(led, hexColorStr);
    ledCount++;

    index = endIndex + 1;
  }
  serialPrintLn("Set %d LEDs to colour %s", ledCount, hexColorStr.c_str());
}

// Helper to parse and store incoming animation payload sequences safely
void parseAnimationCommand(const String& payload) {
  animationActive = false; // Halt engine during processing modifications
  activeTrackCount = 0;
  
  uint16_t trackStart = 5; // Skip past the initial prefix boundary "ANIM|"
  uint16_t payloadLength = payload.length();

  while (trackStart < payloadLength && activeTrackCount < MAX_TRACKS) {
    int16_t trackEnd = payload.indexOf(',', trackStart);
    if (trackEnd == -1) {
      trackEnd = payloadLength;
    }

    String trackStr = payload.substring(trackStart, trackEnd);
    int16_t separatorIndex = trackStr.indexOf(':');

    if (separatorIndex != -1) {
      String ledsSection = trackStr.substring(0, separatorIndex);
      String colorsSection = trackStr.substring(separatorIndex + 1);

      AnimationTrack& currentTrack = animTracks[activeTrackCount];
      currentTrack.ledCount = 0;
      currentTrack.colorCount = 0;
      currentTrack.currentColorIndex = 0;

      // 1. Extract LEDs for current track WITH BUFFER OVERFLOW GUARD
      uint16_t ledIndex = 0;
      uint16_t ledsLength = ledsSection.length();
      while (ledIndex < ledsLength) {
        int16_t slashIndex = ledsSection.indexOf('/', ledIndex);
        if (slashIndex == -1) {
          slashIndex = ledsLength;
        }
        
        // FIXED: Stop saving to array if we exceed capacity, avoiding memory corruption
        if (currentTrack.ledCount < MAX_LEDS_PER_TRACK) {
          currentTrack.leds[currentTrack.ledCount++] = atol(ledsSection.substring(ledIndex, slashIndex).c_str());
        } else {
          serialPrintLn("Warning: Track %d reached MAX_LEDS_PER_TRACK limit!", activeTrackCount);
        }
        
        ledIndex = slashIndex + 1;
      }

      // 2. Extract Colours for current track WITH BUFFER OVERFLOW GUARD
      uint16_t colorIndex = 0;
      uint16_t colorsLength = colorsSection.length();
      while (colorIndex < colorsLength) {
        int16_t slashIndex = colorsSection.indexOf('/', colorIndex);
        if (slashIndex == -1) {
          slashIndex = colorsLength;
        }
        String colorHex = colorsSection.substring(colorIndex, slashIndex);
        if (colorHex.length() > 0) {
          // FIXED: Stop saving to array if we exceed capacity
          if (currentTrack.colorCount < MAX_COLORS_PER_TRACK) {
            currentTrack.colors[currentTrack.colorCount++] = parseHexToColor(colorHex);
          } else {
            serialPrintLn("Warning: Track %d reached MAX_COLORS_PER_TRACK limit!", activeTrackCount);
          }
        }
        colorIndex = slashIndex + 1;
        if (slashIndex == colorsLength) {
          break;
        }
      }

      if (currentTrack.ledCount > 0 && currentTrack.colorCount > 0) {
        activeTrackCount++;
      }
    }
    trackStart = trackEnd + 1;
  }

  if (activeTrackCount > 0) {
    animationActive = true;
    lastAnimationUpdate = millis();
    serialPrintLn("Animation loaded: %d tracks configured.", activeTrackCount);
  }
}

// Dedicated loop runner to handle updating pixels based on configured tracks
void runAnimationLoop() {
  if (!animationActive || activeTrackCount == 0) return;

  if (millis() - lastAnimationUpdate >= ANIMATION_INTERVAL) {
    lastAnimationUpdate = millis();

    for (uint16_t t = 0; t < activeTrackCount; t++) {
      AnimationTrack& track = animTracks[t];
      uint32_t activeColor = track.colors[track.currentColorIndex];

      // Draw the active step color to all matching pins in the sequence track
      for (uint16_t l = 0; l < track.ledCount; l++) {
        if (track.leds[l] < NUM_PIXELS) {
          ledArray.setPixelColor(track.leds[l], activeColor);
        }
      }

      // Advance color index for the next cycle loop step
      track.currentColorIndex = (track.currentColorIndex + 1) % track.colorCount;
    }
    ledArray.show();
  }
}

// Callback class to monitor connection changes
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
      deviceConnected = true;
      digitalWrite(LED_BUILTIN, HIGH);

      String lightColour = "00FF00";
      setHEXColor(BLINKING_LED, lightColour);
      ledArray.show();
      serialPrintLn("Next.js app connected!");
    }

    void onDisconnect(BLEServer* pServer) override {
      deviceConnected = false;
      animationActive = false; // Terminate animation cycles on disconnect
      deviceConnectingStart = millis();

      serialPrintLn("Next.js app disconnected.");
      BLEDevice::startAdvertising();
      serialPrintLn("Restarted advertising... Waiting for reconnect.");
    }
};

// Callback class to handle incoming messages from Next.js
class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
      String value = String(pCharacteristic->getValue().c_str());

      if (value.startsWith("LED|")) {
        animationActive = false; // Standard static command overrides animation mode
        uint16_t index = 4;
        uint16_t length = value.length(); 

        while (index < length) {
          int16_t endIndex = value.indexOf(',', index); 
          if (endIndex == -1) {
            endIndex = length;
          }
          int16_t separatorIndex = value.indexOf(':', index);
          if (separatorIndex != -1 && separatorIndex < endIndex) {
            String ledStr = value.substring(index, separatorIndex);
            String colourStr = value.substring(separatorIndex + 1, endIndex);

            setHEXColorforLEDs(ledStr, colourStr);
          }

          index = endIndex + 1;
        }
        serialPrintLn("Action executed: Set LED colours");
        ledArray.show();
      } 
      else if (value.startsWith("ANIM|")) {
        serialPrintLn("Processing animation stream command...");
        parseAnimationCommand(value);
      }
    }
};

void setup() {
  if (enableSerial) {
    Serial.begin(SERIAL_BAUDRATE);
    while (!Serial && millis() < 3000) {
      delay(10);
    }
    serialPrintLn("Started Serial Output");
  }

  deviceConnectingStart = millis();

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  ledArray.begin();
  ledArray.setBrightness(BRIGHTNESS);
  ledArray.show();

  serialPrintLn("Initialising ESP32-C BLE...");
  BLEDevice::init(BLE_NAME);

  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_READ |
                                         BLECharacteristic::PROPERTY_WRITE
                                       );

  pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  
  pAdvertising->setMinPreferred(0x06);  
  pAdvertising->setMinPreferred(0x12);
  
  BLEDevice::startAdvertising();
  serialPrintLn("BLE Active. Open TableRunner App to connect.");
}

void loop() {
  delay(10); 

  if (deviceConnected) {
    // Run the multi-track color animation when connected and configured
    runAnimationLoop();
  } 
  else {
    unsigned long actualMillis = millis() - deviceConnectingStart;
    unsigned long statusLightOn = (actualMillis / 250) % 2;

    digitalWrite(LED_BUILTIN, statusLightOn ? LOW : HIGH);

    String lightColour = statusLightOn ? "0000FF" : "000000";
    setHEXColor(BLINKING_LED, lightColour);
    ledArray.show();
  }
}

