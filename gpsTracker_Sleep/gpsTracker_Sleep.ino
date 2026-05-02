#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <FS.h>
#include <SD_MMC.h>
#include "esp_sleep.h"

#define RX_GSM 17
#define TX_GSM 18
#define LED_PIN 38
#define LED_COUNT 1

#define MAX17048_ADDR 0x36

//========== PIN SD CARD ============
const int SDMMC_CLK = 5;
const int SDMMC_CMD = 4;
const int SDMMC_DATA = 6;
const int SD_CD_PIN = 46;

HardwareSerial sim(1);
Adafruit_NeoPixel led(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

//================ DEVICE =================
const char* ID_ALAT = "DMG-0001";
const char* YTRACK_URL = "http://ytrack.dumeg.com/api/v1/submit-gps";
const char* AUTH_TOKEN = "1277-dmg-gps-tracking-4424";

//================ THINGSBOARD =================
const char* TB_URL = "http://103.147.91.40:8080/api/v1/IasB57XqeswS99y3cMke/telemetry";

//================ TIMER ==================
unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 60000;

//============= SLEEP =================
void goToSleep(unsigned long durationMs) {

  Serial.println("ESP SLEEP");

  uint64_t sleepTime = (uint64_t)durationMs * 1000;

  esp_sleep_enable_timer_wakeup(sleepTime);

  delay(1000); 

  esp_deep_sleep_start();
}
//================ BATTERY =================
void quickStart() {
  Wire.beginTransmission(MAX17048_ADDR);
  Wire.write(0x06);
  Wire.write(0x40);
  Wire.write(0x00);
  Wire.endTransmission();
}
//================ READ VOLTAGE ===============
float readVoltageAvg(int samples = 10) {

  float total = 0;
  int valid = 0;

  for (int i = 0; i < samples; i++) {

    Wire.beginTransmission(MAX17048_ADDR);
    Wire.write(0x02);
    Wire.endTransmission(false);

    Wire.requestFrom(MAX17048_ADDR, 2);

    if (Wire.available() >= 2) {
      uint16_t raw = (Wire.read() << 8) | Wire.read();
      float v = raw * 0.000078125;

      total += v;
      valid++;
    }

    delay(20);
  }

  if (valid == 0) return -1;

  return total / valid;
}
//============== INTERVAL BATT ============
unsigned long getSendInterval(float battery) {

  if (battery >= 75) {
    return 60000;  // 1 minute
  } else if (battery >= 50) {
    return 300000;  // 5 minute
  } else {
    return 600000;  // 10 minute
  }
}
//================ SOC LINEAR =================
float estimateSOC(float voltage) {

  float VMAX = 4.0;
  float VMIN = 3.2;

  float soc = (voltage - VMIN) * 100.0 / (VMAX - VMIN);

  if (soc > 100) soc = 100;
  if (soc < 0) soc = 0;

  return soc;
}
//============= STABLE VOLTAGE ============
float smoothVoltage(float newV) {
  static float lastV = 3.7;

  float alpha = 0.2;
  lastV = (alpha * newV) + ((1 - alpha) * lastV);

  return lastV;
}
//================ UTIL ===================
String convertLatLon(String value, String dir) {
  double v = value.toFloat();
  if (dir == "S" || dir == "W") v = -v;

  char buf[20];
  dtostrf(v, 0, 7, buf);
  return String(buf);
}
//================ DATETIME ================
String formatDate(String yymmdd) {
  return yymmdd.substring(4, 6) + "/" + yymmdd.substring(2, 4) + "/" + yymmdd.substring(0, 2);
}

String formatTimeGMT8(String hhmmss) {
  int hh = hhmmss.substring(0, 2).toInt() + 8;
  if (hh >= 24) hh -= 24;

  char buf[10];
  sprintf(buf, "%02d:%02d:%02d",
          hh,
          hhmmss.substring(2, 4).toInt(),
          hhmmss.substring(4, 6).toInt());

  return String(buf);
}

//================ LED ====================
void blink(uint8_t r, uint8_t g, uint8_t b) {

  for (int i = 0; i < 3; i++) {

    led.setPixelColor(0, led.Color(r, g, b));
    led.show();

    delay(150);

    led.clear();
    led.show();

    delay(150);
  }
}

void ledSuccess() {
  blink(255, 0, 0);
}
void ledFail() {
  blink(0, 255, 0);
}

//================ AT =====================
void sendAT(String cmd, int timeout = 3000) {

  Serial.println(">> " + cmd);
  sim.println(cmd);

  unsigned long start = millis();

  while (millis() - start < timeout) {
    while (sim.available()) {
      Serial.write(sim.read());
    }
  }
}

//================ INTERNET ===============
void initInternet() {

  sendAT("AT+CGATT=1");
  sendAT("AT+CGDCONT=1,\"IP\",\"internet\"");
  sendAT("AT+NETOPEN", 8000);
  sendAT("AT+IPADDR");
}

//================ HTTP ===================
bool httpPost(String payload) {

  sendAT("AT+HTTPTERM", 500);
  sendAT("AT+HTTPINIT");
  sendAT("AT+HTTPSSL=0");

  sendAT(String("AT+HTTPPARA=\"URL\",\"") + YTRACK_URL + "\"");
  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"");
  sendAT(String("AT+HTTPPARA=\"USERDATA\",\"Authorization: ") + AUTH_TOKEN + "\"");

  sendAT("AT+HTTPDATA=" + String(payload.length()) + ",10000");

  sim.print(payload);

  unsigned long waitPayload = millis();
  while (millis() - waitPayload < 2000) {
    while (sim.available()) Serial.write(sim.read());
  }

  sim.println("AT+HTTPACTION=1");

  unsigned long start = millis();
  bool success = false;

  while (millis() - start < 10000) {

    if (sim.available()) {

      String resp = sim.readStringUntil('\n');
      Serial.println(resp);

      if (resp.indexOf("+HTTPACTION:") >= 0) {

        if (resp.indexOf(",200,") >= 0 || resp.indexOf(",201,") >= 0) success = true;
        break;
      }
    }
  }

  sendAT("AT+HTTPREAD", 5000);
  sendAT("AT+HTTPTERM");

  return success;
}
//============ THINGSBOARD =============
bool sendThingsboard(String payload) {

  sendAT("AT+HTTPTERM", 500);
  sendAT("AT+HTTPINIT");
  sendAT("AT+HTTPSSL=1");

  sendAT(String("AT+HTTPPARA=\"URL\",\"") + TB_URL + "\"");
  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"");

  int len = payload.length();

  Serial.print("TB LEN: ");
  Serial.println(len);

  sim.println("AT+HTTPDATA=" + String(len) + ",10000");

  if (!sim.find("DOWNLOAD")) {
    Serial.println("MODEM NOT READY");
    return false;
  }

  sim.print(payload);

  Serial.println("TB PAYLOAD:");
  Serial.println(payload);

  delay(500);

  sim.println("AT+HTTPACTION=1");

  bool success = false;
  unsigned long start = millis();

  while (millis() - start < 10000) {

    if (sim.available()) {

      String resp = sim.readStringUntil('\n');
      Serial.println(resp);

      if (resp.indexOf("+HTTPACTION:") >= 0) {

        if (resp.indexOf(",200,") >= 0 || resp.indexOf(",204,") >= 0) {
          success = true;
        }

        break;
      }
    }
  }

  sendAT("AT+HTTPREAD", 3000);
  sendAT("AT+HTTPTERM");

  return success;
}
//================ SEND DATA ==============
float sendYtrack(String lat, String lon, String alt, int speed, int sat, String date, String time) {

  int rssi = getRSSI();

  float voltage = readVoltageAvg(10);

  if (voltage < 0) {
    Serial.println("BATTERY READ FAIL");
    voltage = 3.7;
  }
  voltage = smoothVoltage(voltage);

  float battery = estimateSOC(voltage);

  String rawDate = date;

  String datetime =
    "20" + rawDate.substring(4, 6) + "-" +  // YYYY
    rawDate.substring(2, 4) + "-" +         // MM
    rawDate.substring(0, 2) + " " +         // DD
    time;

  String payload =
    "{"
    "\"id_alat\":\""
    + String(ID_ALAT) + "\","
                        "\"lat\":\""
    + lat + "\","
            "\"long\":\""
    + lon + "\","
            "\"alt\":\""
    + alt + "\","
            "\"sat\":\""
    + String(sat) + "\","
                    "\"speed\":\""
    + String(speed) + "\","
                      "\"battery\":\""
    + String((int)battery) + "\","
                             "\"voltage\":\""
    + String(voltage, 3) + "\","
                           "\"rssi\":\""
    + String(rssi) + "\","
                     "\"datetime\":\""
    + datetime + "\""
                 "}";

  Serial.println("\n===== YTRACK PAYLOAD =====");
  Serial.println(payload);
  Serial.println("==========================");

  if (networkAvailable()) {

    Serial.println("NETWORK OK");

    sendBufferSD();

    //========= KIRIM YTRACK =========
    if (httpPost(payload)) {
      Serial.println("YTRACK SUCCESS");
      ledSuccess();
    } else {
      Serial.println("YTRACK FAIL");
      ledFail();
      saveToSD(payload);
    }

    //========= KIRIM THINGSBOARD =========
    Serial.println("SEND TO THINGSBOARD");

    String tbPayload =
      "{"
      "\"lat\":"
      + lat + ","
              "\"lon\":"
      + lon + ","
              "\"alt\":"
      + alt + ","
              "\"speed\":"
      + String(speed) + ","
                        "\"sat\":"
      + String(sat) + ","
                      "\"battery\":"
      + String((int)battery) + ","
                               "\"voltage\":"
      + String(voltage, 3) + ","
                             "\"rssi\":"
      + String(rssi) + ","
                       "\"datetime\":\""
      + datetime + "\""
                   "}";

    if (sendThingsboard(tbPayload)) {
      Serial.println("THINGSBOARD OK");
    } else {
      Serial.println("THINGSBOARD FAIL");
    }

  } else {

    Serial.println("NO NETWORK - SAVE TO SD");

    saveToSD(payload);
  }
  return battery;
}
//================ SD CARD =================
bool initSD() {

  if (!SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA)) {
    Serial.println("SD PIN FAILED");
    return false;
  }

  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("SD INIT FAILED");
    return false;
  }

  Serial.println("SD READY");
  return true;
}
//============ SAVE GPS =================
void saveToSD(String data) {

  File file = SD_MMC.open("/gps.txt", FILE_APPEND);

  if (!file) {
    Serial.println("SD WRITE FAILED");
    return;
  }

  file.println(data);
  file.close();

  Serial.println("GPS SAVED TO SD");
}
//============= SEND BUFFER ============
void sendBufferSD() {

  File file = SD_MMC.open("/gps.txt");

  if (!file) {
    Serial.println("NO BUFFER FILE");
    return;
  }

  Serial.println("SENDING BUFFER DATA");

  while (file.available()) {

    String line = file.readStringUntil('\n');
    line.trim();

    if (line.length() == 0) continue;

    Serial.println("SEND BUFFER:");
    Serial.println(line);

    // SEND TO YTRACK
    if (!httpPost(line)) {
      Serial.println("YTRACK BUFFER FAIL - STOP");
      file.close();
      return;
    }

    // SEND TO THINGSBOARD
    String tbPayload = line;

    tbPayload.replace("\"lat\":\"", "\"lat\":");
    tbPayload.replace("\"long\":\"", "\"lon\":");
    tbPayload.replace("\"alt\":\"", "\"alt\":");
    tbPayload.replace("\"sat\":\"", "\"sat\":");
    tbPayload.replace("\"speed\":\"", "\"speed\":");
    tbPayload.replace("\"battery\":\"", "\"battery\":");
    tbPayload.replace("\"voltage\":\"", "\"voltage\":");

    tbPayload.replace("\",", ",");
    tbPayload.replace("\"}", "}");

    sendThingsboard(tbPayload);
  }

  file.close();

  Serial.println("BUFFER SENT SUCCESS");

  SD_MMC.remove("/gps.txt");
}
//================ INTERNET CHECK ==================
bool networkAvailable() {
  sim.println("AT+CREG?");

  String resp = "";
  unsigned long start = millis();

  while (millis() - start < 2000) {
    while (sim.available()) {
      resp += char(sim.read());
    }
  }

  Serial.println(resp);

  int stat = -1;
  int pos = resp.indexOf("+CREG:");

  if (pos != -1) {
    stat = resp.substring(pos + 9, pos + 10).toInt();
  }

  sim.println("AT+CSQ");

  resp = "";
  start = millis();

  while (millis() - start < 2000) {
    while (sim.available()) {
      resp += char(sim.read());
    }
  }

  Serial.println(resp);

  int rssi = 99;

  pos = resp.indexOf("+CSQ:");

  if (pos != -1) {
    rssi = resp.substring(pos + 6, resp.indexOf(",", pos)).toInt();
  }

  Serial.print("CREG: ");
  Serial.print(stat);
  Serial.print(" | CSQ: ");
  Serial.println(rssi);

  if (!(stat >= 1 && stat <= 6) && rssi == 99) {

    Serial.println("NO NETWORK");

    return false;
  }

  Serial.println("NETWORK AVAILABLE");

  return true;
}
//=============== RSSI =================
int getRSSI() {

  sim.println("AT+CSQ");

  String resp = "";
  unsigned long start = millis();

  while (millis() - start < 2000) {
    while (sim.available()) {
      resp += char(sim.read());
    }
  }

  Serial.println("RAW CSQ: " + resp);

  int rssi = 99;
  int pos = resp.indexOf("+CSQ:");

  if (pos != -1) {
    rssi = resp.substring(pos + 6, resp.indexOf(",", pos)).toInt();
  }

  if (rssi == 99) return -113;

  int dBm = -113 + (2 * rssi);

  Serial.print("RSSI: ");
  Serial.print(rssi);
  Serial.print(" -> ");
  Serial.print(dBm);
  Serial.println(" dBm");

  return dBm;
}
//================ GPS CHECK ==================
bool gpsValid(String lat, String lon) {
  if (lat.length() == 0 || lon.length() == 0) return false;

  if (lat == "0" || lat == "0.0" || lat == "0.0000000") return false;
  if (lon == "0" || lon == "0.0" || lon == "0.0000000") return false;

  if (lat.toFloat() == 0.0 && lon.toFloat() == 0.0) return false;

  return true;
}
//================ SETUP ==================
void setup() {

  Serial.begin(115200);
  sim.begin(115200, SERIAL_8N1, RX_GSM, TX_GSM);

  Wire.begin(15, 16);
  quickStart();

  led.begin();
  led.clear();
  led.show();

  initSD();

  delay(2000);

  Serial.println("GNSS START");

  sendAT("AT+CGNSSPWR=1", 5000);

  initInternet();
}

