#define FASTLED_ESP32_FLASH_LOCK 1
#define FASTLED_INTERNAL
// ==========================================
// FIX 1: Force FastLED to use Hardware SPI for standard definitions
// ==========================================
#define FASTLED_ALL_PINS_HARDWARE_SPI

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <FastLED.h> 
#include <esp_bt.h>

#define LED_BUILTIN 8
#define SERIAL_BAUDRATE 115200

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_NAME            "TABLERUNNER-ASC"

#define NUM_PIXELS          240
#define PIXELS_PIN          5
#define BLINKING_LED        0
#define BRIGHTNESS          50

#define MAX_TRACKS          10  
#define MAX_LEDS_PER_TRACK  24  
#define MAX_COLORS_PER_TRACK 8  
#define ANIMATION_INTERVAL 500  

struct AnimationTrack {
  uint16_t leds[MAX_LEDS_PER_TRACK];
  uint16_t ledCount = 0;
  CRGB colors[MAX_COLORS_PER_TRACK]; 
  uint16_t colorCount = 0;
  uint16_t currentColorIndex = 0;
};

AnimationTrack animTracks[MAX_TRACKS];
uint16_t activeTrackCount = 0;
bool animationActive = false;
unsigned long lastAnimationUpdate = 0;

unsigned long deviceConnectingStart = 0;
bool deviceConnected = false;
bool enableSerial = true;

// FIXED: Tracks the previous status light state to prevent hammering FastLED.show()
unsigned long lastStatusLightState = 999; 
bool ledUpdated = false;
bool ledEditing = false;

CRGB leds[NUM_PIXELS];

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

CRGB parseHexToColor(const String& hexColorStr) {
  if (hexColorStr.length() < 6) return CRGB::Black;

  long rgb = strtol(hexColorStr.c_str(), NULL, 16);
  
  uint8_t rawRed   = rgb >> 16;
  uint8_t rawGreen = (rgb & 0x00ff00) >> 8;
  uint8_t rawBlue  = (rgb & 0x0000ff);

  CRGB color = CRGB(rawRed, rawGreen, rawBlue);
  color.r = applyGamma_video(color.r, 2.5);
  color.g = applyGamma_video(color.g, 2.5);
  color.b = applyGamma_video(color.b, 2.5);

  return color;
}

void setHEXColor(int32_t index, const String& hexColorStr) {
  if (index >= 0 && index < NUM_PIXELS) {
    leds[index] = parseHexToColor(hexColorStr);
  }
}

void setHEXColorforLEDs(const String& indexStr, const String& hexColorStr) {
  uint16_t index = 0;
  uint16_t length = indexStr.length();

  while (index < length) {
    int16_t endIndex = indexStr.indexOf('/', index);
    if (endIndex == -1) {
      endIndex = length;
    }
    String ledStr = indexStr.substring(index, endIndex);
    int32_t led = atol(ledStr.c_str()); 

    setHEXColor(led, hexColorStr);
    index = endIndex + 1;
  }
}

