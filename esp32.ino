#include <WiFi.h>
#include <HTTPClient.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LiquidCrystal_I2C.h>
#include "esp_camera.h"
#include "stream_handler.h"

// Include game files
#include "xo_game.h"
#include "rubik_game.h"
#include "memory_game.h"
#include "threeCups_game.h"

// Enable ESP32 server
#define ENABLE_ESP32_SERVER 1
#define ENABLE_SERVER_STREAMING 1
#define ENABLE_SERVER_CONFIG 1
#define ENABLE_SERVER_GAME_CHANGE 1
#define ENABLE_SERVER_GAME_INFO 1

// LCD Display
#define ENABLE_DISPLAY 1
#define SDA_PIN 14
#define SCL_PIN 15

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
#define MAX_SIZE 30

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
  GameFunctionPtr stopGame;
};

Game games[GAME_COUNT];
int currentGameIndex = GAME_NONE;

// Game switching request
bool gameSwitchInProgress = false;
int requestedGameIndex = -2; // -2 means no game switch requested
SemaphoreHandle_t gameSwitchMutex = NULL;

// Wifi credentials
const char *ssid = "Zengebary";
const char *password = "1234abcdABCD";

// Main server endpoint
const char *serverEndpoint = "http://192.168.1.4:8000/process";

// HTTP server
#if ENABLE_ESP32_SERVER
AsyncWebServer server(80);
#endif

// LCD Display
#if ENABLE_DISPLAY
LiquidCrystal_I2C lcd(0x27, 16, 2);
#endif

// Camera configuration
sensor_t *sensor = nullptr;

void initCamera();
void connectToWiFi();
void initGames();
bool switchGame(int gameIndex);

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
void parseCSV(const char *csv, int arr[], int &count);

#if ENABLE_DISPLAY
void initDisplay();
void printOnLCD(const String &msg);
#endif

#if ENABLE_ESP32_SERVER
void setupServerEndpoints();
bool toBool(String value);

#if ENABLE_SERVER_GAME_CHANGE
void handleChangeGame(AsyncWebServerRequest *request);
#endif

#if ENABLE_SERVER_GAME_INFO
void handleGetCurrentGame(AsyncWebServerRequest *request);
#endif

#if ENABLE_SERVER_CONFIG
void handleConfig(AsyncWebServerRequest *request);
#endif

#if ENABLE_SERVER_STREAMING
void handleStream(AsyncWebServerRequest *request);
void handleStreamJpg(AsyncWebServerRequest *request);
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
  changeConfig("none");

#if ENABLE_DISPLAY
  initDisplay();
  printOnLCD("Zengebary       No Game is selected");
#endif

#if ENABLE_ESP32_SERVER
  setupServerEndpoints();
#endif

  // Create mutex for game switching
  gameSwitchMutex = xSemaphoreCreateMutex();
}

