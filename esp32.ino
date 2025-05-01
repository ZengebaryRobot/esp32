#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include "esp_camera.h"

// Include game files
#include "xo_game.h"
#include "rubik_game.h"
#include "memory_game.h"
#include "cups_game.h"

// Enable ESP32 server
#define ENABLE_ESP32_SERVER 1
#define ENABLE_SERVER_STREAMING 1
#define ENABLE_SERVER_CONFIG 1
#define ENABLE_SERVER_GAME_CHANGE 1

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

// Serial2 Pins (comm with Arduino)
#define RXD2 13
#define TXD2 12
#define STEPPER_COUNT 10
#define TIMEOUT_MS_SERVO 5000
#define TIMEOUT_MS_STEPPER 5000

// Game management
enum GameType
{
  GAME_NONE = -1,
  GAME_XO = 0,
  GAME_RUBIK = 1,
  GAME_MEMORY = 2,
  GAME_CUPS = 3,
  GAME_COUNT = 4
};

typedef void (*GameFunctionPtr)();

struct Game
{
  const char *name;
  GameFunctionPtr startGame;
  GameFunctionPtr gameLoop;
  GameFunctionPtr stopGame; // New function pointer for stopping games
};

Game games[GAME_COUNT];
int currentGameIndex = GAME_NONE;

// Wifi credentials
const char *ssid = "AS";
const char *password = "#$ahSB#$135#$";

// Main server endpoint
const char *serverEndpoint = "http://192.168.1.3:5000/process";

// To get image from camera or change config
#if ENABLE_ESP32_SERVER
WebServer server(80);
#endif

// Camera configuration
sensor_t *sensor = nullptr;

void initCamera();
void connectToWiFi();
void initGames();
void switchGame(int gameIndex);

// Function declarations for stopping games
void stopXOGame();
void stopRubikGame();
void stopMemoryGame();
void stopCupsGame();

String readLine();
bool sendServoCommand(int a1, int a2, int a3);
bool sendStepperCommand(const int cmds[10]);

void changeConfig(String command);
String getPythonData(String command);

#if ENABLE_ESP32_SERVER
void setupServerEndpoints();
bool toBool(String value);

#if ENABLE_SERVER_GAME_CHANGE
void handleChangeGame();
#endif

#if ENABLE_SERVER_CONFIG
void handleConfig();
#endif

#if ENABLE_SERVER_STREAMING
void handleStream();
#endif

#endif

void setup()
{
  // Serial monitor for debugging
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  // Arduino communication
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  initCamera();
  connectToWiFi();
  initGames();

#if ENABLE_ESP32_SERVER
  setupServerEndpoints();
#endif
}

void loop()
{
#if ENABLE_ESP32_SERVER
  server.handleClient();
#endif

  if (currentGameIndex >= 0 && currentGameIndex < GAME_COUNT)
    games[currentGameIndex].gameLoop();
}

// Setup functions
void initCamera()
{
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
}

void connectToWiFi()
{
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
}

// Arduino communication functions
String readLine(int timeout)
{
  unsigned long start = millis();
  String s;
  while (millis() - start < timeout)
  {
    if (Serial2.available())
    {
      char c = Serial2.read();
      if (c == '\n')
        break;
      s += c;
    }
  }

  if (s.length() > 0 && s[s.length() - 1] == '\r')
    s.remove(s.length() - 1, 1);

  Serial.print("Received: ");
  Serial.println(s);

  return s;
}

bool sendServoCommand(int a1, int a2, int a3)
{
  while (Serial2.available())
    Serial2.read();

  Serial2.print('A');
  Serial2.print(',');
  Serial2.print(a1);
  Serial2.print(',');
  Serial2.print(a2);
  Serial2.print(',');
  Serial2.println(a3);

  String resp = readLine(TIMEOUT_MS_SERVO);

  return (resp == "OK");
}

bool sendStepperCommand(const int cmds[STEPPER_COUNT])
{
  while (Serial2.available())
    Serial2.read();

  Serial2.print('S');
  for (int i = 0; i < STEPPER_COUNT; i++)
  {
    Serial2.print(',');
    Serial2.print(cmds[i]);
  }
  Serial2.println();

  String resp = readLine(TIMEOUT_MS_STEPPER);

  return (resp == "OK");
}

