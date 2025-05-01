#ifndef GAME_UTILS_H
#define GAME_UTILS_H

#include <Arduino.h>

String getPythonData(String command);
bool sendServoCommand(int a1, int a2, int a3);
bool sendStepperCommand(const int cmds[10]);
void changeConfig(String command);

#endif
