#include "threeCups_game.h"
#include "game_utils.h"
#include <Arduino.h>
extern void changeConfig(String command);
extern String getPythonData(String command);
extern void parseCSV(const char *csv, int arr[], int &count);
extern bool sendServoCommand(int a1, int a2, int a3);
extern bool sendStepperCommand(const int cmds[]);
extern void printOnLCD(const String &msg);

// Define cup positions (left, middle, right)
#define LEFT_CUP 0
#define MIDDLE_CUP 1
#define RIGHT_CUP 2
#define NO_CUP -1

#define GRIP_CLOSED 92
#define GRIP_OPEN 130
#define DEFAULT_ANGLE_SHOULDER 80
const int retreatAngles[4] = {90, 90, 90, 90};
// Define game states
enum GameState
{
  GAME_INIT,
  WAITING_FOR_DETECTION,
  PROCESSING_MULTIPLE_CUPS, // New state for handling multiple balls
  GAME_OVER,
  PICK_CUP,
  ROBOT_RETREATING,
  DROP_CUP
};

// States for the grabbing and releasing sequences
enum ArmMoveState
{
  MOVE_IDLE,
  PICK_LEFT,   // Added for cup positions
  DROP_LEFT,   // Added for cup positions
  PICK_MIDDLE, // Added for cup positions
  DROP_MIDDLE, // Added for cup positions
  PICK_RIGHT,  // Added for cup positions
  DROP_RIGHT,  // Added for cup positions
  MOVE_COMPLETE
};

// Cup position angles for the arm to point to
const int cupAngleData[3][4] = {
    {138, 46, 126, 66}, // Left cup
    {108, 60, 146, 80}, // Middle cup
    {80, 48, 126, 64},  // Right cup
};

// State machine variables
static GameState currentState = GAME_INIT;
static ArmMoveState armState = MOVE_IDLE; // Current arm movement state
static unsigned long stateStartTime = 0;
static unsigned long lastActionTime = 0;
static int servoMoveIndex = 0;
static int currentMotor = 0;
static int targetAngle = 0;
static int overShootValue = 0;
static String ballCup[3]; // Cup contents - can be "null" or a string like "red"
static int moveAngles[4] = {0};
static bool gameEnded = false;
static int roundCount = 0;
static int currentCupIndex = 0; // Used for processing multiple cups
static String colors[4] = {"", "red", "blue", "orange"};
static String cupsName[3] = {"Left Cup", "Middle Cup", "Right Cup"};
int dropped = 1;

void startCupsGame()
{
  Serial.println("Starting Three Cups Game");
  changeConfig("cups");

  gameEnded = false;
  // Initialize all cups to "null"
  for (int i = 0; i < 3; i++)
  {
    ballCup[i] = "null";
  }
  roundCount = 1; // Only one round
  currentCupIndex = 0;

  currentState = GAME_INIT;
  armState = MOVE_IDLE; // Initialize arm state
  stateStartTime = millis();

  printOnLCD("3 Cups Game Started");
}

bool cupsExecuteServoMove(ArmMotor motor, int angle)
{
  if (sendServoCommand(motor, angle, 0))
  {
    return true;
  }

  // Serial.println("Servo command failed, will retry...");
  return false;
}

// State machine servo move sequence setup
void setupCupsServoMoveSequence(int baseAngle, int shoulderAngle, int elbowAngle, int wristAngle, ArmMotor Currmotor, int angle)
{
  servoMoveIndex = 0;
  moveAngles[0] = baseAngle;
  moveAngles[1] = shoulderAngle;
  moveAngles[2] = elbowAngle;
  moveAngles[3] = wristAngle;

  currentMotor = Currmotor;
  targetAngle = angle;
}