void loop()
{
  // Check if a game switch has been requested
  if (xSemaphoreTake(gameSwitchMutex, 0) == pdTRUE)
  {
    if (requestedGameIndex != -2)
    {
      int gameToSwitch = requestedGameIndex;
      requestedGameIndex = -2;
      performGameSwitch(gameToSwitch);
    }
    xSemaphoreGive(gameSwitchMutex);
  }

  // Run the current game loop if one is active
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
  // Set static IP address
  IPAddress local_ip(192, 168, 1, 3);
  IPAddress gateway(192, 168, 1, 1);
  IPAddress subnet(255, 255, 255, 0);
  if (!WiFi.config(local_ip, gateway, subnet))
  {
    Serial.println("Failed to configure static IP");
  }

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

  if (game == "xo")
  {
    s->set_framesize(s, (framesize_t)10);
    s->set_quality(s, 9);
    s->set_contrast(s, 0);
    s->set_brightness(s, 0);
    s->set_saturation(s, 2);
    s->set_gainceiling(s, (gainceiling_t)0);
    s->set_colorbar(s, 0);
    s->set_whitebal(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_hmirror(s, 0);
    s->set_vflip(s, 0);
    s->set_awb_gain(s, 1);
    s->set_agc_gain(s, 0);
    s->set_aec_value(s, 168);
    s->set_aec2(s, 1);
    s->set_dcw(s, 1);
    s->set_bpc(s, 1);
    s->set_wpc(s, 1);
    s->set_raw_gma(s, 1);
    s->set_lenc(s, 1);
    s->set_special_effect(s, 0);
    s->set_wb_mode(s, 0);
    s->set_ae_level(s, 0);
    analogWrite(LED_GPIO_NUM, 200);
  }
  else if (game == "rubik")
  {
    s->set_framesize(s, (framesize_t)10);
    s->set_quality(s, 9);
    s->set_contrast(s, 0);
    s->set_brightness(s, 0);
    s->set_saturation(s, 0);
    s->set_gainceiling(s, (gainceiling_t)0);
    s->set_colorbar(s, 0);
    s->set_whitebal(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_hmirror(s, 1);
    s->set_vflip(s, 1);
    s->set_awb_gain(s, 1);
    s->set_agc_gain(s, 0);
    s->set_aec_value(s, 168);
    s->set_aec2(s, 1);
    s->set_dcw(s, 1);
    s->set_bpc(s, 1);
    s->set_wpc(s, 1);
    s->set_raw_gma(s, 1);
    s->set_lenc(s, 1);
    s->set_special_effect(s, 0);
    s->set_wb_mode(s, 0);
    s->set_ae_level(s, 0);
    analogWrite(LED_GPIO_NUM, 150);
  }
  else if (game == "memory")
  {
    s->set_framesize(s, (framesize_t)13);
    s->set_quality(s, 10);
    s->set_contrast(s, 2);
    s->set_brightness(s, -2);
    s->set_saturation(s, 0);
    s->set_gainceiling(s, (gainceiling_t)0);
    s->set_colorbar(s, 0);
    s->set_whitebal(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_hmirror(s, 0);
    s->set_vflip(s, 0);
    s->set_awb_gain(s, 1);
    s->set_agc_gain(s, 0);
    s->set_aec_value(s, 1200);
    s->set_aec2(s, 1);
    s->set_dcw(s, 1);
    s->set_bpc(s, 1);
    s->set_wpc(s, 1);
    s->set_raw_gma(s, 1);
    s->set_lenc(s, 1);
    s->set_special_effect(s, 2);
    s->set_wb_mode(s, 0);
    s->set_ae_level(s, 2);
    analogWrite(LED_GPIO_NUM, 136);
  }
  else if (game == "cups")
  {
    s->set_framesize(s, (framesize_t)10);
    s->set_quality(s, 9);
    s->set_contrast(s, 0);
    s->set_brightness(s, 0);
    s->set_saturation(s, 0);
    s->set_gainceiling(s, (gainceiling_t)0);
    s->set_colorbar(s, 0);
    s->set_whitebal(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_hmirror(s, 0);
    s->set_vflip(s, 0);
    s->set_awb_gain(s, 1);
    s->set_agc_gain(s, 0);
    s->set_aec_value(s, 168);
    s->set_aec2(s, 1);
    s->set_dcw(s, 1);
    s->set_bpc(s, 1);
    s->set_wpc(s, 1);
    s->set_raw_gma(s, 1);
    s->set_lenc(s, 1);
    s->set_special_effect(s, 0);
    s->set_wb_mode(s, 0);
    s->set_ae_level(s, 0);
    analogWrite(LED_GPIO_NUM, 200);
  }
  else
  {
    s->set_framesize(s, (framesize_t)10);
    s->set_quality(s, 9);
    s->set_contrast(s, 0);
    s->set_brightness(s, 0);
    s->set_saturation(s, 0);
    s->set_gainceiling(s, (gainceiling_t)0);
    s->set_colorbar(s, 0);
    s->set_whitebal(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_hmirror(s, 0);
    s->set_vflip(s, 0);
    s->set_awb_gain(s, 1);
    s->set_agc_gain(s, 0);
    s->set_aec_value(s, 168);
    s->set_aec2(s, 1);
    s->set_dcw(s, 1);
    s->set_bpc(s, 1);
    s->set_wpc(s, 1);
    s->set_raw_gma(s, 1);
    s->set_lenc(s, 1);
    s->set_special_effect(s, 0);
    s->set_wb_mode(s, 0);
    s->set_ae_level(s, 0);
    analogWrite(LED_GPIO_NUM, 0);
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

void parseCSV(const char *csv, int arr[], int &count)
{
  count = 0;
  char buffer[256];
  strncpy(buffer, csv, sizeof(buffer));
  buffer[sizeof(buffer) - 1] = '\0';

  char *token = strtok(buffer, ",");

  while (token != NULL && count < MAX_SIZE)
  {
    arr[count++] = atoi(token);
    token = strtok(NULL, ",");
  }
}

// LCD Display
#if ENABLE_DISPLAY
void initDisplay()
{
  Wire.begin(SDA_PIN, SCL_PIN);

  lcd.init();
  lcd.backlight();

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Ready!");
}
#endif

void printOnLCD(const String &msg)
{
#if ENABLE_DISPLAY
  uint16_t len = msg.length();
  if (len > 32)
    len = 32;

  lcd.setCursor(0, 0);
  lcd.print(msg.substring(0, min<uint16_t>(len, 16)));

  lcd.setCursor(0, 1);
  if (len > 16)
  {
    lcd.print(msg.substring(16, len));
  }
  else
  {
    lcd.print("                ");
  }
#else
  Serial.print("LCD: ");
  Serial.println(msg);
#endif
}

// Game management functions
void initGames()
{
  games[GAME_XO] = {"XO", startXOGame, xoGameLoop, stopXOGame};
  games[GAME_RUBIK] = {"Rubik's Cube", startRubikGame, rubikGameLoop, stopRubikGame};
  games[GAME_MEMORY] = {"Memory", startMemoryGame, memoryGameLoop, stopMemoryGame};
  games[GAME_CUPS] = {"3 Cups", startCupsGame, cupsGameLoop, stopCupsGame};
}

bool switchGame(int gameIndex)
{
  if (xSemaphoreTake(gameSwitchMutex, portMAX_DELAY) == pdTRUE)
  {
    requestedGameIndex = gameIndex;
    xSemaphoreGive(gameSwitchMutex);
    return true;
  }
  else
  {
    Serial.println("Game already switching");
    return false;
  }
}

void performGameSwitch(int gameIndex)
{
  Serial.print("Performing game switch to: ");
  Serial.println(gameIndex);

  if (gameIndex < -1 || gameIndex >= GAME_COUNT)
  {
    Serial.println("Invalid game index");
    printOnLCD("Invalid game");
    return;
  }

  bool sameGame = (currentGameIndex == gameIndex);

  // Stop the current game if one is running
  if (currentGameIndex >= 0 && currentGameIndex < GAME_COUNT)
  {
    Serial.print("Stopping game: ");
    Serial.println(games[currentGameIndex].name);
    printOnLCD("Stopping: " + String(games[currentGameIndex].name));
    games[currentGameIndex].stopGame();
  }

  if (gameIndex >= 0 && gameIndex < GAME_COUNT)
  {
    currentGameIndex = gameIndex;
    String message = sameGame ? "Restarting: " : "Starting: ";
    message += String(games[currentGameIndex].name);

    Serial.print(sameGame ? "Restarting game: " : "Switching to game: ");
    Serial.println(games[currentGameIndex].name);

    printOnLCD(message);
    games[currentGameIndex].startGame();
  }
  else
  {
    currentGameIndex = GAME_NONE;
    printOnLCD("Zengebary       No Game is selected");
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

  if (start_stream_server())
  {
    Serial.println("ESP-IDF streaming server started on port 81");
  }
  else
  {
    Serial.println("Failed to start ESP-IDF streaming server");
  }

  server.on("/stream", HTTP_GET, handleStream);
  server.on("/streamjpg", HTTP_GET, handleStreamJpg);
#endif

#if ENABLE_SERVER_GAME_CHANGE
  server.on("/changeGame", HTTP_GET, handleChangeGame);
#endif

#if ENABLE_SERVER_GAME_INFO
  server.on("/getCurrentGame", HTTP_GET, handleGetCurrentGame);
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

#if ENABLE_SERVER_GAME_INFO
  Serial.println("Use '/getCurrentGame' to get current game info.");
#endif
}

bool toBool(String value)
{
  value.toLowerCase();
  return (value == "1" || value == "true" || value == "on");
}

#if ENABLE_SERVER_CONFIG
void handleConfig(AsyncWebServerRequest *request)
{
  sensor_t *s = esp_camera_sensor_get();

  if (request->hasParam("framesize"))
  {
    String value = request->getParam("framesize")->value();
    s->set_framesize(s, (framesize_t)value.toInt());
  }

  if (request->hasParam("quality"))
    s->set_quality(s, request->getParam("quality")->value().toInt());
  if (request->hasParam("contrast"))
    s->set_contrast(s, request->getParam("contrast")->value().toInt());
  if (request->hasParam("brightness"))
    s->set_brightness(s, request->getParam("brightness")->value().toInt());
  if (request->hasParam("saturation"))
    s->set_saturation(s, request->getParam("saturation")->value().toInt());
  if (request->hasParam("gainceiling"))
    s->set_gainceiling(s, (gainceiling_t)request->getParam("gainceiling")->value().toInt());

  if (request->hasParam("colorbar"))
    s->set_colorbar(s, toBool(request->getParam("colorbar")->value()));
  if (request->hasParam("awb"))
    s->set_whitebal(s, toBool(request->getParam("awb")->value()));
  if (request->hasParam("agc"))
    s->set_gain_ctrl(s, toBool(request->getParam("agc")->value()));
  if (request->hasParam("aec"))
    s->set_exposure_ctrl(s, toBool(request->getParam("aec")->value()));
  if (request->hasParam("hmirror"))
    s->set_hmirror(s, toBool(request->getParam("hmirror")->value()));
  if (request->hasParam("vflip"))
    s->set_vflip(s, toBool(request->getParam("vflip")->value()));

  if (request->hasParam("awb_gain"))
    s->set_awb_gain(s, request->getParam("awb_gain")->value().toInt());
  if (request->hasParam("agc_gain"))
    s->set_agc_gain(s, request->getParam("agc_gain")->value().toInt());
  if (request->hasParam("aec_value"))
    s->set_aec_value(s, request->getParam("aec_value")->value().toInt());

  if (request->hasParam("aec2"))
    s->set_aec2(s, toBool(request->getParam("aec2")->value()));
  if (request->hasParam("dcw"))
    s->set_dcw(s, toBool(request->getParam("dcw")->value()));
  if (request->hasParam("bpc"))
    s->set_bpc(s, toBool(request->getParam("bpc")->value()));
  if (request->hasParam("wpc"))
    s->set_wpc(s, toBool(request->getParam("wpc")->value()));
  if (request->hasParam("raw_gma"))
    s->set_raw_gma(s, toBool(request->getParam("raw_gma")->value()));
  if (request->hasParam("lenc"))
    s->set_lenc(s, toBool(request->getParam("lenc")->value()));

  if (request->hasParam("ae_level"))
    s->set_ae_level(s, request->getParam("ae_level")->value().toInt());

  if (request->hasParam("led_intensity"))
  {
    int intensity = request->getParam("led_intensity")->value().toInt();
    analogWrite(LED_GPIO_NUM, intensity);
  }

  request->send(200, "text/plain", "Camera settings updated!");
}
#endif

#if ENABLE_SERVER_STREAMING
void handleStream(AsyncWebServerRequest *request)
{
  request->redirect("http://" + WiFi.localIP().toString() + ":81/stream");
}

void handleStreamJpg(AsyncWebServerRequest *request)
{
  static unsigned long last_capture = 0;
  unsigned long current_millis = millis();

  if (current_millis - last_capture < 50)
  {
    delay(50);
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb)
  {
    Serial.println("Camera capture failed in handleStreamJpg. Possible causes: camera not initialized, insufficient memory, or hardware issue");

    // Try capture again
    fb = esp_camera_fb_get();
    if (!fb)
    {
      request->send(500, "text/plain", "Camera capture failed - Please restart device");
      return;
    }
  }

  last_capture = current_millis;

  AsyncWebServerResponse *response = request->beginResponse_P(200, "image/jpeg", fb->buf, fb->len);
  response->addHeader("Content-Disposition", "inline; filename=capture.jpg");
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "0");
  request->send(response);

  esp_camera_fb_return(fb);
}
#endif

#if ENABLE_SERVER_GAME_CHANGE
void handleChangeGame(AsyncWebServerRequest *request)
{
  if (!request->hasParam("game"))
  {
    request->send(400, "text/plain", "Missing 'game' parameter");
    return;
  }

  String gameParam = request->getParam("game")->value();
  int gameIndex = -1;

  if (gameParam == "xo")
    gameIndex = GAME_XO;
  else if (gameParam == "rubik")
    gameIndex = GAME_RUBIK;
  else if (gameParam == "memory")
    gameIndex = GAME_MEMORY;
  else if (gameParam == "cups")
    gameIndex = GAME_CUPS;
  else
    gameIndex = GAME_NONE;

  if (gameIndex >= GAME_NONE)
  {
    bool success = switchGame(gameIndex);
    if (success)
    {
      request->send(200, "text/plain", "Game change request queued: " + gameParam);
    }
    else
    {
      request->send(409, "text/plain", "Game already switching");
    }
  }
  else
  {
    request->send(400, "text/plain", "Invalid game name. Use: xo, rubik, memory, cups or none");
  }
}
#endif

#if ENABLE_SERVER_GAME_INFO
void handleGetCurrentGame(AsyncWebServerRequest *request)
{
  String response;

  if (currentGameIndex == GAME_NONE)
  {
    response = "None";
  }
  else
  {
    response = String(games[currentGameIndex].name);
  }

  request->send(200, "text/plain", response);
}
#endif
#endif
