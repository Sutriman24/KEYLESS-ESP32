#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// --- KONFIGURASI KEY ---
const String key1Address = "16:3a:7d:b6:f6:ce";
const String key2Address = "6c:c6:28:35:dd:9a";

// UUID target
const String TARGET_UUID = "25011997-2501-2501-2501-25019ABCDEF7";

// --- PIN GPIO ---
const int relayPin        = 32;
const int starterRelayPin = 33;
const int ledPin          = 2;
const int SeinKiri        = 25;
const int SeinKanan       = 26;
const int switchPin       = 27;
const int hazardButtonPin = 13;

// --- STATUS SISTEM ---
BLEScan* pBLEScan;
bool systemUnlocked = false;
String statusInfo = "MENCARI...";
String activeMAC = "NONE";
int activeRSSI = 0;

// --- TIMER ANTI-BEGAL ---
unsigned long lastBleSeenTime = 0;
const unsigned long timeoutLimit = 15000;

// --- Tracking BLE sebelumnya ---
bool prevBleDitemukan = false;

// --- Proteksi crank lama ---
unsigned long starterOverheatTimer = 0;
const unsigned long maxCrankTime = 10000;

// --- Hazard Blink (presisi tinggi) ---
unsigned long lastHazardToggle = 0;
bool hazardState = false;
const unsigned long hazardOnTime  = 200;
const unsigned long hazardOffTime = 100;

// --- Print interval ---
unsigned long lastPrintTime = 0;
const unsigned long printInterval = 500;

// --- Scan interval (jarang agar hazard lancar) ---
unsigned long lastScanTime = 0;
const unsigned long scanInterval = 1000;  // scan setiap 1 detik

void setup() {
  Serial.begin(115200);
  pinMode(relayPin, OUTPUT);
  pinMode(starterRelayPin, OUTPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(SeinKiri, OUTPUT);
  pinMode(SeinKanan, OUTPUT);
  pinMode(switchPin, INPUT_PULLUP);
  pinMode(hazardButtonPin, INPUT_PULLUP);

  digitalWrite(relayPin, LOW);
  digitalWrite(starterRelayPin, LOW);
  digitalWrite(ledPin, LOW);
  digitalWrite(SeinKiri, LOW);
  digitalWrite(SeinKanan, LOW);

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);

  Serial.println(">>> SISTEM V6.8: HAZARD SUPER RESPONSIF + SCAN JARANG (READY) <<<");
}

