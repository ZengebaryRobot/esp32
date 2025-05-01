#include "rubik_game.h"
#include "game_utils.h"
#include <Arduino.h>

extern void changeConfig(String command);
extern String getPythonData(String command);
extern bool sendServoCommand(int a1, int a2, int a3);
extern bool sendStepperCommand(const int cmds[]);

void startRubikGame()
{
  Serial.println("Starting Rubik's Cube Game");
  changeConfig("rubik");

  //
}

void rubikGameLoop()
{
  //
}

void stopRubikGame()
{
  Serial.println("Stopping Rubik's Cube Game");
  changeConfig("none");

  //
}