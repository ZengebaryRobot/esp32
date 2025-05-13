#ifndef GAME_UTILS_H
#define GAME_UTILS_H

#include <Arduino.h>

String getPythonData(String command);
void parseCSV(const char *csv, int arr[], int &count);
bool sendServoCommand(int a1, int a2, int a3);
bool sendStepperCommand(const int cmds[10]);
void changeConfig(String command);
void printOnLCD(const String &msg);

// Arm enum
enum ArmMotor
{
  BASE = 0,
  SHOULDER = 1,
  ELBOW = 2,
  WRIST = 3,
  GRIP = 4
};

#endif
