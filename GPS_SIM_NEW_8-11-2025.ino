 // GPS: RX - pin 2
 // TX - pin 3
 // SIM: TX - pin 7
 // RX - pin 6
 //* SD card attached to SPI bus as follows:
 //** MOSI - pin 11
 //** MISO - pin 12
 //** SCK - pin 13
 //** CS - pin 4
 
// #include <SoftwareSerial.h>
#include <NeoSWSerial.h>
#include <TinyGPS++.h>
#include <SD.h>
#include <SPI.h>

// GPS pins
#define GPS_RX 3
#define GPS_TX 2

// SIM800L pins
#define SIM_RX 7
#define SIM_TX 6

// LEDs
#define LED_GPS 8
#define LED_SMS 9

// SD card chip select
#define SD_CS 4

// Logging interval (2 min)
const unsigned long LOG_INTERVAL = 120000;
// GPS print interval (1 second)
const unsigned long PRINT_INTERVAL = 1000;

TinyGPSPlus gps;
NeoSWSerial gpsSerial(GPS_RX, GPS_TX);
NeoSWSerial simSerial(SIM_RX, SIM_TX);

unsigned long lastLogTime = 0;
unsigned long lastPrintTime = 0;

const char phoneNumber[] = "+639435704513"; // Change to your number

// ==== CHANGE #1: last known GPS data cache ====
double lastLat = 0.0, lastLng = 0.0, lastAlt = 0.0;
int lastHour = 0, lastMin = 0, lastSec = 0;
int lastDay = 0, lastMonth = 0, lastYear = 0;
bool hasFix = false;

void setup() {
  Serial.begin(9600);
  gpsSerial.begin(9600);
  simSerial.begin(9600);

  pinMode(LED_GPS, OUTPUT);
  pinMode(LED_SMS, OUTPUT);
  digitalWrite(LED_GPS, LOW);
  digitalWrite(LED_SMS, LOW);

  Serial.print("Initializing SD card...");
  if (!SD.begin(SD_CS)) {
    Serial.println("SD init failed!");
    while (true);
  }
  Serial.println("SD OK");

  initSIM();
}

void loop() {
  // Read GPS data continuously
  gpsSerial.listen();
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }

  // ==== CHANGE #1: cache only when valid ====
  if (gps.location.isValid()) {
    lastLat = gps.location.lat();
    lastLng = gps.location.lng();
    hasFix = true;
  }
  if (gps.altitude.isValid()) {
    lastAlt = gps.altitude.meters();
  }
  if (gps.time.isValid()) {
    lastHour = gps.time.hour() + 8;
    if (lastHour >= 24) lastHour -= 24;
    lastMin = gps.time.minute();
    lastSec = gps.time.second();
  }
  if (gps.date.isValid()) {
    lastDay = gps.date.day();
    lastMonth = gps.date.month();
    lastYear = gps.date.year();
  }

  // GPS LED indicator
  digitalWrite(LED_GPS, gps.location.isValid() ? HIGH : LOW);

  // Print GPS data every 1 second if valid
  if (gps.location.isValid() && millis() - lastPrintTime >= PRINT_INTERVAL) {
    lastPrintTime = millis();
    Serial.print("Lat: "); Serial.println(gps.location.lat(), 6);
    Serial.print("Lng: "); Serial.println(gps.location.lng(), 6);
    Serial.print("Alt: "); Serial.println(gps.altitude.meters(), 2);
  }

  // ==== CHANGE #2: Get fresh GPS before SIM ====
  if (millis() - lastLogTime >= LOG_INTERVAL) {
    lastLogTime = millis();

    refreshGPS(3000); // actively listen before switching to SIM
    logToSD();
    sendGPSviaSMS();
  }
}

