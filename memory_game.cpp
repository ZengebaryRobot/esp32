#include "memory_game.h"
#include "game_utils.h"
#include <Arduino.h>

extern void changeConfig(String command);
extern String getPythonData(String command);
extern void parseCSV(const char *csv, int arr[], int &count);
extern bool sendServoCommand(int a1, int a2, int a3);
extern bool sendStepperCommand(const int cmds[]);
extern void printOnLCD(const String &msg);

void startMemoryGame()
{
  Serial.println("Starting Memory Game");
  changeConfig("memory");
}

void memoryGameLoop()
{
  //
}

void stopMemoryGame()
{
  Serial.println("Stopping Memory Game");
  changeConfig("none");

  //
}