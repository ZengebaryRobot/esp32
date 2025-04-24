#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include "esp_camera.h"

// Camera Pins
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

#define LED_GPIO_NUM 4

// Serial2 Pins
#define RXD2 13
#define TXD2 12

const char *ssid = "Zengebary";
const char *password = "1234abcdABCD";

const char *serverEndpoint = "http://192.168.1.3:5000/process";
const char *slaveCamEndpoint = "http://192.168.1.100/capture";

WebServer server(80);
sensor_t *sensor = nullptr;

void changeConfig(String command);
void handleConfig();
void handleStream();
String getPythonData(String command);

void setup()
{
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  // Camera init config
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
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
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_VGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  if (psramFound())
  {
    config.jpeg_quality = 10;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
  }
  else
  {
    config.frame_size = FRAMESIZE_VGA;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  if (esp_camera_init(&config) != ESP_OK)
  {
    Serial.println("Camera init failed");
    return;
  }

  sensor = esp_camera_sensor_get();
  if (sensor->id.PID == OV3660_PID)
  {
    sensor->set_vflip(sensor, 1);
    sensor->set_brightness(sensor, 1);
    sensor->set_saturation(sensor, -2);
  }
  sensor->set_framesize(sensor, FRAMESIZE_VGA);

  // Connect to WiFi
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Endpoints
  server.on("/config", HTTP_GET, handleConfig);
  server.on("/stream", HTTP_GET, handleStream);
  server.begin();
  Serial.println("HTTP server started on port 80");

  Serial.println("Use '/stream' to access the stream.");
  Serial.println("Use '/config' to set config.");
}

void loop()
{
  server.handleClient();

  if (Serial2.available())
  {
    String line = Serial2.readStringUntil('\n');
    line.trim();
    if (line.length())
    {
      Serial.println("Received command: " + line);

      // <camId> <action>
      int sp = line.indexOf(' ');
      if (sp < 0)
      {
        Serial2.println("ERROR");
      }
      else
      {
        int camId = line.substring(0, sp).toInt();
        String action = line.substring(sp + 1);
        String serverReply = getPythonData(action, camId);
        Serial2.println(serverReply);
      }
    }

    while (Serial2.available())
      Serial2.read();
  }

  delay(10);
}

String latestCommand = "";
void changeConfig(String command)
{
  if (latestCommand == command)
    return;

  latestCommand = command;
  Serial.println("Changing config to: " + command);

  sensor_t *s = esp_camera_sensor_get();

  // Reset to default settings
  s->set_framesize(s, FRAMESIZE_VGA);
  s->set_quality(s, 12);
  s->set_saturation(s, 0);
  analogWrite(LED_GPIO_NUM, 0);

  if (command == "xo")
  {
    s->set_framesize(s, FRAMESIZE_VGA);
    s->set_quality(s, 9);
    s->set_saturation(s, 2);
    analogWrite(LED_GPIO_NUM, 200);
  }
  else if (command == "rubik")
  {
    //
  }
}

bool toBool(String value)
{
  value.toLowerCase();
  return (value == "1" || value == "true" || value == "on");
}

void handleConfig()
{
  sensor_t *s = esp_camera_sensor_get();

  if (server.hasArg("framesize"))
  {
    String value = server.arg("framesize");
    if (value == "QVGA")
      s->set_framesize(s, FRAMESIZE_QVGA);
    else if (value == "SVGA")
      s->set_framesize(s, FRAMESIZE_SVGA);
    else if (value == "VGA")
      s->set_framesize(s, FRAMESIZE_VGA);
  }

  if (server.hasArg("quality"))
    s->set_quality(s, server.arg("quality").toInt());
  if (server.hasArg("contrast"))
    s->set_contrast(s, server.arg("contrast").toInt());
  if (server.hasArg("brightness"))
    s->set_brightness(s, server.arg("brightness").toInt());
  if (server.hasArg("saturation"))
    s->set_saturation(s, server.arg("saturation").toInt());
  if (server.hasArg("gainceiling"))
    s->set_gainceiling(s, (gainceiling_t)server.arg("gainceiling").toInt());

  if (server.hasArg("colorbar"))
    s->set_colorbar(s, toBool(server.arg("colorbar")));
  if (server.hasArg("awb"))
    s->set_whitebal(s, toBool(server.arg("awb")));
  if (server.hasArg("agc"))
    s->set_gain_ctrl(s, toBool(server.arg("agc")));
  if (server.hasArg("aec"))
    s->set_exposure_ctrl(s, toBool(server.arg("aec")));
  if (server.hasArg("hmirror"))
    s->set_hmirror(s, toBool(server.arg("hmirror")));
  if (server.hasArg("vflip"))
    s->set_vflip(s, toBool(server.arg("vflip")));

  if (server.hasArg("awb_gain"))
    s->set_awb_gain(s, server.arg("awb_gain").toInt());
  if (server.hasArg("agc_gain"))
    s->set_agc_gain(s, server.arg("agc_gain").toInt());
  if (server.hasArg("aec_value"))
    s->set_aec_value(s, server.arg("aec_value").toInt());

  if (server.hasArg("aec2"))
    s->set_aec2(s, toBool(server.arg("aec2")));
  if (server.hasArg("dcw"))
    s->set_dcw(s, toBool(server.arg("dcw")));
  if (server.hasArg("bpc"))
    s->set_bpc(s, toBool(server.arg("bpc")));
  if (server.hasArg("wpc"))
    s->set_wpc(s, toBool(server.arg("wpc")));
  if (server.hasArg("raw_gma"))
    s->set_raw_gma(s, toBool(server.arg("raw_gma")));
  if (server.hasArg("lenc"))
    s->set_lenc(s, toBool(server.arg("lenc")));

  if (server.hasArg("ae_level"))
    s->set_ae_level(s, server.arg("ae_level").toInt());

  if (server.hasArg("led_intensity"))
  {
    int intensity = server.arg("led_intensity").toInt();
    analogWrite(LED_GPIO_NUM, intensity);
  }

  server.send(200, "text/plain", "Camera settings updated!");
}

void handleStream()
{
  WiFiClient client = server.client();
  String header =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
  client.print(header);

  while (client.connected())
  {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
      Serial.println("Camera capture failed");
      break;
    }
    client.printf("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);
    client.write(fb->buf, fb->len);
    client.print("\r\n");
    esp_camera_fb_return(fb);
    if (!client.connected())
      break;
    delay(30);
  }
}

String getPythonData(String command, int camId)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi not connected.");
    return "ERROR";
  }

  camera_fb_t *fb = nullptr;

  if (camId == 0)
  {
    changeConfig(command);

    fb = esp_camera_fb_get();
    if (!fb)
    {
      Serial.println("Camera capture failed");
      return "ERROR";
    }
  }
  else
  {
    HTTPClient httpGet;
    httpGet.begin(slaveCamEndpoint);

    int code = httpGet.GET();

    if (code != 200)
    {
      Serial.println("Failed to get image from slave camera. HTTP code: " + String(code) + " - " + httpGet.errorToString(code));
      httpGet.end();
      return "ERROR";
    }

    WiFiClient *stream = httpGet.getStreamPtr();
    size_t len = httpGet.getSize();

    fb = (camera_fb_t *)malloc(sizeof(camera_fb_t));
    fb->len = len;
    fb->buf = (uint8_t *)malloc(len);

    size_t idx = 0;
    while (stream->available() && idx < len)
    {
      fb->buf[idx++] = stream->read();
    }

    httpGet.end();
  }

  HTTPClient http;
  http.setTimeout(5000);
  String fullUrl = String(serverEndpoint) + "?action=" + command;
  http.begin(fullUrl);
  http.addHeader("Content-Type", "image/jpeg");

  int httpResponseCode = http.POST(fb->buf, fb->len);
  String response = "";

  if (httpResponseCode > 0)
  {
    response = http.getString();
    if (response == "error")
    {
      Serial.println("POST failed. HTTP error code: " + String(httpResponseCode) + " - " + http.errorToString(httpResponseCode) + " - " + response);
      response = "ERROR";
    }
    else
    {
      Serial.println("Server response: " + response);
    }
  }
  else
  {
    Serial.println("POST failed. HTTP error code: " + String(httpResponseCode) + " - " + http.errorToString(httpResponseCode));
    response = "ERROR";
  }

  http.end();

  if (camId == 0)
  {
    esp_camera_fb_return(fb);
  }
  else
  {
    free(fb->buf);
    free(fb);
  }

  return response;
}