/* ------------------------------------------------------------
   Field logger – pH + RTD → CSV on SD  (single-file version)
   – watchdog auto-reboot on lock-up
   – no dynamic String allocation
   – buffered writes (flush once/min ≈ 12 lines @ 5 s)
   ------------------------------------------------------------ */

   #include <SoftwareSerial.h>
   #include <SD.h>
   #include <SPI.h>
   #include <RTClib.h>
   #include <avr/wdt.h>
   
   /* ---------- user settings ---------- */
   #define LOG_FILENAME  "site1.csv"    // <---  EDIT THIS
   
   /* ---------- pin map ---------- */
   #define PH_RX   3
   #define PH_TX   2
   #define RTD_PIN A0
   #define SD_CS   10
   
   /* ---------- sampling interval ---------- */
   const unsigned long READ_INTERVAL_MS = 5000UL;   // 5 s    <- CHANGE THE TIMINGS HERE (in milliseconds)
   const uint16_t      LINES_PER_FLUSH  = 12;       // ≈ 1 min between flushes
   
   /* ---------- globals ---------- */
   SoftwareSerial phSerial(PH_RX, PH_TX);
   RTC_DS3231 rtc;
   bool        rtcPresent = false;
   
   File        logfile;
   uint16_t    lineCnt   = 0;
   unsigned long lastRead = 0UL;
   
   /* ============================================================ */
   void setup() {
     wdt_disable();                    // if WDT caused reset
     Serial.begin(9600);
     phSerial.begin(9600);
     delay(3000);                      // allow sensors to power-up
   
     /* SD-card --------------------------------------------------- */
     if (!SD.begin(SD_CS)) { Serial.println(F("SD init failed")); while (1); }
   
     /* RTC ------------------------------------------------------- */
     if (rtc.begin() && !rtc.lostPower()) rtcPresent = true;
   
     /* open / create fixed log file ----------------------------- */
     logfile = SD.open(LOG_FILENAME, FILE_WRITE);
     if (!logfile) { Serial.println(F("Can't open log file")); while (1); }
     if (logfile.size() == 0) {                      // brand-new → header
       logfile.println(F("Date,Time,pH,tempC"));
       logfile.flush();
     }
   
     /* pH board → polling mode ---------------------------------- */
     flushPH();
     phSerial.print("C,0\r");
     delay(2000);
     flushPH();
   
     Serial.println(F("Logging started"));
     wdt_enable(WDTO_8S);              // watchdog 8-second window
   }
   
   /* ============================================================ */
   void loop() {
     wdt_reset();
   
     if (millis() - lastRead < READ_INTERVAL_MS) return;
     lastRead = millis();
   
     /* ---------- read RTD voltage ---------- */
     int   raw   = analogRead(RTD_PIN);
     float volts = raw * (5.0f / 1023.0f);
     float tempC = (volts - 1.058f) / 0.009f;
   
     /* ---------- read pH (blocking w/ timeout) ---------- */
     char pHbuf[8];
     if (!readPH(pHbuf, sizeof pHbuf)) strcpy(pHbuf, "nan");
   
     /* ---------- timestamp ---------- */
     char stamp[20];                   // "YYYY-MM-DD,HH:MM:SS"
     if (rtcPresent) {
       DateTime t = rtc.now();
       sprintf(stamp, "%04d-%02d-%02d,%02d:%02d:%02d",
               t.year(), t.month(), t.day(),
               t.hour(), t.minute(), t.second());
     } else {
       uint64_t s = millis() / 1000ULL;
       sprintf(stamp, "0000-00-00,%llu", (unsigned long long)s);
     }
   
     /* ---------- console echo ---------- */
     Serial.print(F("pH: ")); Serial.print(pHbuf);
     Serial.print(F(" | T: ")); Serial.print(tempC, 2); Serial.println(F(" °C"));
   
     /* ---------- CSV write ---------- */
     logfile.print(stamp); logfile.print(',');
     logfile.print(pHbuf); logfile.print(',');
     logfile.println(tempC, 2);
   
     /* buffered flush once/minute ------------------------------- */
     if (++lineCnt >= LINES_PER_FLUSH) {
       logfile.flush();                // write data + update FAT
       lineCnt = 0;
     }
   }
   
   /* ============================================================ */
   /* ----- helpers ---------------------------------------------- */
   void flushPH() { while (phSerial.available()) phSerial.read(); }
   
   /* blocking pH read with 2 s timeout, result into char* -------- */
   bool readPH(char *buf, size_t len) {
     flushPH();
     phSerial.print("R\r");
     unsigned long t0 = millis();
     size_t idx = 0;
     while (millis() - t0 < 2000UL && idx < len - 1) {
       if (phSerial.available()) {
         char c = phSerial.read();
         if (c == '\r' || c == '\n') break;
         buf[idx++] = c;
       }
     }
     buf[idx] = '\0';
     return idx > 0;
   }
   