// Process one servo move step in the sequence
bool gripCups()
{
  if (millis() - lastActionTime < 200)
  {
    return false; // Wait a little between commands
  }

  lastActionTime = millis();

  bool success = cupsExecuteServoMove((ArmMotor)currentMotor, targetAngle);
  if (success)
  {
    servoMoveIndex++;

    switch (servoMoveIndex)
    {
    case 1:
      currentMotor = ArmMotor::BASE;
      targetAngle = moveAngles[0];
      break;
    case 2: // After base
      currentMotor = ArmMotor::WRIST;
      targetAngle = moveAngles[3];
      break;
    case 3: // After wrist
      currentMotor = ArmMotor::ELBOW;
      targetAngle = moveAngles[2];
      break;
    case 4: // After elbow
      currentMotor = ArmMotor::SHOULDER;
      targetAngle = moveAngles[1];
      break;
    case 5: // After shoulder
      currentMotor = ArmMotor::GRIP;
      targetAngle = GRIP_CLOSED;
      break;
    case 6:
      currentMotor = ArmMotor::SHOULDER;
      targetAngle = moveAngles[1] + 40;
      break;
    case 7:        // After grip
      return true; // Sequence complete
    }
  }
  return false; // Sequence not complete yet
}
bool retreatArm()
{
  if (millis() - lastActionTime < 200)
  {
    return false; // Wait a little between commands
  }

  lastActionTime = millis();

  bool success = cupsExecuteServoMove((ArmMotor)currentMotor, targetAngle);
  if (success)
  {
    servoMoveIndex++;

    switch (servoMoveIndex)
    {
    case 1: // After shoulder
      currentMotor = ArmMotor::BASE;
      targetAngle = moveAngles[0];
      break;
    case 2: // After base
      currentMotor = ArmMotor::WRIST;
      targetAngle = moveAngles[3];
      break;
    case 3: // After wrist
      currentMotor = ArmMotor::ELBOW;
      targetAngle = moveAngles[2];
      break;
    case 4: // After elbow
      currentMotor = ArmMotor::GRIP;
      targetAngle = GRIP_OPEN;
      break;
    case 5:        // After grip
      return true; // Sequence complete
    }
  }
  return false; // Sequence not complete yet
}
// Process one servo move step in the sequence
bool dropCups()
{
  if (millis() - lastActionTime < 200)
  {
    return false; // Wait a little between commands
  }

  lastActionTime = millis();

  bool success = cupsExecuteServoMove((ArmMotor)currentMotor, targetAngle);
  if (success)
  {
    servoMoveIndex++;

    switch (servoMoveIndex)
    {
    case 1: // After shoulder default
      currentMotor = ArmMotor::GRIP;
      targetAngle = GRIP_OPEN;
      break;
    case 2:        // After grip
      return true; // Sequence complete
    }
  }
  return false; // Sequence not complete yet
}
void getAnglesForCup(int cupPosition, int angles[4])
{
  for (int i = 0; i < 4; i++)
  {
    angles[i] = cupAngleData[cupPosition][i];
  }
}

// Extract ball cup information from camera data
bool extractBallCup(int cameraData[], uint8_t count)
{
  if (count != 3)
  {
    // Serial.println("Invalid camera data count");
    return false;
  }

  // Set cup contents from camera data
  bool anyBallFound = false;
  for (int i = 0; i < 3; i++)
  {
    // Serial.println(cameraData[i]);
    if (cameraData[i] != 0)
    {
      anyBallFound = true;
      ballCup[i] = colors[cameraData[i]];
    }
  }

  return anyBallFound; // Return true if at least one cup has a ball
}

