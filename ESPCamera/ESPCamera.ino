#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>

// ================= AP MODE =================
const char* ap_ssid = "ESP32-CAM";
const char* ap_password = "12345678";

WebServer server(80);

// ================= PIN WAVESHARE =================
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     39

#define SIOD_GPIO_NUM     15
#define SIOC_GPIO_NUM     16

#define Y9_GPIO_NUM       14
#define Y8_GPIO_NUM       13
#define Y7_GPIO_NUM       12
#define Y6_GPIO_NUM       11
#define Y5_GPIO_NUM       10
#define Y4_GPIO_NUM       9
#define Y3_GPIO_NUM       8
#define Y2_GPIO_NUM       7

#define VSYNC_GPIO_NUM    42
#define HREF_GPIO_NUM     41
#define PCLK_GPIO_NUM     46
// ============================================


// ================= HTML =================
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32 Camera AP</title>
</head>
<body>
  <h2>ESP32-S3 Camera Testing</h2>
  <img src="/stream" style="width:100%;max-width:600px;" />
</body>
</html>
)rawliteral";


// ================= ROOT =================
void handleRoot() {
  server.send(200, "text/html", htmlPage);
}


// ================= STREAM =================
void handleStream() {
  WiFiClient client = server.client();

  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
  server.sendContent(response);

  while (client.connected()) {

    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) continue;

    server.sendContent("--frame\r\n");
    server.sendContent("Content-Type: image/jpeg\r\n\r\n");
    server.sendContent((const char*)fb->buf, fb->len);
    server.sendContent("\r\n");

    esp_camera_fb_return(fb);

    delay(10);
  }
}


// ================= CAMERA INIT =================
void startCamera() {

  camera_config_t config;

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;

  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn  = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 15;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK) {
    Serial.printf("Camera init FAILED: 0x%x\n", err);
    return;
  }

  Serial.println("Camera OK");
}


// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  Serial.println("Starting camera...");
  startCamera();

  // ================= AP MODE =================
  WiFi.softAP(ap_ssid, ap_password);

  Serial.println("AP Started");
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/stream", handleStream);

  server.begin();

  Serial.println("Server ready");
}


// ================= LOOP =================
void loop() {
  server.handleClient();
}