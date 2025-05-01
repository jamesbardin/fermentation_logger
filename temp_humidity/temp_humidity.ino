/* ------------------------------------------------------------
   DHT22  + RTC  + SD  data-logger   (failsafe build)
   – watchdog auto-reboot on lock-up
   – 3× sensor-read retry with timeout
   – buffered writes (flush every LINES_PER_FLUSH records)
   – auto-reopen log on write error
   – no dynamic String allocation
   ------------------------------------------------------------ */

   #include <SPI.h>
   #include <SD.h>
   #include <Wire.h>
   #include "RTClib.h"
   #include <DHT.h>
   #include <avr/wdt.h>
   
   /* ---------- USER SETTINGS ---------------------------------- */
   #define DHTPIN        2
   #define DHTTYPE       DHT22
   #define SD_CS_PIN     10
   #define LOG_FILENAME  "LOG00.CSV"   // 8.3 DOS-style name
   const unsigned long LOG_INTERVAL_MS = 1800000UL;   // 30 min
   const uint8_t  SENSOR_RETRIES       = 3;
   const uint16_t LINES_PER_FLUSH      = 4;           // flush 2-hour chunks
   /* ----------------------------------------------------------- */
   
   DHT        dht(DHTPIN, DHTTYPE);
   RTC_DS1307 rtc;
   File       logfile;
   
   unsigned long lastLog  = 0UL;
   uint16_t      lineCnt  = 0;
   
   /* ============= helper prototypes =========================== */
   bool readDHT(float& tC, float& h);
   bool reopenLogIfNeeded();
   
   /* =========================================================== */
   void setup() {
     wdt_disable();                                  // if WDT caused reset
     Serial.begin(9600);
     dht.begin();
   
     /* ---------- SD card ---------- */
     if (!SD.begin(SD_CS_PIN)) { Serial.println(F("SD init failed")); while (1); }
   
     bool newFile = !SD.exists(LOG_FILENAME);
     logfile = SD.open(LOG_FILENAME, FILE_WRITE);
     if (!logfile) { Serial.println(F("File open failed")); while (1); }
   
     if (newFile) {                                  // write header once
       logfile.println(F("Date,Time,TempC,TempF,Humidity"));
       logfile.flush();
     }
   
     /* ---------- RTC ---------- */
     if (!rtc.begin())  Serial.println(F("RTC not found"));
     if (!rtc.isrunning()) {
       rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));  // set compile-time
       Serial.println(F("RTC set to compile-time"));
     }
   
     Serial.println(F("Logging started"));
     wdt_enable(WDTO_8S);                            // watchdog 8-second window
   }
   
   /* =========================================================== */
   void loop() {
     wdt_reset();
   
     if (millis() - lastLog < LOG_INTERVAL_MS) return;
     lastLog = millis();
   
     /* ---------- sensor read with retries ---------- */
     float tC, hum;
     if (!readDHT(tC, hum)) {
       Serial.println(F("DHT failed x3 – skipping sample"));
       return;                                       // keep running, try next slot
     }
     float tF = tC * 9.0f / 5.0f + 32.0f;
   
     /* ---------- timestamp ---------- */
     char stamp[20];
     if (rtc.isrunning()) {
       DateTime t = rtc.now();
       sprintf(stamp, "%04d-%02d-%02d,%02d:%02d:%02d",
               t.year(), t.month(), t.day(),
               t.hour(), t.minute(), t.second());
     } else {                                       // fallback to millis
       uint64_t s = millis() / 1000ULL;
       sprintf(stamp, "0000-00-00,%llu", (unsigned long long)s);
     }
   
     /* ---------- CSV write ---------- */
     logfile.print(stamp); logfile.print(',');
     logfile.print(tC, 1);  logfile.print(',');
     logfile.print(tF, 1);  logfile.print(',');
     logfile.println(hum, 1);
   
     /* ---------- buffered flush ---------- */
     if (++lineCnt >= LINES_PER_FLUSH) {
       if (!logfile.flush()) {                      // flush failed? try reopen
         Serial.println(F("Flush failed – reopening log"));
         if (!reopenLogIfNeeded())  wdt_reset();    // force reboot if reopen fails
       }
       lineCnt = 0;
     }
   
     Serial.print(F("Logged ")); Serial.println(stamp);
   }
   
   /* -------------------- helpers ------------------------------ */
   bool readDHT(float& tC, float& h) {
     for (uint8_t i = 0; i < SENSOR_RETRIES; ++i) {
       h  = dht.readHumidity();
       tC = dht.readTemperature();
       if (!isnan(h) && !isnan(tC)) return true;
       delay(2000);                                // wait before retry
     }
     return false;
   }
   
   /* attempt to close and reopen the log file ------------------ */
   bool reopenLogIfNeeded() {
     logfile.close();
     logfile = SD.open(LOG_FILENAME, FILE_WRITE);
     if (!logfile) return false;
     logfile.seek(logfile.size());                 // append mode
     return true;
   }
   