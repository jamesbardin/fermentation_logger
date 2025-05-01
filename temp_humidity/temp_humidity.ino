#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include "RTClib.h"
#include <DHT.h>

#define DHTPIN       2
#define DHTTYPE      DHT22
#define SD_CS_PIN    10
#define LOG_INTERVAL 1800000UL        // 1 s now; change to 1 800 000UL for 30 min

DHT        dht(DHTPIN, DHTTYPE);
RTC_DS1307 rtc;
File       logfile;
unsigned long lastLog = 0;

void setup() {
  Serial.begin(9600);
  dht.begin();

  /* ---------- SD card ---------- */
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println(F("SD init failed"));  while (1);
  }

  // YOU CAN CHANGE YOUR FILE NAME HERE OR IT WILL JUST KEEP WRITING TO THE TEST FILE
  const char filename[] = "LOG00.CSV";   // <-- fixed name
  const bool newFile = !SD.exists(filename);
  logfile = SD.open(filename, FILE_WRITE);   // FILE_WRITE = “append or create”
  if (!logfile) { Serial.println(F("File open failed")); while (1); }

  if (newFile) {                           // add header only once
    logfile.println(F("Date,Time,TempC,TempF,Humidity"));
    logfile.flush();
  }

  /* ---------- RTC ---------- */
  if (!rtc.begin())  Serial.println(F("RTC not found"));
  if (!rtc.isrunning())  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  Serial.println(F("Logging started"));
}

void loop() {
  unsigned long now = millis();
  if (now - lastLog >= LOG_INTERVAL) {
    lastLog = now;

    float hum   = dht.readHumidity();
    float tempC = dht.readTemperature();
    if (isnan(hum) || isnan(tempC)) {
      Serial.println(F("Sensor read failed"));
      return;
    }
    float tempF = tempC * 9.0 / 5.0 + 32.0;

    DateTime t = rtc.now();
    char stamp[20];
    sprintf(stamp, "%04d-%02d-%02d,%02d:%02d:%02d",
            t.year(), t.month(), t.day(),
            t.hour(), t.minute(), t.second());

    logfile.print(stamp);
    logfile.print(',');
    logfile.print(tempC, 1);
    logfile.print(',');
    logfile.print(tempF, 1);
    logfile.print(',');
    logfile.println(hum, 1);
    logfile.flush();

    Serial.print(F("Logged: "));
    Serial.println(stamp);
  }
}
