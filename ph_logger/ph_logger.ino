/* ------------------------------------------------------------
   Field logger - pH + RTD → CSV on SD
   – watchdog auto-reboot on lock-up
   – no dynamic String allocation
   – buffered writes (flush once/min ≈ 12 lines @ 5 s)
   – daily log-file rollover “YYYYMMDD.csv”
   ------------------------------------------------------------ */

   #include <SoftwareSerial.h>
   #include <SD.h>
   #include <SPI.h>
   #include <RTClib.h>
   #include <avr/wdt.h>
   
   /* ---------- pin map ---------- */
   #define PH_RX    3
   #define PH_TX    2
   #define RTD_PIN  A0
   #define SD_CS    10
   
   /* ---------- sampling interval ---------- */
   const unsigned long READ_INTERVAL_MS = 5000UL;     // 5 s
   const uint16_t      LINES_PER_FLUSH   = 12;        // ≈ 1 min between flushes
   
   /* ---------- globals ---------- */
   SoftwareSerial phSerial(PH_RX, PH_TX);
   RTC_DS3231 rtc;
   bool         rtcPresent = false;
   
   File   logfile;
   char   curDate[9] = "";           // “YYYYMMDD” + NUL
   uint16_t lineCnt  = 0;
   unsigned long lastRead = 0UL;
   
   /* ============================================================ */
   void setup() {
     wdt_disable();                  // in case WDT caused reset
     Serial.begin(9600);
     phSerial.begin(9600);
     delay(3000);                    // allow sensors to power-up
   
     /* SD-card --------------------------------------------------- */
     if (!SD.begin(SD_CS)) { Serial.println(F("SD init failed")); while (1); }
   
     /* RTC ------------------------------------------------------- */
     if (rtc.begin() && !rtc.lostPower()) rtcPresent = true;
   
     openNewLogFile();               // sets curDate[]
   
     /* pH board → polling mode ---------------------------------- */
     flushPH();
     phSerial.print("C,0\r");
     delay(2000);
     flushPH();
   
     Serial.println(F("Logging started"));
     wdt_enable(WDTO_8S);            // watchdog 8-second window
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
     char stamp[20];                 // "YYYY-MM-DD,HH:MM:SS"
     if (rtcPresent) {
       DateTime t = rtc.now();
       sprintf(stamp, "%04d-%02d-%02d,%02d:%02d:%02d",
               t.year(), t.month(), t.day(),
               t.hour(), t.minute(), t.second());
   
       /* roll file at midnight ----------------------------- */
       char ymd[9];
       sprintf(ymd, "%04d%02d%02d", t.year(), t.month(), t.day());
       if (strcmp(ymd, curDate) != 0) openNewLogFile();
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
   
     /* buffered flush / reopen every minute ------------- */
     if (++lineCnt >= LINES_PER_FLUSH) {
       logfile.flush();              // write data + update FAT
       lineCnt = 0;
     }
   }
   
   /* ============================================================ */
   /* ----- helpers ------------------------------------------------*/
   void flushPH() { while (phSerial.available()) phSerial.read(); }
   
   /* blocking pH read with 2 s timeout, result as char* */
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
   
   /* open today’s file, write header if new -------------- */
   void openNewLogFile() {
     if (logfile) logfile.close();
   
     char fname[16];                 // “YYYYMMDD.csv”
     if (rtcPresent) {
       DateTime t = rtc.now();
       // YOU CAN CHANGE THE FILE NAME HERE FOR EXAMPLE: 
       // sprintf(fname, "site1_%04d%02d%02d.csv", t.year(), t.month(), t.day()); -> file will be named site1_20250322
       sprintf(fname, "%04d%02d%02d.csv", t.year(), t.month(), t.day());
       sprintf(curDate, "%04d%02d%02d", t.year(), t.month(), t.day());
     } else {
       strcpy(fname, "datalog.csv");
       strcpy(curDate, "00000000");
     }
   
     logfile = SD.open(fname, FILE_WRITE);
     if (!logfile) { Serial.println(F("Can't open log file")); while (1); }
     if (logfile.size() == 0) {
       logfile.println(F("Date,Time,pH,tempC"));
       logfile.flush();
     }
   }
   