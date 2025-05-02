#include "xo_game.h"
#include "game_utils.h"
#include <Arduino.h>

extern void changeConfig(String command);
extern String getPythonData(String command);
extern void parseCSV(const char *csv, int arr[], int &count);
extern bool sendServoCommand(int a1, int a2, int a3);
extern bool sendStepperCommand(const int cmds[]);

void startXOGame()
{
  Serial.println("Starting XO Game");
  changeConfig("xo");

  //
}

void xoGameLoop()
{
  // Check for game state updates from Python server
  String response = "ERROR";
  while (response == "ERROR")
  {
    response = getPythonData("xo");
    if (response == "ERROR")
    {
      Serial.println("Failed to get game state from Python server. Retrying...");
      delay(1000);
    }
  }
  Serial.println("Game state received: " + response);

  while (!sendServoCommand(1, 2, 3))
  {
    Serial.println("Failed to send servo command");
    delay(1000);
  }
  Serial.println("Servo command sent successfully");

  int stepperCmds[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  while (!sendStepperCommand(stepperCmds))
  {
    Serial.println("Failed to send stepper command");
    delay(1000);
  }
  Serial.println("Stepper command sent successfully");
}

void stopXOGame()
{
  Serial.println("Stopping XO Game");
  changeConfig("none");

  //
}