void parseAnimationCommand(const String& payload) {
  animationActive = false; 
  activeTrackCount = 0;
  
  uint16_t trackStart = 5; 
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

      uint16_t ledIndex = 0;
      uint16_t ledsLength = ledsSection.length();
      while (ledIndex < ledsLength) {
        int16_t slashIndex = ledsSection.indexOf('/', ledIndex);
        if (slashIndex == -1) {
          slashIndex = ledsLength;
        }
        
        if (currentTrack.ledCount < MAX_LEDS_PER_TRACK) {
          int32_t rawLed = atol(ledsSection.substring(ledIndex, slashIndex).c_str());
          if (rawLed >= 0 && rawLed < NUM_PIXELS) {
             currentTrack.leds[currentTrack.ledCount++] = rawLed;
          }
        }
        ledIndex = slashIndex + 1;
      }

      uint16_t colorIndex = 0;
      uint16_t colorsLength = colorsSection.length();
      while (colorIndex < colorsLength) {
        int16_t slashIndex = colorsSection.indexOf('/', colorIndex);
        if (slashIndex == -1) {
          slashIndex = colorsLength;
        }
        String colorHex = colorsSection.substring(colorIndex, slashIndex);
        if (colorHex.length() >= 6) { 
          if (currentTrack.colorCount < MAX_COLORS_PER_TRACK) {
            currentTrack.colors[currentTrack.colorCount++] = parseHexToColor(colorHex);
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
    serialPrintLn("Animation loaded.");
  }
}

void runAnimationLoop() {
  if (!animationActive || activeTrackCount == 0) return;

  if (millis() - lastAnimationUpdate >= ANIMATION_INTERVAL) {
    ledEditing = true;
    lastAnimationUpdate = millis();

    for (uint16_t t = 0; t < activeTrackCount; t++) {
      AnimationTrack& track = animTracks[t];
      CRGB activeColor = track.colors[track.currentColorIndex];

      for (uint16_t l = 0; l < track.ledCount; l++) {
        if (track.leds[l] < NUM_PIXELS) {
          leds[track.leds[l]] = activeColor;
        }
      }
      track.currentColorIndex = (track.currentColorIndex + 1) % track.colorCount;
    }
    ledUpdated = true;
    ledEditing = false;
  }
}

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
      deviceConnected = true;
      digitalWrite(LED_BUILTIN, HIGH);
      setHEXColor(BLINKING_LED, "00FF00");
      ledUpdated = true;
      serialPrintLn("Next.js app connected!");
    }

    void onDisconnect(BLEServer* pServer) override {
      deviceConnected = false;
      animationActive = false; 
      activeTrackCount = 0; // FIXED: Safely wipe track assignments down to 0 on disconnect
      lastStatusLightState = 999; // Reset state machine trigger
      deviceConnectingStart = millis();
      serialPrintLn("Disconnected. Reinforcing Advertising arrays.");
      BLEDevice::startAdvertising();
    }
};

class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
      ledEditing = true;
      String value = String(pCharacteristic->getValue().c_str());

      if (value.startsWith("LED|")) {
        animationActive = false;
        
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
        
        ledUpdated = true;
      } 
      else if (value.startsWith("ANIM|")) {
        parseAnimationCommand(value);
      }
      else if (value.startsWith("BRIT|")) {
        // Extract string data past "BRIT|" prefix
        String brightnessStr = value.substring(5);
        int newBrightness = brightnessStr.toInt();
        
        // Constrain incoming data to safe 0-255 bounds for FastLED
        newBrightness = constrain(newBrightness, 0, 255);
        
        FastLED.setBrightness(newBrightness);
        ledUpdated = true;
        serialPrintLn("Brightness updated to: %d", newBrightness);
      }

      ledEditing = false;
    }
};

void setup() {
  if (enableSerial) {
    Serial.begin(SERIAL_BAUDRATE);
    delay(500);
  }

  deviceConnectingStart = millis();
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  // C3 defaults: change the MOSI pin to 5 before initialization
  SPI.end(); // Clear defaults if any exist
  // Parameters: SCK, MISO, MOSI, SS (We only care about MOSI being Pin 5)
  SPI.begin(2, 3, 5, 4); 

  // Set the hardware SPI clock speed to 4MHz (extremely stable for single-core WS2812B)
  SPI.setFrequency(4000000); 

  // Standard WS2812B, but operates via hardware DMA SPI
  FastLED.addLeds<WS2812B, PIXELS_PIN, GRB>(leds, NUM_PIXELS);
  
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.show();

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
  
  BLEDevice::startAdvertising();

  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P3);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P3);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P3);
}

void loop() {
  delay(1); 

  if (deviceConnected) {
    if (animationActive) {
      runAnimationLoop();
    }
  } 
  else {
    unsigned long actualMillis = millis() - deviceConnectingStart;
    unsigned long statusLightOn = (actualMillis / 250) % 2;

    if (statusLightOn != lastStatusLightState) {
      lastStatusLightState = statusLightOn;
      digitalWrite(LED_BUILTIN, statusLightOn ? LOW : HIGH);

      String lightColour = statusLightOn ? "0000FF" : "000000";
      setHEXColor(BLINKING_LED, lightColour);
      ledUpdated = true;
    }
  }

  EVERY_N_MILLISECONDS(100) {
    if (ledUpdated && !ledEditing) {
      ledUpdated = false;

      FastLED.show();
    }
  }
}
