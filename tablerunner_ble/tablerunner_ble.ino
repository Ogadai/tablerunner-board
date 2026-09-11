#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <Adafruit_NeoPixel.h>

#define LED_BUILTIN 8
#define SERIAL_BAUDRATE 115200

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_NAME            "TABLERUNNER-ASC"

#define NUM_PIXELS          240
#define PIXELS_PIN          1
#define BLINKING_LED        0
#define BRIGHTNESS          200

#define MAX_TRACKS          10  
#define MAX_LEDS_PER_TRACK  24  
#define MAX_COLORS_PER_TRACK 8  
#define ANIMATION_INTERVAL 500  

struct AnimationTrack {
  uint16_t leds[MAX_LEDS_PER_TRACK];
  uint16_t ledCount = 0;
  uint32_t colors[MAX_COLORS_PER_TRACK]; 
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

Adafruit_NeoPixel ledArray(NUM_PIXELS, PIXELS_PIN, NEO_GRB + NEO_KHZ800);

// FIXED: Increased buffer size and safety limiters to completely prevent memory corruption
void serialPrintLn(const char *format, ...) {
  if (enableSerial) {
    char buffer[256]; 
  
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    Serial.println(buffer);
  }
}

uint32_t parseHexToColor(const String& hexColorStr) {
  // FIXED: If BLE packet truncation causes a shortened color string, ignore it safely
  if (hexColorStr.length() < 6) return 0;

  long rgb = strtol(hexColorStr.c_str(), NULL, 16);
  
  uint8_t rawRed   = rgb >> 16;
  uint8_t rawGreen = (rgb & 0x00ff00) >> 8;
  uint8_t rawBlue  = (rgb & 0x0000ff);

  uint8_t gammaRed   = ledArray.gamma8(rawRed);
  uint8_t gammaGreen = ledArray.gamma8(rawGreen);
  uint8_t gammaBlue  = ledArray.gamma8(rawBlue);

  return ledArray.Color(gammaRed, gammaGreen, gammaBlue);
}

// FIXED: Explicitly protects against signed negative values (-1 underflow)
void setHEXColor(int32_t index, const String& hexColorStr) {
  if (index >= 0 && index < NUM_PIXELS) {
    uint32_t packedColor = parseHexToColor(hexColorStr);
    ledArray.setPixelColor(index, packedColor);
  } else {
    serialPrintLn("Warning: Out of bounds index skipped.");
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
    
    // FIXED: Parse as a signed long first to catch negative inputs safely
    int32_t led = atol(ledStr.c_str()); 

    setHEXColor(led, hexColorStr);
    ledCount++;

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
        if (colorHex.length() >= 6) { // Safeguard truncation
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
    lastAnimationUpdate = millis();

    for (uint16_t t = 0; t < activeTrackCount; t++) {
      AnimationTrack& track = animTracks[t];
      uint32_t activeColor = track.colors[track.currentColorIndex];

      for (uint16_t l = 0; l < track.ledCount; l++) {
        if (track.leds[l] < NUM_PIXELS) {
          ledArray.setPixelColor(track.leds[l], activeColor);
        }
      }
      track.currentColorIndex = (track.currentColorIndex + 1) % track.colorCount;
    }
    ledArray.show();
  }
}

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
      deviceConnected = true;
      digitalWrite(LED_BUILTIN, HIGH);
      setHEXColor(BLINKING_LED, "00FF00");
      ledArray.show();
      serialPrintLn("Next.js app connected!");
    }

    void onDisconnect(BLEServer* pServer) override {
      deviceConnected = false;
      animationActive = false; 
      deviceConnectingStart = millis();
      BLEDevice::startAdvertising();
    }
};

class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
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
        ledArray.show();
      } 
      else if (value.startsWith("ANIM|")) {
        parseAnimationCommand(value);
      }
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

  ledArray.begin();
  ledArray.setBrightness(BRIGHTNESS);
  ledArray.show();

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
  
  // Set explicit MTU size preference
  pAdvertising->setMinPreferred(0x06);  
  pAdvertising->setMinPreferred(0x12);
  
  BLEDevice::startAdvertising();
}

void loop() {
  delay(10); 

  if (deviceConnected) {
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
