#include "rubik_game.h"
#include "game_utils.h"
#include <Arduino.h>

extern void changeConfig(String command);
extern String getPythonData(String command);
extern void parseCSV(const char *csv, int arr[], int &count);
extern bool sendServoCommand(int a1, int a2, int a3);
extern bool sendStepperCommand(const int cmds[]);
extern void printOnLCD(const String &msg);

String movesString;
int moves[30];
uint8_t movesCount;
int i;
int stepperCmd[10] = {0};

void parseString(String str, int *data, uint8_t &count)
{
  count = 0;
  char buffer[256];
  strncpy(buffer, str.c_str(), sizeof(buffer));
  buffer[sizeof(buffer) - 1] = '\0';

  char *token = strtok(buffer, ",");

  while (token != NULL && count < 30)
  {
    data[count++] = atoi(token);
    token = strtok(NULL, ",");
  }
}

void sendLastFaceToServer(int *data, uint8_t &count)
{
  while (true)
  {
    String res = getPythonData("rubik");

    if (res != "ERROR")
    {
      movesString = res;
      // Convert the string array-like to an integer array
      parseString(movesString, data, count);
      Serial.println("Moves: " + res);
      break;
    }
    else
    {
      count = 0;
      Serial.println("Camera failed while scanning face");
    }
  }
}

void sendFaceToServer()
{
  while (true)
  {
    String res = getPythonData("rubik");

    if (res != "ERROR")
    {
      Serial.println("Scanned face: " + res);
      break;
    }
    else
    {
      Serial.println("Camera failed while scanning face");
    }
  }
}

void startRubikGame()
{
  Serial.println("Starting Rubik's Cube Game");
  changeConfig("rubik");

  while (true)
  {
    String res = getPythonData("rubikReset");
    if (res != "ERROR")
    {
      Serial.println("Rubik's Cube reset successful");
      break;
    }
    else
    {
      Serial.println("Failed to start Rubik's Cube, retrying...");
    }
  }

  // Fix array assignments
  memset(moves, 0, sizeof(moves));
  movesCount = 0;
  i = 0;
  memset(stepperCmd, 0, sizeof(stepperCmd));
}

void parseStepperCommands(int move)
{
  // Moves array = {xy}, where x = motor, y = angle (1, 2, 3)
  // Angle: 1: 90, 2: 180, 3: -90
  int motor = move / 10;  // Changed from 's' to 'move'
  int code = move % 10;   // Changed from 's' to 'move'

  int direction = 0;
  int angle = 0;

  if (code == 1)
  {
    angle = 90;
  }
  else if (code == 2)
  {
    angle = 180;
  }
  else
  {
    angle = 90;
    direction = 1;
  }

  // Clear the stepperCmd array first
  memset(stepperCmd, 0, sizeof(stepperCmd));

  switch (motor)  // Changed from 'id' to 'motor'
  {
  case 1:
    stepperCmd[0] = angle;
    stepperCmd[1] = direction;
    sendStepperCommand(stepperCmd);
    break;
  case 4:
    stepperCmd[6] = angle;
    stepperCmd[7] = direction;
    sendStepperCommand(stepperCmd);
    break;
  case 5:
    stepperCmd[8] = angle;
    stepperCmd[9] = direction;
    sendStepperCommand(stepperCmd);
    break;
  case 2:
    stepperCmd[2] = angle;
    stepperCmd[3] = direction;
    sendStepperCommand(stepperCmd);
    break;
  case 3:
    stepperCmd[4] = angle;
    stepperCmd[5] = direction;
    sendStepperCommand(stepperCmd);
    break;
  default:
    return; // Invalid input
  }
}
/**
 * U: 1 -> [0,1]
 * D: 4 -> [6,7]
 * L: 5 -> [8,9]
 * R: 2 -> [2,3]
 * F: 3 -> [4,5]
 */