void refreshGPS(unsigned long ms) {
  gpsSerial.listen();
  unsigned long start = millis();
  bool gotFix = false;

  while (millis() - start < ms) {
    while (gpsSerial.available()) {
      gps.encode(gpsSerial.read());
    }
    if (gps.location.isValid()) {
      lastLat = gps.location.lat();
      lastLng = gps.location.lng();
      hasFix = true;
      gotFix = true;
    }
    if (gps.altitude.isValid()) lastAlt = gps.altitude.meters();
    if (gps.time.isValid()) {
      lastHour = gps.time.hour() + 8;
      if (lastHour >= 24) lastHour -= 24;
      lastMin = gps.time.minute();
      lastSec = gps.time.second();
    }
    if (gps.date.isValid()) {
      lastDay = gps.date.day();
      lastMonth = gps.date.month();
      lastYear = gps.date.year();
    }
  }

  Serial.print("[refreshGPS] Fix status after refresh: ");
  Serial.println(gotFix ? "Valid" : "No new fix — using last known");
}



void initSIM() {
  simSerial.listen();
  delay(1000);
  sendAT("AT");
  sendAT("AT+CMGF=1"); // SMS text mode
}

void flushSerial() {
  while (simSerial.available()) simSerial.read();
}

void sendAT(const char* cmd) {
  flushSerial();
  simSerial.println(cmd);
  delay(500);
  while (simSerial.available()) {
    Serial.write(simSerial.read());
  }
}

void logToSD() {
  File file = SD.open("GpsDat.txt", FILE_WRITE);
  if (file) {
    writeGPSData(file);
    file.close();
    Serial.println("GPS logged to SD.");
  } else {
    Serial.println("Failed to open SD file.");
  }
}

void sendGPSviaSMS() {
  String smsText = buildGPSString();

  Serial.println("=== SMS text to send ===");
  Serial.println(smsText);
  Serial.println("========================");

  simSerial.listen();
  flushSerial();

  simSerial.println("AT+CMGF=1");
  delay(300);
  simSerial.print("AT+CMGS=\"");
  simSerial.print(phoneNumber);
  simSerial.println("\"");
  delay(100); // small but enough

  for (int i = 0; i < smsText.length(); i++) {
    simSerial.write(smsText[i]);
    delay(3); // give SIM800L time to process each byte
  }
  simSerial.write(26); // CTRL+Z
  Serial.println("SMS send command issued.");
}


String buildGPSString() {
  String out;

  if (hasFix && lastLat != 0.0 && lastLng != 0.0) {
    out += "maps.google.com?q=" + String(lastLat, 6) + "," + String(lastLng, 6) + "\n";
    out += "Time: ";
    if (lastHour < 10) out += "0";
    out += String(lastHour) + ":";
    if (lastMin < 10) out += "0";
    out += String(lastMin) + ":";
    if (lastSec < 10) out += "0";
    out += String(lastSec) + "\n";
    /*out += "Date: " + String(lastMonth) + "-" + String(lastDay) + "-" + String(lastYear) + "\n";*/
  } else {
    out += "No GPS fix yet.\n";
  }

  return out;
}

void writeGPSData(Print &out) {
  out.print("Altitude: "); out.println(gps.altitude.meters(), 3);
  out.print("Latitude: "); out.println(gps.location.lat(), 6);
  out.print("Longitude: "); out.println(gps.location.lng(), 6);

  out.print("Time: ");
  if (gps.time.hour() < 10) out.print("0");
  out.print(gps.time.hour() + 8); // Timezone adjust
  out.print(":");
  if (gps.time.minute() < 10) out.print("0");
  out.print(gps.time.minute());
  out.print(":");
  if (gps.time.second() < 10) out.print("0");
  out.println(gps.time.second());

  out.print("Date: ");
  if (gps.date.isValid()) {
    out.print(gps.date.month());
    out.print("/");
    out.print(gps.date.day());
    out.print("/");
    out.println(gps.date.year());
  }

  out.print("https://www.google.com/maps/place/");
  out.print(gps.location.lat(), 6);
  out.print(",");
  out.println(gps.location.lng(), 6);
  out.println();
}