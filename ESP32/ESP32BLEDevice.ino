#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <Wire.h>
#include <Adafruit_INA219.h>

#define FSR_PIN 34   // Pin ที่เชื่อมต่อกับ FSR
#define BUZZER_PIN 26 // Pin ของ Buzzer

int fsr_value = 0;
bool is_running = false;

// BLE UUIDs
#define SERVICE_UUID "FF10"
#define STATE_UUID "FF11"
#define TIMER_UUID "FF13"
#define BATTERY_UUID "FF14"

BLEServer *pServer = nullptr;
BLECharacteristic *state_characteristic;
BLECharacteristic *timer_characteristic;
BLECharacteristic *battery_characteristic;
BLEAdvertising *pAdvertising;

bool deviceConnected = false;
bool oldDeviceConnected = false;

Adafruit_INA219 ina219;

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    deviceConnected = true;
    Serial.println("Device Connected");
  }

  void onDisconnect(BLEServer *pServer) {
    deviceConnected = false;
    Serial.println("Device Disconnected, Restarting Advertising...");
    delay(500);
    pAdvertising->start();
  }
};

void setup() {
  Serial.begin(115200);
  pinMode(FSR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  if (!ina219.begin()) {
    Serial.println("ไม่สามารถเชื่อมต่อกับ INA219");
    while (1);
  }
  Serial.println("INA219 เริ่มทำงานสำเร็จ!");

  BLEDevice::init("ESP32 FSR Server");

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *fsr_service = pServer->createService(SERVICE_UUID);

  state_characteristic = fsr_service->createCharacteristic(STATE_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  timer_characteristic = fsr_service->createCharacteristic(TIMER_UUID, BLECharacteristic::PROPERTY_WRITE);
  battery_characteristic = fsr_service->createCharacteristic(BATTERY_UUID, BLECharacteristic::PROPERTY_NOTIFY);

  fsr_service->start();

  pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();

  Serial.println("BLE Server Started");
}

void loop() {
  static unsigned long last_send_time = 0;
  static unsigned long last_batt_check = 0;
  const unsigned long send_interval = 100;
  const unsigned long batt_check_interval = 10000; // ✅ เช็คแบตทุก 10 วิ
  unsigned long current_time = millis();

  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    Serial.println("Restarting Advertising...");
    oldDeviceConnected = deviceConnected;
  }

  if (deviceConnected && !oldDeviceConnected) {
    Serial.println("Connected to device");
    oldDeviceConnected = deviceConnected;
  }

  // รับคำสั่งจากแอป
  if (timer_characteristic->getValue().length() > 0) {
    String command = timer_characteristic->getValue().c_str();
    Serial.print("Command: ");
    Serial.println(command);

    if (command == "start") {
      is_running = true;
      Serial.println("Start sending FSR values...");
    } else if (command == "stop") {
      is_running = false;
      Serial.println("Stop sending FSR values.");
    } 
    timer_characteristic->setValue("");
  }

  // ✅ เช็คแบตทุก 10 วิ
  if (current_time - last_batt_check >= batt_check_interval) {
    last_batt_check = current_time;

    float total_percentage = 0;
    float total_voltage = 0;

    for (int i = 0; i < 10; i++) {
      float voltage = ina219.getBusVoltage_V();
      float battery_percentage = (voltage - 3.0) / (4.2 - 3.0) * 100;
      battery_percentage = constrain(battery_percentage, 0, 100);

      total_percentage += battery_percentage;
      total_voltage += voltage;
      delay(10);
    }

    float average_percentage = total_percentage / 10;
    float average_voltage = total_voltage / 10;

    String batteryData = "Battery: " + String(average_percentage) + "%";
    battery_characteristic->setValue(batteryData.c_str());
    battery_characteristic->notify();

    Serial.print("Average Battery Voltage: ");
    Serial.print(average_voltage);
    Serial.println(" V");

    Serial.print("Average Battery Percentage: ");
    Serial.print(average_percentage);
    Serial.println(" %");

    // ✅ ถ้าแรงดันต่ำกว่า 3.8V ให้เปิดบัซเซอร์
    if (average_voltage < 3.8) {
      digitalWrite(BUZZER_PIN, HIGH);
      delay(3000);
      digitalWrite(BUZZER_PIN, LOW);
    }
  }

  if (is_running) {
    if (current_time - last_send_time >= send_interval) {
      last_send_time = current_time;
      fsr_value = analogRead(FSR_PIN);
      int mapped_value = map(fsr_value, 0, 4095, 100, 0);
      state_characteristic->setValue(String(mapped_value).c_str());
      state_characteristic->notify();
      Serial.print("FSR Value: "); Serial.println(mapped_value);
    }
  }
}
