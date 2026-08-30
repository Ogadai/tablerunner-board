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
#define BRIGHTNESS          5
// 21

unsigned long deviceConnectingStart = 0;
bool deviceConnected = false;

bool enableSerial = true;

/* the LED pixel array controller */
Adafruit_NeoPixel ledArray(NUM_PIXELS, PIXELS_PIN, NEO_GRB + NEO_KHZ800);

void serialPrintLn(const char *format, ...) {
  if (enableSerial) {
    char buffer[128]; // Adjust buffer size based on your longest expected message
  
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    Serial.println(buffer);
  }
}

void setHEXColor(uint16_t index, String& hexColorStr) {
  //to RGB
  long rgb = strtol(hexColorStr.c_str(), NULL, 16);

  uint8_t red = rgb >> 16;
  uint8_t green = (rgb & 0x00ff00) >> 8;
  uint8_t blue = (rgb & 0x0000ff);

  ledArray.setPixelColor(index, ledArray.Color(red, green, blue));

  serialPrintLn("Set LED %d to RGB(%d, %d, %d)", index, red, green, blue);
}

void setHEXColorforLEDs(String& indexStr, String& hexColorStr) {
  uint16_t index = 0;
  uint16_t length = strlen(indexStr.c_str());  

  while (index < length) {
    uint16_t endIndex = indexStr.indexOf("/", index);
    if (endIndex == -1 || endIndex > length) {
      endIndex = length;
    }
    String ledStr = indexStr.substring(index, endIndex);
    uint16_t led = atol(ledStr.c_str());

    setHEXColor(led, hexColorStr);

    index = endIndex + 1;
  }
}

// Callback class to monitor connection changes
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
      deviceConnected = true;
      digitalWrite(LED_BUILTIN, HIGH);
      serialPrintLn("Next.js app connected!");
    }

    void onDisconnect(BLEServer* pServer) override {
      deviceConnected = false;
      deviceConnectingStart = millis();

      serialPrintLn("Next.js app disconnected.");
      
      // ESP32-C requires advertising to restart immediately so it can reconnect
      BLEDevice::startAdvertising();
      serialPrintLn("Restarted advertising... Waiting for reconnect.");
    }
};
// Callback class to handle incoming messages from Next.js
class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
      // FIX: Use Arduino's 'String' instead of 'std::string'
      String value = pCharacteristic->getValue();

      if (value.startsWith("LED|")) {
        uint16_t index = 4;
        uint16_t length = strlen(value.c_str()); 

        while (index < length) {
          uint16_t endIndex = value.indexOf(",", index);
          if (endIndex == -1 || endIndex > length) {
            endIndex = length;
          }
          uint16_t separatorIndex = value.indexOf(":", index);
          if (separatorIndex != -1) {
            String ledStr = value.substring(index, separatorIndex);
            String colourStr = value.substring(separatorIndex + 1, endIndex);

            setHEXColorforLEDs(ledStr, colourStr);
          }

          index = endIndex + 1;
        }
        serialPrintLn("Action executed: Set LED colours");
        ledArray.show();
      }
    }
};


void setup() {
  if (enableSerial) {
    Serial.begin(SERIAL_BAUDRATE);

    // Wait up to 3 seconds for the Serial Monitor to connect
    while (!Serial && millis() < 3000) {
      delay(10);
    }

    Serial.println("Started Serial Output");
  }

  deviceConnectingStart = millis();

  // Configure internal built-in status indicator LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  // Initialise the led array
  ledArray.begin();
  ledArray.setBrightness(BRIGHTNESS);

  serialPrintLn("Initialising ESP32-C BLE...");

  BLEDevice::init(BLE_NAME);

  // Spin up the GATT Bluetooth server
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Instantiate primary data container environment
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Bind write property permission flags to match component requirements
  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_READ |
                                         BLECharacteristic::PROPERTY_WRITE
                                       );

  pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());

  // Spin up background wireless threads
  pService->start();

  // Broadcast device visibility signals
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  
  // Performance optimization parameters required for reliable connections
  pAdvertising->setMinPreferred(0x06);  
  pAdvertising->setMinPreferred(0x12);
  
  BLEDevice::startAdvertising();
  serialPrintLn("BLE Active. Open TableRunner App to connect.");
}

void loop() {
  // Main background loop remains non-blocking for performance efficiency
  delay(10); 

  if (!deviceConnected) {
    unsigned long actualMillis = millis() - deviceConnectingStart;
    unsigned long statusLightOn = (actualMillis / 250) % 2;

    digitalWrite(LED_BUILTIN, statusLightOn ? LOW : HIGH);
  }
}
