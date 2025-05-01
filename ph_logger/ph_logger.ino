#include <SoftwareSerial.h>
#include <SD.h>
#include <SPI.h>
#include <RTClib.h>

#define PH_RX   3
#define PH_TX   2
#define RTD_PIN A0
#define SD_CS   10

SoftwareSerial phSerial(PH_RX, PH_TX);
RTC_DS3231 rtc;
bool rtcPresent = false;

// CHANGE TIME HERE FOR DIFFERENT INTERVAL (in MS, so 1800000 = 30 min)
const unsigned long READ_INTERVAL_MS = 5000;
unsigned long lastRead = 0;

File logfile;

/* --------------------------- */
void setup() {
  Serial.begin(9600);
  phSerial.begin(9600);
  delay(3000);

  /* SD card */
  if (!SD.begin(SD_CS)) {
    Serial.println("SD init failed"); while (1);
  }
  // CHANGE FILE NAME HERE FOR DIFFERENT FILE ex: "site1.csv"
  logfile = SD.open("datalog.csv", FILE_WRITE);
  if (!logfile) { Serial.println("Can't open datalog.csv"); while (1); }
  if (logfile.size() == 0) {                       // new file → header
    logfile.println("Date,Time,pH,tempC");
    logfile.flush();
  }

  /* RTC (optional) */
  if (rtc.begin() && !rtc.lostPower()) rtcPresent = true;

  /* put pH board in polling mode */
  flushPH();
  phSerial.print("C,0\r");
  delay(2000);
  flushPH();

  Serial.println("Logging started");
}

/* --------------------------- */
void loop() {
  if (millis() - lastRead < READ_INTERVAL_MS) return;
  lastRead = millis();

  /* temperature */
  int raw = analogRead(RTD_PIN);
  float volts = raw * (5.0 / 1023.0);
  float tempC = (volts - 1.058) / 0.009;

  /* pH */
  String pH = getPH();

  /* timestamp → “YYYY-MM-DD,HH:MM:SS” (or millis if no RTC) */
  char stamp[25];
  if (rtcPresent) {
    DateTime t = rtc.now();
    sprintf(stamp, "%04d-%02d-%02d,%02d:%02d:%02d",
            t.year(), t.month(), t.day(),
            t.hour(), t.minute(), t.second());
  } else {
    unsigned long s = millis() / 1000;
    sprintf(stamp, "0000-00-00,%lu", s);          // fallback
  }

  /* Serial echo */
  Serial.print("pH: "); Serial.print(pH);
  Serial.print(" | T: "); Serial.print(tempC, 2); Serial.println(" °C");

  /* write to SD in new format */
  logfile.print(stamp); logfile.print(',');
  logfile.print(pH);   logfile.print(',');
  logfile.println(tempC, 2);
  logfile.flush();
}

/* helpers */
void flushPH() { while (phSerial.available()) phSerial.read(); }

String getPH() {
  flushPH();
  phSerial.print("R\r");
  delay(1500);
  String resp = "";
  unsigned long t0 = millis();
  while (millis() - t0 < 2000) {
    if (phSerial.available()) {
      char c = phSerial.read();
      if (c == '\r' || c == '\n') break;
      resp += c;
    }
  }
  return resp.length() ? resp : "nan";
}
