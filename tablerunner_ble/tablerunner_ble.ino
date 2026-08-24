#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

#define LED_BUILTIN 8

// Unique UUIDs matching your Next.js frontend application
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

bool deviceConnected = false;

// Callback class to monitor connection changes
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
      deviceConnected = true;
      Serial.println("Next.js app connected!");
    }

    void onDisconnect(BLEServer* pServer) override {
      deviceConnected = false;
      Serial.println("Next.js app disconnected.");
      
      // ESP32-C requires advertising to restart immediately so it can reconnect
      BLEDevice::startAdvertising();
      Serial.println("Restarted advertising... Waiting for reconnect.");
    }
};
// Callback class to handle incoming messages from Next.js
class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
      // FIX: Use Arduino's 'String' instead of 'std::string'
      String value = pCharacteristic->getValue();

      if (value.length() > 0) {
        Serial.print("Received message: ");
        
        // Loop through the String characters safely
        for (unsigned int i = 0; i < value.length(); i++) {
          Serial.print(value[i]);
        }
        Serial.println();

        // Concrete Example: Execute action if string matches
        if (value == "LED_ON") {
          digitalWrite(LED_BUILTIN, HIGH);
          Serial.println("Action executed: Built-in LED Turned ON");
        } else if (value == "LED_OFF") {
          digitalWrite(LED_BUILTIN, LOW);
          Serial.println("Action executed: Built-in LED Turned OFF");
        }
      }
    }
};


void setup() {
  Serial.begin(115200);
  
  // Configure internal built-in status indicator LED
  #ifdef LED_BUILTIN
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
  #endif

  Serial.println("Initialising ESP32-C BLE...");

  // Match the name prefix filter 'ESP32' targeted by Next.js app
  BLEDevice::init("ESP32-C3-Board");

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
  Serial.println("BLE Active. Open your Next.js app to connect.");
}

void loop() {
  // Main background loop remains non-blocking for performance efficiency
  delay(10); 
}