void rubikGameLoop()
{
  if (i < 4)
  {
    sendFaceToServer();
    stepperCmd[2] = 90;
    stepperCmd[3] = 0;
    sendStepperCommand(stepperCmd); // R1
    stepperCmd[2] = 0;
    delay(20);

    stepperCmd[8] = 90;
    stepperCmd[9] = 1;
    sendStepperCommand(stepperCmd); // L3
    stepperCmd[8] = 0;
    delay(20);
  }
  else if (i >= 4 && i < 8)
  {
    stepperCmd[6] = 90;
    stepperCmd[7] = 0;
    sendStepperCommand(stepperCmd); // D1
    stepperCmd[6] = 0;    
    delay(20);

    stepperCmd[0] = 90;
    stepperCmd[1] = 1;
    sendStepperCommand(stepperCmd); // U3
    stepperCmd[0] = 0;
    delay(20);

    if(i < 7) sendFaceToServer();
  }
  else if (i == 8)
  {
    stepperCmd[2] = 90;
    stepperCmd[3] = 0;
    sendStepperCommand(stepperCmd); // R1
    stepperCmd[2] = 0;
    delay(20);

    stepperCmd[0] = 90;
    stepperCmd[1] = 1;
    sendStepperCommand(stepperCmd); // U3
    stepperCmd[0] = 0;
    delay(20);

    stepperCmd[6] = 90;
    stepperCmd[7] = 0;
    sendStepperCommand(stepperCmd); // D1
    stepperCmd[6] = 0;
    delay(20);

    sendFaceToServer();

    // Reverse the moves
    stepperCmd[6] = 90;
    stepperCmd[7] = 1;
    sendStepperCommand(stepperCmd); // D3
    stepperCmd[6] = 0;
    delay(20);

    stepperCmd[0] = 90;
    stepperCmd[1] = 0;
    sendStepperCommand(stepperCmd); // U1
    stepperCmd[0] = 0;
    delay(20);

    
    stepperCmd[2] = 90;
    stepperCmd[3] = 1;
    sendStepperCommand(stepperCmd); // R3
    stepperCmd[2] = 0;
    delay(20);
  }
  else if (i == 9)
  {
    stepperCmd[8] = 90;
    stepperCmd[9] = 1;
    sendStepperCommand(stepperCmd); // L3
    stepperCmd[8] = 0;
    delay(20);

    
    stepperCmd[0] = 90;
    stepperCmd[1] = 0;
    sendStepperCommand(stepperCmd); // U1
    stepperCmd[0] = 0;
    delay(20);

    stepperCmd[6] = 90;
    stepperCmd[7] = 1;
    sendStepperCommand(stepperCmd); // D3
    stepperCmd[6] = 0;
    delay(20);

    sendFaceToServer();

    stepperCmd[6] = 90;
    stepperCmd[7] = 0;
    sendStepperCommand(stepperCmd); // D1
    stepperCmd[6] = 0;
    delay(20);

    
    stepperCmd[0] = 90;
    stepperCmd[1] = 1;
    sendStepperCommand(stepperCmd); // U3
    stepperCmd[0] = 0;
    delay(20);

    
    stepperCmd[8] = 90;
    stepperCmd[9] = 0;
    sendStepperCommand(stepperCmd); // L1
    stepperCmd[8] = 0;
    delay(20);
  }
  else if (i == 10)
  {
    
    stepperCmd[6] = 90;
    stepperCmd[7] = 0;
    sendStepperCommand(stepperCmd); // D1
    stepperCmd[6] = 0;
    delay(20);

    stepperCmd[8] = 90;
    stepperCmd[9] = 0;
    sendStepperCommand(stepperCmd); // L1
    stepperCmd[8] = 0;
    delay(20);

    
    stepperCmd[2] = 90;
    stepperCmd[3] = 1;
    sendStepperCommand(stepperCmd); // R3
    stepperCmd[2] = 0;
    delay(20);

    sendFaceToServer();

    // Reverse the moves
    
    stepperCmd[2] = 90;
    stepperCmd[3] = 0;
    sendStepperCommand(stepperCmd); // R1
    stepperCmd[2] = 0;
    delay(20);
    
    stepperCmd[8] = 90;
    stepperCmd[9] = 1;
    sendStepperCommand(stepperCmd); // L3
    stepperCmd[8] = 0;
    delay(20);

    
    stepperCmd[6] = 90;
    stepperCmd[7] = 1;
    sendStepperCommand(stepperCmd); // D3
    stepperCmd[6] = 0;
    delay(20);
  }
  else if (i == 11)
  {
    
    stepperCmd[0] = 90;
    stepperCmd[1] = 1;
    sendStepperCommand(stepperCmd); // U3
    stepperCmd[0] = 0;
    delay(20);

    
    stepperCmd[8] = 90;
    stepperCmd[9] = 1;
    sendStepperCommand(stepperCmd); // L3
    stepperCmd[8] = 0;
    delay(20);

    
    stepperCmd[2] = 90;
    stepperCmd[3] = 0;
    sendStepperCommand(stepperCmd); // R1
    stepperCmd[2] = 0;
    delay(20);

    sendLastFaceToServer(moves, movesCount);

    
    stepperCmd[2] = 90;
    stepperCmd[3] = 1;
    sendStepperCommand(stepperCmd); // R3
    stepperCmd[2] = 0;
    delay(20);

    
    stepperCmd[8] = 90;
    stepperCmd[9] = 0;
    sendStepperCommand(stepperCmd); // L1
    stepperCmd[8] = 0;
    delay(20);

    
    stepperCmd[0] = 90;
    stepperCmd[1] = 0;
    sendStepperCommand(stepperCmd); // U1
    stepperCmd[0] = 0;
    delay(20);
  }
  else if (i == 12)
  {
    if(movesCount == 0) return;
    for (int k = 0; k < movesCount; k++)
    {
      // Moves array = {xy}, where x = motor, y = angle (1, 2, 3)
      // Angle: 1: 90, 2: 180, 3: -90
      parseStepperCommands(moves[k]);
      delay(20);
    }
  }
  i++;
}

void stopRubikGame()
{
  Serial.println("Stopping Rubik's Cube Game");
  changeConfig("none");

  //
}