void cupsGameLoop()
{
  // Serial.println("CURRENT STATE : ");
  // Serial.println(currentState);
  // Serial.println("ARM STATE : ");
  // Serial.println(armState);

  if (gameEnded)
  {
    if (currentState != GAME_OVER)
    {
      currentState = GAME_OVER;
      armState = MOVE_COMPLETE;
      String resultMsg = "Game Over!";
      printOnLCD(resultMsg);
    }
    return;
  }

  unsigned long currentTime = millis();

  switch (currentState)
  {
  case GAME_INIT:
    // Initialize game state
    if (currentTime - stateStartTime > 500)
    {
      currentState = WAITING_FOR_DETECTION;
      stateStartTime = currentTime;
      printOnLCD("Waiting for cups...");
    }
    break;

  case WAITING_FOR_DETECTION:
    // Wait for camera to detect cups
    if (currentTime - stateStartTime > 4000)
    {
      //   Serial.println("Looking for ball position...");
      String res = getPythonData("cupsResult");

      if (res != "ERROR")
      {
        int cameraData[3];
        int count;
        parseCSV(res.c_str(), cameraData, count);

        if (extractBallCup(cameraData, count))
        {
          // Serial.println("Balls detected in cups:");

          // Reset the cup index counter for processing multiple cups
          currentCupIndex = 0;

          // Transition to the new state for handling multiple cups
          currentState = PROCESSING_MULTIPLE_CUPS;
          stateStartTime = currentTime;
          printOnLCD("Balls detected");
        }
        else
        {
          // Couldn't identify any balls, retry
          stateStartTime = currentTime + 1000;
          printOnLCD("No balls detected");
        }
      }
      else
      {
        // Serial.println("Camera failed");
        stateStartTime = currentTime + 1000;
        printOnLCD("Camera error!");
      }
    }
    break;

  case PROCESSING_MULTIPLE_CUPS:
    // Find the next cup with a ball
    while (currentCupIndex < 3 && ballCup[currentCupIndex] == "null")
    {
      currentCupIndex++;
    }

    // If we found a cup with a ball
    if (currentCupIndex < 3)
    {
      // Get cup name for display
      String cupName;

      // Set appropriate arm state based on cup position

      Serial.print("Ball with color : ");
      Serial.print(ballCup[currentCupIndex]);
      Serial.print(" under the ");
      Serial.println(cupsName[currentCupIndex]);
      // Serial.print(" cup: ");
      // Serial.println(ballCup[currentCupIndex]);

      // Setup angles for pointing to this cup
      getAnglesForCup(currentCupIndex, moveAngles);
      setupCupsServoMoveSequence(
          cupAngleData[currentCupIndex][0],
          cupAngleData[currentCupIndex][1],
          cupAngleData[currentCupIndex][2],
          cupAngleData[currentCupIndex][3],
          ArmMotor::GRIP,
          GRIP_OPEN);

      // Move to the pointing state
      currentState = PICK_CUP;
      stateStartTime = currentTime;
      printOnLCD(ballCup[currentCupIndex] + " in " + cupName);
    }
    else
    {
      // No more cups with balls, game is complete
      Serial.println("Game complete");

      // Set arm state to complete
      armState = MOVE_COMPLETE;

      // Reset current index
      currentCupIndex = 0;

      // Always end the game after one round
      gameEnded = true;
      currentState = GAME_OVER;
    }
    break;
  case PICK_CUP:
    if (currentTime - stateStartTime > 1000)
    {
      if (gripCups())
      {
        setupCupsServoMoveSequence(
            cupAngleData[currentCupIndex][0],
            cupAngleData[currentCupIndex][1],
            cupAngleData[currentCupIndex][2],
            cupAngleData[currentCupIndex][3],
            ArmMotor::SHOULDER,
            cupAngleData[currentCupIndex][1] + 1);

        currentState = DROP_CUP;
        stateStartTime = currentTime;
      }
    }
    break;
  case DROP_CUP:
    if (currentTime - stateStartTime > 5000)
    {
      if (dropCups())
      {
        setupCupsServoMoveSequence(
            retreatAngles[0],
            retreatAngles[1],
            retreatAngles[2],
            retreatAngles[3],
            ArmMotor::SHOULDER,
            DEFAULT_ANGLE_SHOULDER);

        currentState = ROBOT_RETREATING;
        stateStartTime = currentTime;
      }
    }
    break;
  case ROBOT_RETREATING:
    // Execute retreating sequence
    if (currentTime - stateStartTime > 1000)
    {
      if (retreatArm())
      {
        currentCupIndex++;
        currentState = PROCESSING_MULTIPLE_CUPS;
        stateStartTime = currentTime;
      }
    }
    break;
  case GAME_OVER:
    // Game has ended
    break;
  }
}

void stopCupsGame()
{
  Serial.println("Stopping Three Cups Game");
  changeConfig("none");
  gameEnded = true;
  currentState = GAME_OVER;
  armState = MOVE_IDLE; // Reset arm state

  // Final result
  String finalMessage = "Game completed";
  printOnLCD(finalMessage);
}
