#include "cups_game.h"
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

#define GRIP_CLOSED 72
#define GRIP_OPEN 100
#define DEFAULT_ANGLE_SHOULDER 90

// Define game states
enum GameState
{
  GAME_INIT,
  WAITING_FOR_DETECTION,
  POINTING_TO_CUP,
  PROCESSING_MULTIPLE_CUPS, // New state for handling multiple balls
  GAME_OVER
};

// States for the grabbing and releasing sequences
enum ArmMoveState
{
    MOVE_IDLE,
    PICK_LEFT,       // Added for cup positions
    PICK_MIDDLE,     // Added for cup positions
    PICK_RIGHT,      // Added for cup positions
    MOVE_COMPLETE
};

// Cup position angles for the arm to point to
const int cupAngleData[3][4] = {
    {120, 20, 80, 50},   // Left cup
    {108, 25, 85, 55},   // Middle cup
    {90, 20, 80, 50}     // Right cup
};

// State machine variables
static GameState currentState = GAME_INIT;
static ArmMoveState armState = MOVE_IDLE;  // Current arm movement state
static unsigned long stateStartTime = 0;
static unsigned long lastActionTime = 0;
static int servoMoveIndex = 0;
static int currentMotor = 0;
static int targetAngle = 0;
static int overShootValue = 0;
static String ballCup[3];      // Cup contents - can be "null" or a string like "red"
static int moveAngles[4] = {0};
static bool gameEnded = false;
static int roundCount = 0;
static int currentCupIndex = 0; // Used for processing multiple cups

void startCupsGame()
{
  Serial.println("Starting Three Cups Game");
  changeConfig("cups");

  gameEnded = false;
  // Initialize all cups to "null"
  for (int i = 0; i < 3; i++) {
    ballCup[i] = "null";
  }
  roundCount = 1; // Only one round
  currentCupIndex = 0;

  currentState = GAME_INIT;
  armState = MOVE_IDLE; // Initialize arm state
  stateStartTime = millis();
  
  printOnLCD("3 Cups Game Started");
}

bool cupsExecuteServoMove(ArmMotor motor, int angle, int overShoot)
{
  if (sendServoCommand(motor, angle, overShoot))
  {
    return true;
  }

  Serial.println("Servo command failed, will retry...");
  return false;
}

// State machine servo move sequence setup
void setupCupsServoMoveSequence(int baseAngle, int shoulderAngle, int elbowAngle, int wristAngle)
{
  servoMoveIndex = 0;
  moveAngles[0] = baseAngle;
  moveAngles[1] = shoulderAngle;
  moveAngles[2] = elbowAngle;
  moveAngles[3] = wristAngle;

  currentMotor = ArmMotor::SHOULDER;
  targetAngle = DEFAULT_ANGLE_SHOULDER;
  overShootValue = 10;
}

// Process one servo move step in the sequence
bool processCupsServoMoveStep()
{
  if (millis() - lastActionTime < 200)
  {
    return false; // Wait a little between commands
  }

  lastActionTime = millis();

  bool success = cupsExecuteServoMove((ArmMotor)currentMotor, targetAngle, overShootValue);
  if (success)
  {
    servoMoveIndex++;

    switch (servoMoveIndex)
    {
    case 1: // After shoulder default
      currentMotor = ArmMotor::BASE;
      targetAngle = moveAngles[0];
      overShootValue = 0;
      break;
    case 2: // After base
      currentMotor = ArmMotor::WRIST;
      targetAngle = moveAngles[3];
      overShootValue = 4;
      break;
    case 3: // After wrist
      currentMotor = ArmMotor::ELBOW;
      targetAngle = moveAngles[2];
      overShootValue = 0;
      break;
    case 4: // After elbow
      currentMotor = ArmMotor::SHOULDER;
      targetAngle = moveAngles[1];
      overShootValue = 10;
      break;
    case 5:        // After shoulder
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
    Serial.println("Invalid camera data count");
    return false;
  }
  
  // Set cup contents from camera data
  bool anyBallFound = false;
  for(int i = 0; i < 3; i++) {
    if(cameraData[i] != "null")
      anyBallFound = true;
    ballCup[i] = cameraData[i];
  }
  
  return anyBallFound; // Return true if at least one cup has a ball
}

void cupsGameLoop()
{
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
      Serial.println("Looking for ball position...");
      String res = getPythonData("cupsResult");

      if (res != "ERROR")
      {
        int cameraData[3];
        int count;
        parseCSV(res.c_str(), cameraData, count);

        if (extractBallCup(cameraData, count))
        {
          Serial.println("Balls detected in cups:");
          
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
        Serial.println("Camera failed");
        stateStartTime = currentTime + 1000;
        printOnLCD("Camera error!");
      }
    }
    break;

  case PROCESSING_MULTIPLE_CUPS:
    // Find the next cup with a ball
    while (currentCupIndex < 3 && ballCup[currentCupIndex] == "null") {
      currentCupIndex++;
    }
    
    // If we found a cup with a ball
    if (currentCupIndex < 3) {
      // Get cup name for display
      String cupName;
      
      // Set appropriate arm state based on cup position
      switch(currentCupIndex) {
        case 0: // Left cup
          cupName = "Left";
          armState = PICK_LEFT;
          break;
        case 1: // Middle cup
          cupName = "Middle";
          armState = PICK_MIDDLE;
          break;
        case 2: // Right cup
          cupName = "Right";
          armState = PICK_RIGHT;
          break;
      }
      
      Serial.print("Ball found in ");
      Serial.print(cupName);
      Serial.print(" cup: ");
      Serial.println(ballCup[currentCupIndex]);
      
      // Setup angles for pointing to this cup
      getAnglesForCup(currentCupIndex, moveAngles);
      setupCupsServoMoveSequence(
          moveAngles[0],
          moveAngles[1],
          moveAngles[2],
          moveAngles[3]);
      
      // Move to the pointing state
      currentState = POINTING_TO_CUP;
      stateStartTime = currentTime;
      printOnLCD(ballCup[currentCupIndex] + " in " + cupName);
    }
    else {
      // No more cups with balls, game is complete
      Serial.println("Game complete");
      
      // Set arm state to complete
      armState = MOVE_COMPLETE;
      
      // Reset current index
      currentCupIndex = 0;
      
      // Return to default position
      delay(1000);
      setupCupsServoMoveSequence(108, DEFAULT_ANGLE_SHOULDER, 85, 55);
      
      // Process servo move to default position
      bool moveComplete = false;
      while (!moveComplete) {
        moveComplete = processCupsServoMoveStep();
        delay(200);
      }
      
      // Always end the game after one round
      gameEnded = true;
      currentState = GAME_OVER;
    }
    break;

  case POINTING_TO_CUP:
    // Execute pointing sequence for current cup
    if (processCupsServoMoveStep())
    {
      // Pointing complete - display for a moment
      String cupName;
      
      // Use the already set armState to determine which cup
      switch(armState) {
        case PICK_LEFT:
          cupName = "Left Cup";
          break;
        case PICK_MIDDLE:
          cupName = "Middle Cup";
          break;
        case PICK_RIGHT:
          cupName = "Right Cup";
          break;
        default:
          cupName = "Unknown";
          break;
      }
      
      // Display cup contents on LCD
      printOnLCD(ballCup[currentCupIndex] + " in " + cupName);
      
      // Wait a moment to show the result
      delay(2000);
      
      // Mark current cup as processed and move to next
      currentCupIndex++;
      armState = MOVE_IDLE; // Reset arm state
      currentState = PROCESSING_MULTIPLE_CUPS;
      stateStartTime = currentTime;
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