// Change camera configuration
String latestGame = "";
void changeConfig(String game)
{
  if (latestGame == game)
    return;

  latestGame = game;
  Serial.println("Changing config to: " + game);

  sensor_t *s = esp_camera_sensor_get();

  // Reset to default settings
  s->set_framesize(s, FRAMESIZE_VGA);
  s->set_quality(s, 12);
  s->set_saturation(s, 0);
  analogWrite(LED_GPIO_NUM, 0);

  if (game == "xo")
  {
    s->set_framesize(s, FRAMESIZE_VGA);
    s->set_quality(s, 9);
    s->set_saturation(s, 2);
    analogWrite(LED_GPIO_NUM, 200);
  }
  else if (game == "rubik")
  {
    //
  }
}

// Server communication functions
String getPythonData(String command)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi not connected.");
    return "ERROR";
  }

  camera_fb_t *fb = nullptr;

  fb = esp_camera_fb_get();
  if (!fb)
  {
    Serial.println("Camera capture failed");
    return "ERROR";
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

  esp_camera_fb_return(fb);

  return response;
}

// Game management functions
void initGames()
{
  games[GAME_XO] = {"XO", startXOGame, xoGameLoop, stopXOGame};
  games[GAME_RUBIK] = {"Rubik's Cube", startRubikGame, rubikGameLoop, stopRubikGame};
  games[GAME_MEMORY] = {"Memory", startMemoryGame, memoryGameLoop, stopMemoryGame};
  games[GAME_CUPS] = {"3 Cups", startCupsGame, cupsGameLoop, stopCupsGame};
}

void switchGame(int gameIndex)
{
  if (gameIndex < 0 || gameIndex >= GAME_COUNT)
  {
    Serial.println("Invalid game index");
    return;
  }

  // Stop the current game if one is running
  if (currentGameIndex >= 0 && currentGameIndex < GAME_COUNT)
  {
    Serial.print("Stopping game: ");
    Serial.println(games[currentGameIndex].name);
    games[currentGameIndex].stopGame();
  }

  if (gameIndex >= 0 && gameIndex < GAME_COUNT)
  {
    currentGameIndex = gameIndex;
    Serial.print("Switching to game: ");
    Serial.println(games[currentGameIndex].name);
    games[currentGameIndex].startGame();
  }
  else
  {
    currentGameIndex = GAME_NONE;
  }
}

// ESP32 Server Endpoints Handlers
#if ENABLE_ESP32_SERVER
void setupServerEndpoints()
{
#if ENABLE_SERVER_CONFIG
  server.on("/config", HTTP_GET, handleConfig);
#endif

#if ENABLE_SERVER_STREAMING
  server.on("/stream", HTTP_GET, handleStream);
#endif

#if ENABLE_SERVER_GAME_CHANGE
  server.on("/changeGame", HTTP_GET, handleChangeGame);
#endif

  server.begin();
  Serial.println("HTTP server started on port 80");

#if ENABLE_SERVER_STREAMING
  Serial.println("Use '/stream' to access the stream.");
#endif

#if ENABLE_SERVER_CONFIG
  Serial.println("Use '/config' to set config.");
#endif

#if ENABLE_SERVER_GAME_CHANGE
  Serial.println("Use '/changeGame?game=NAME' to switch games. Available games: xo, rubik, memory, cups, none.");
#endif
}

bool toBool(String value)
{
  value.toLowerCase();
  return (value == "1" || value == "true" || value == "on");
}

#if ENABLE_SERVER_CONFIG
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
#endif

#if ENABLE_SERVER_STREAMING
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
#endif

#if ENABLE_SERVER_GAME_CHANGE
void handleChangeGame()
{
  if (!server.hasArg("game"))
  {
    server.send(400, "text/plain", "Missing 'game' parameter");
    return;
  }

  String gameParam = server.arg("game");
  int gameIndex = -1;

  if (gameParam == "xo")
    gameIndex = GAME_XO;
  else if (gameParam == "rubik")
    gameIndex = GAME_RUBIK;
  else if (gameParam == "memory")
    gameIndex = GAME_MEMORY;
  else if (gameParam == "cups")
    gameIndex = GAME_CUPS;
  else if (gameParam == "none")
    gameIndex = GAME_NONE;

  if (gameIndex >= GAME_NONE)
  {
    if (gameIndex == GAME_NONE)
    {
      // Stop current game before setting to GAME_NONE
      if (currentGameIndex >= 0 && currentGameIndex < GAME_COUNT)
      {
        games[currentGameIndex].stopGame();
      }
      currentGameIndex = GAME_NONE;
      server.send(200, "text/plain", "All games stopped");
    }
    else
    {
      switchGame(gameIndex);
      server.send(200, "text/plain", "Game switched to " + gameParam);
    }
  }
  else
  {
    server.send(400, "text/plain", "Invalid game name. Use: xo, rubik, memory, cups or none");
  }
}
#endif

#endif