void loop() {
  unsigned long now = millis();

  bool switchState = (digitalRead(switchPin) == LOW);
  bool relayAktif = (digitalRead(relayPin) == HIGH);
  bool hazardButtonPressed = (digitalRead(hazardButtonPin) == LOW);

  // === Hazard kedip – dicek setiap loop (paling prioritas) ===
  if (relayAktif && hazardButtonPressed) {
    unsigned long elapsed = now - lastHazardToggle;

    if (hazardState) {  // nyala
      if (elapsed >= hazardOnTime) {
        digitalWrite(SeinKiri, LOW);
        digitalWrite(SeinKanan, LOW);
        hazardState = false;
        lastHazardToggle = now;
        Serial.println("Hazard: NYALA → MATI");  // debug singkat
      }
    } else {  // mati
      if (elapsed >= hazardOffTime) {
        digitalWrite(SeinKiri, HIGH);
        digitalWrite(SeinKanan, HIGH);
        hazardState = true;
        lastHazardToggle = now;
        Serial.println("Hazard: MATI → NYALA");  // debug singkat
      }
    }
  } else {
    if (hazardState || digitalRead(SeinKiri) == HIGH) {
      digitalWrite(SeinKiri, LOW);
      digitalWrite(SeinKanan, LOW);
      hazardState = false;
      lastHazardToggle = now;
      Serial.println("Hazard: DIMATIKAN");
    }
  }

  // === Scan BLE hanya setiap 3 detik ===
  bool bleDitemukan = false;

  if (now - lastScanTime >= scanInterval) {
    lastScanTime = now;

    BLEScanResults foundDevices = *pBLEScan->start(1, true);

    Serial.println("--- SCAN RESULT ---");

    for (int i = 0; i < foundDevices.getCount(); i++) {
      BLEAdvertisedDevice device = foundDevices.getDevice(i);
      String addr = device.getAddress().toString().c_str();
      int rssi = device.getRSSI();
      String name = device.getName().c_str();

      Serial.printf("Device: %s | RSSI: %d | Name: %s\n", addr.c_str(), rssi, name.c_str());

      bool isKeytag = (addr == key1Address || addr == key2Address);
      bool uuidMatch = false;

      if (device.haveServiceUUID()) {
        BLEUUID advUUID = device.getServiceUUID();
        String uuidStr = advUUID.toString().c_str();
        uuidStr.toUpperCase();
        Serial.print("   Service UUID: ");
        Serial.println(uuidStr);

        if (uuidStr == TARGET_UUID) {
          uuidMatch = true;
        }
      }

      if (device.haveManufacturerData()) {
        std::string manuf = std::string(device.getManufacturerData().c_str());
        if (manuf.length() >= 25 && manuf[0] == 0x4C && manuf[1] == 0x00 && manuf[2] == 0x02 && manuf[3] == 0x15) {
          char uuidStr[37];
          snprintf(uuidStr, sizeof(uuidStr),
                   "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                   (uint8_t)manuf[4], (uint8_t)manuf[5], (uint8_t)manuf[6], (uint8_t)manuf[7],
                   (uint8_t)manuf[8], (uint8_t)manuf[9],
                   (uint8_t)manuf[10], (uint8_t)manuf[11],
                   (uint8_t)manuf[12], (uint8_t)manuf[13],
                   (uint8_t)manuf[14], (uint8_t)manuf[15], (uint8_t)manuf[16],
                   (uint8_t)manuf[17], (uint8_t)manuf[18], (uint8_t)manuf[19]);

          String detected = String(uuidStr);
          Serial.print("   iBeacon Proximity UUID: ");
          Serial.println(detected);

          if (detected == TARGET_UUID) {
            uuidMatch = true;
          }
        }
      }

      bool detected = false;
      if (isKeytag && rssi > -75) {
        detected = true;
      } else if (uuidMatch && rssi > -75) {
        detected = true;
      }

      if (detected) {
        bleDitemukan = true;
        activeMAC = addr;
        activeRSSI = rssi;
        lastBleSeenTime = now;
        break;
      }
    }

    Serial.println("--- END SCAN ---");
    pBLEScan->clearResults();
  }

  // Logic unlock/lock
  bool baruDikenali = bleDitemukan && !prevBleDitemukan;

  if (baruDikenali && !systemUnlocked) {
    systemUnlocked = true;
    statusInfo = "KUNCI TERDETEKSI";
    kedipkanSein(1, 1000, 500);
    delay(500);
  }

  unsigned long timeElapsed = now - lastBleSeenTime;
  long sisaWaktu = (timeElapsed < timeoutLimit) ? (timeoutLimit - timeElapsed) / 1000 : 0;

  if (switchState) {
    if (timeElapsed < timeoutLimit && (systemUnlocked || relayAktif)) {
      delay(500);
      digitalWrite(relayPin, HIGH);
      digitalWrite(ledPin, HIGH);
      statusInfo = "MESIN MENYALA";
    } else {
      digitalWrite(relayPin, LOW);
      digitalWrite(ledPin, LOW);
      digitalWrite(starterRelayPin, LOW);
      starterOverheatTimer = 0;
      systemUnlocked = false;
      statusInfo = "LOCKED / TIMEOUT";
    }
  } else {
    digitalWrite(starterRelayPin, LOW);
    starterOverheatTimer = 0;

    if (!bleDitemukan && systemUnlocked) {
      digitalWrite(relayPin, LOW);
      digitalWrite(ledPin, LOW);
      delay(1000);
      kedipkanSein(2, 500, 200);
      systemUnlocked = false;
    }

    if (!switchState) {
      digitalWrite(relayPin, LOW);
      digitalWrite(ledPin, LOW);
    }

    statusInfo = bleDitemukan ? "STANDBY (READY)" : "STANDBY (LOCKED)";
  }

  // Print smooth
  if (now - lastPrintTime >= printInterval) {
    lastPrintTime = now;

    Serial.println("=================SMARTKEYLESS ESP32=================");
    Serial.printf(" STATUS SISTEM       : %s\n", statusInfo.c_str());
    Serial.printf(" BLE KEY             : %s\n", bleDitemukan ? "CONNECTED" : "DISCONNECTED");
    Serial.printf(" RSSI AKTIF          : %d dBm (Min: -75)\n", activeRSSI);
    Serial.printf(" DETIK TANPA KUNCI   : %lu / 15 Detik\n", (timeElapsed / 1000));
    Serial.println("----------------------------------------------------");
    Serial.printf(" SAKLAR (GPIO 27)    : %s\n", switchState ? "ON" : "OFF");
    Serial.printf(" RELAY (GPIO 32)     : %s\n", relayAktif ? "ON" : "OFF");
    Serial.println("====================================================");
  }

  prevBleDitemukan = bleDitemukan;
  delay(50);  // super kecil agar loop cepat
}

void kedipkanSein(int jumlah, int durasiNyala, int durasiMati) {
  for (int i = 0; i < jumlah; i++) {
    digitalWrite(SeinKiri, HIGH);
    digitalWrite(SeinKanan, HIGH);
    delay(durasiNyala);
    digitalWrite(SeinKiri, LOW);
    digitalWrite(SeinKanan, LOW);
    if (i < jumlah - 1) delay(durasiMati);
  }
}