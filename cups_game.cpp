#include "cups_game.h"
#include "game_utils.h"
#include <Arduino.h>

extern void changeConfig(String command);
extern String getPythonData(String command);
extern void parseCSV(const char *csv, int arr[], int &count);
extern bool sendServoCommand(int a1, int a2, int a3);
extern bool sendStepperCommand(const int cmds[]);
extern void printOnLCD(const String &msg);

// Constants for grip
#define GRIP_CLOSED 72
#define GRIP_OPEN 100
#define DEFAULT_ANGLE_SHOULDER 90

// Servo angles for each cup [base, shoulder, elbow, wrist]
const int cupAngles[3][4] = {
    {30, 90, 90, 90}, // Cup 1: base=30°, others default
    {90, 90, 90, 90}, // Cup 2: base=90°, others default
    {150, 90, 90, 90} // Cup 3: base=150°, others default
};

// Game state
static bool gameEnded = false;
static int cupNumber = -1;

void startCupsGame()
{
  Serial.println("Starting 3cups Game");
  printOnLCD("Starting 3cups");
  changeConfig("cups");

  gameEnded = false;
  cupNumber = -1;
}

void cupsExecuteServoMove(ArmMotor motor, int angle, int overShoot)
{
  while (true)
  {
    if (sendServoCommand(motor, angle, overShoot))
      break;

    Serial.println("Servo command failed, retrying...");
    printOnLCD("Servo failed");
    delay(500);
  }
}

void cupsGoGrab(int requiredBaseAngle, int requiredShoulderAngle, int requiredElbowAngle, int requiredWristAngle)
{
  cupsExecuteServoMove(ArmMotor::GRIP, GRIP_OPEN, 0);
  cupsExecuteServoMove(ArmMotor::SHOULDER, DEFAULT_ANGLE_SHOULDER, 10);
  cupsExecuteServoMove(ArmMotor::BASE, requiredBaseAngle, 0);
  cupsExecuteServoMove(ArmMotor::WRIST, requiredWristAngle, 4);
  cupsExecuteServoMove(ArmMotor::ELBOW, requiredElbowAngle, 0);
  cupsExecuteServoMove(ArmMotor::SHOULDER, requiredShoulderAngle, 10);
  cupsExecuteServoMove(ArmMotor::GRIP, GRIP_CLOSED, 0);
}

void cupsGoRelease(int requiredBaseAngle, int requiredShoulderAngle, int requiredElbowAngle, int requiredWristAngle)
{
  cupsExecuteServoMove(ArmMotor::SHOULDER, DEFAULT_ANGLE_SHOULDER, 10);
  cupsExecuteServoMove(ArmMotor::BASE, requiredBaseAngle, 0);
  cupsExecuteServoMove(ArmMotor::WRIST, requiredWristAngle, 4);
  cupsExecuteServoMove(ArmMotor::ELBOW, requiredElbowAngle, 0);
  cupsExecuteServoMove(ArmMotor::SHOULDER, requiredShoulderAngle, 10);
  cupsExecuteServoMove(ArmMotor::GRIP, GRIP_OPEN, 0);
}

void getAnglesForCup(int cupIdx, int angles[4])
{
  for (int i = 0; i < 4; i++)
  {
    angles[i] = cupAngles[cupIdx][i];
  }
}

bool parseCameraResponse(const String &response)
{
  // Expected format: "red,none,none"
  char temp[128];
  strncpy(temp, response.c_str(), sizeof(temp));
  temp[sizeof(temp) - 1] = '\0';

  char *token = strtok(temp, ",");
  int index = 0;
  while (token && index < 3)
  {
    // Remove leading/trailing spaces
    while (*token == ' ')
      token++;
    char *end = token + strlen(token) - 1;
    while (end > token && *end == ' ')
      *end-- = '\0';

    if (strcmp(token, "red") == 0)
    {
      cupNumber = index + 1; // Cup 1, 2, or 3
      return true;
    }
    token = strtok(NULL, ",");
    index++;
  }
  return false;
}

void cupsGameLoop()
{
  if (gameEnded)
    return;

  Serial.println("Requesting camera data for 3cups");
  printOnLCD("Scanning cups");

  String res = getPythonData("cupsResult");

  if (res != "ERROR")
  {
    Serial.print("Camera response: ");
    Serial.println(res);

    if (parseCameraResponse(res))
    {
      Serial.print("Ball detected under cup ");
      Serial.println(cupNumber);
      printOnLCD("Cup " + String(cupNumber));

      int ang[4];
      getAnglesForCup(cupNumber - 1, ang);

      cupsGoGrab(ang[0], ang[1], ang[2], ang[3]);
      delay(1000);
      cupsGoRelease(ang[0], ang[1], ang[2], ang[3]);

      Serial.print("Lifted cup ");
      Serial.println(cupNumber);
      printOnLCD("Lifted cup " + String(cupNumber));

      gameEnded = true;
      Serial.println("3cups game completed");
      printOnLCD("Game completed");
    }
    else
    {
      Serial.println("Invalid camera response");
      printOnLCD("Invalid data");
    }
  }
  else
  {
    Serial.println("Camera failed");
    printOnLCD("Camera error");
  }

  delay(1000);
}

void stopCupsGame()
{
  Serial.println("Stopping 3cups Game");
  printOnLCD("Stopping 3cups");
  changeConfig("none");
}