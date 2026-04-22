#include <Arduino.h>

// =============== UART A7670E ==================
#define RX_GSM 17
#define TX_GSM 18
HardwareSerial sim(1);

// =============== AT =====================
void sendAT(String cmd, uint32_t timeout = 3000) {
  Serial.println(">> " + cmd);
  sim.println(cmd);

  uint32_t t = millis();
  while (millis() - t < timeout) {
    while (sim.available()) {
      Serial.write(sim.read());
    }
  }
  Serial.println();
}

// =============== SETUP ========================
void setup() {
  Serial.begin(115200);
  delay(1000);

  sim.begin(115200, SERIAL_8N1, RX_GSM, TX_GSM);
  delay(3000);

  Serial.println("=== A7670E RESET ===");

  // Test AT
  sendAT("AT");

  sendAT("AT+CFUN=0");
  delay(2000);

  // Reset modem
  sendAT("AT+CFUN=1,1");

  Serial.println("=== Reboot ===");
  delay(15000);

  sendAT("AT");
  sendAT("ATE0");

  // SIM CHECK
  sendAT("AT+CPIN?");
  sendAT("AT+CSMINS?");

  // NET CHECK
  sendAT("AT+CSQ");
  sendAT("AT+CREG?");
  sendAT("AT+COPS?");

  // ATTACH CHECK
  sendAT("AT+CGATT?");
}

// =============== LOOP =========================
void loop() {
  sendAT("AT+CREG?", 1500);
  sendAT("AT+CGATT?", 1500);
  delay(5000);
}