//================ LOOP ===================
void loop() {

  sim.println("AT+CGNSSINFO");

  String line;
  unsigned long t = millis();

  while (millis() - t < 2000) {
    if (sim.available()) {
      line = sim.readStringUntil('\n');
      if (line.startsWith("+CGNSSINFO:")) break;
    }
  }

  if (!line.startsWith("+CGNSSINFO:")) {
    Serial.println("NO GPS DATA");
    return;
  }

  line = line.substring(12);

  String p[20];
  int c = 0, last = 0;

  for (int i = 0; i < line.length(); i++) {
    if (line[i] == ',') {
      p[c++] = line.substring(last, i);
      last = i + 1;
    }
  }
  p[c++] = line.substring(last);

  if (p[1] == "0") {
    Serial.println("WAITING GPS");
    return;
  }

  String lat = convertLatLon(p[5], p[6]);
  String lon = convertLatLon(p[7], p[8]);
  String dateDisplay = formatDate(p[9]);
  String dateRaw = p[9];
  String time = formatTimeGMT8(p[10]);
  String alt = p[14];

  int sat = p[16].toInt();
  int speed = p[12].toFloat() + 0.5;

  Serial.println("\n===== GPS DATA =====");

  Serial.println("LAT  : " + lat);
  Serial.println("LON  : " + lon);
  Serial.println("ALT  : " + alt);
  Serial.println("SPD  : " + String(speed));
  Serial.println("SAT  : " + String(sat));
  Serial.println("DATE : " + dateDisplay);
  Serial.println("TIME : " + time);

  Serial.println("====================");

  if (!gpsValid(lat, lon)) {
    return;
  }

  static float lastBattery = 100;

  unsigned long interval = getSendInterval(lastBattery);

  if (millis() - lastSend >= interval) {

    lastSend = millis();

    lastBattery = sendYtrack(lat, lon, alt, speed, sat, dateRaw, time);

    Serial.print("INTERVAL: ");
    Serial.println(interval);

    goToSleep(interval);
  }
}