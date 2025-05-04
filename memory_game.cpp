#include "memory_game.h"
#include "game_utils.h"
#include <Arduino.h>
#include <stdlib.h>
#include <ctime>

extern void changeConfig(String command);
extern String getPythonData(String command);
extern void parseCSV(const char *csv, int arr[], int &count);
extern bool sendServoCommand(int a1, int a2, int a3);
extern bool sendStepperCommand(const int cmds[]);
extern void printOnLCD(const String &msg);

#define ROWS 2
#define COLS 3
#define CELLS_CNT 6
#define SHAPES 3
#define GRIP_OPEN 120
#define GRIP_CLOSED 60
#define DEFAULT_ANGLE_SHOULDER 100

class Position {
  public:
      int base;
      int shoulder;
      int elbow;
      int wrist;
  
      Position(float base, float shoulder, float elbow, float wrist) {
          this->base = base;
          this->shoulder = shoulder;
          this->elbow = elbow;
          this->wrist = wrist;
      }
};

Position cell_0(92, 20, 75, 40);
Position cell_1(110, 28, 90, 45);
Position cell_2(122, 20, 75, 40);
Position cell_3(91, 50, 130, 60);
Position cell_4(111, 55, 135, 60);
Position cell_5(130, 50, 130, 60);
Position cell_6(84, 85, 168, 65);
Position cell_7(147, 76, 165, 70);
Position cell_8(30, 85, 168, 65); // dump position

Position getPosition(int idx) {
  switch (idx) {
      case 0: return cell_0;
      case 1: return cell_1;
      case 2: return cell_2;
      case 3: return cell_3;
      case 4: return cell_4;
      case 5: return cell_5;
      case 6: return cell_6;
      case 7: return cell_7;
      case 8: return cell_8;
      default: return cell_8;
  }
}

void memoryExecuteServoMove(ArmMotor motor, int angle, int overShoot)
{
  while (true)
  {
    if (sendServoCommand(motor, angle, overShoot))
      break;

    Serial.println("Servo command failed, retrying...");

    delay(500);
  }
}

void memoryGoGrab(int requiredBaseAngle, int requiredShoulderAngle, int requiredElbowAngle, int requiredWristAngle) {
  memoryExecuteServoMove(ArmMotor::GRIP, GRIP_OPEN, 0);
  memoryExecuteServoMove(ArmMotor::SHOULDER, DEFAULT_ANGLE_SHOULDER, 10);
  memoryExecuteServoMove(ArmMotor::BASE, requiredBaseAngle, 0);
  memoryExecuteServoMove(ArmMotor::WRIST, requiredWristAngle, 4);
  memoryExecuteServoMove(ArmMotor::ELBOW, requiredElbowAngle, 0);
  memoryExecuteServoMove(ArmMotor::SHOULDER, requiredShoulderAngle,0);
  memoryExecuteServoMove(ArmMotor::GRIP, GRIP_CLOSED, 0);
}

void memoryGoRelease(int requiredBaseAngle, int requiredShoulderAngle, int requiredElbowAngle, int requiredWristAngle)
{
    memoryExecuteServoMove(ArmMotor::SHOULDER, DEFAULT_ANGLE_SHOULDER, 10);
    memoryExecuteServoMove(ArmMotor::ELBOW, 140, 0);
    memoryExecuteServoMove(ArmMotor::BASE, requiredBaseAngle, 0);
    memoryExecuteServoMove(ArmMotor::WRIST, 45, 15);
    memoryExecuteServoMove(ArmMotor::ELBOW, requiredElbowAngle, 0);
    memoryExecuteServoMove(ArmMotor::WRIST, 55, 0);
    memoryExecuteServoMove(ArmMotor::SHOULDER, requiredShoulderAngle + 10,0);
    memoryExecuteServoMove(ArmMotor::WRIST, requiredWristAngle, 0);
    memoryExecuteServoMove(ArmMotor::SHOULDER, requiredShoulderAngle,0);
    memoryExecuteServoMove(ArmMotor::GRIP, GRIP_OPEN, 0);
}

void moveBlockFromTo(int idx1, int idx2) {
  Position src = getPosition(idx1);
  Position dest = getPosition(idx2);
  memoryGoGrab(src.base, src.shoulder, src.elbow, src.wrist);
  memoryGoRelease(dest.base, dest.shoulder, dest.elbow, dest.wrist);
  delay(500);
}

int complete = 0;  // Counter to track the number of completed shapes.


int currentMatrixState[ROWS][COLS];  // Initialize the previous state of the matrix with -1 (indicating no previous occurrences).

int prev[SHAPES][3];  // Initialize the previous state of the matrix with -1

int extraPositionsState[2];

int out;  //Index of the next empty out // look at the pic. in memory chat elsultan
/////////////////////////////
//Functions

void initializeGameState() {
  complete = 0;

  // Reset currentMatrixState to -1
  for (int i = 0; i < ROWS; i++) {
    for (int j = 0; j < COLS; j++) {
      currentMatrixState[i][j] = -1;
    }
  }

  // Reset prev array
  for (int i = 0; i < SHAPES; i++) {
    prev[i][0] = -1;
    prev[i][1] = -1;
    prev[i][2] = 0;
  }

  // Reset extraPositionsState to -1
  for (int i = 0; i < 2; i++) {
    extraPositionsState[i] = -1;
  }

  // Reset out index
  out = 8;
}


int pickRandomCell(int currentMatrixState[][COLS]) {
  // int rnd = random(CELLS_CNT); // Arduino has random()
  int rnd = rand()%CELLS_CNT; // Use rand() for C++ standard library
  while (currentMatrixState[rnd % ROWS][rnd % COLS] != -1) {
    rnd = rand()%CELLS_CNT;
  }
  return rnd;
}

//Checks if any shape has been found twice.
int findShapeWithTwoOccurrences(int prev[SHAPES][3]) {
  if (prev[0][2] == 2) return 0;       // if the first shape was found
  else if (prev[1][2] == 2) return 1;  // if the second shape was found
  else if (prev[2][2] == 2) return 2;  // if the third shape was found
  return -1;                           // if no shape was found
}

// Marks a shape as found and increments the complete counter.
void markShapeAsComplete(int fnd, int prev[SHAPES][3], int& complete) {
  prev[fnd][2] = 3;
  complete++;
}

// Checks if a shape has been found for the first time.
bool hasShapeBeenFoundBefore(int prev[SHAPES][3], int cur) {
  return prev[cur][0] != -1;
}

void convetStringTo2D(String input, int length, int outputArray[2][3]) {  // Specify COLS
  int arr[CELLS_CNT]; 
  int count = 0;
  parseCSV(input.c_str(), arr, count);
  if(count > 0){
    for(int i = 0; i < length; i++) {
      outputArray[i % ROWS][i / COLS] = arr[i];
    }
  }
}

// Fixed module function
void module(int outputArray[2][3]) {  // Pass array by reference
  String values = getPythonData("memory");  // Assuming this function returns a String
  
  convetStringTo2D(values,  CELLS_CNT, outputArray);  
}

// Fixed reveal function
int reveal(int cell) {
  if (extraPositionsState[0] == -1) {
    moveBlockFromTo(cell, 6);
    extraPositionsState[0] = cell;
  } else {
    moveBlockFromTo(cell, 7);
    extraPositionsState[1] = cell;
  }

  int matrixFromCamera[2][3]; 
  module(matrixFromCamera);    
  currentMatrixState[cell % ROWS][cell % COLS]=matrixFromCamera[cell % ROWS][cell % COLS];
  return currentMatrixState[cell % ROWS][cell % COLS];
}

void dump() {
  if (extraPositionsState[1] != -1) moveBlockFromTo(7, extraPositionsState[1]);
  if (extraPositionsState[0] != -1) moveBlockFromTo(6, extraPositionsState[0]);
  extraPositionsState[0] = -1;
  extraPositionsState[1] = -1;
}



void startMemoryGame()
{
  Serial.println("Starting Memory Game");
  changeConfig("memory");
  srand(time(nullptr));
  initializeGameState(); 
}

void memoryGameLoop()
{
  if (complete < SHAPES) {
    int shapeWithTwoOccs = findShapeWithTwoOccurrences(prev);
    Serial.print("Shape with two occurrences: ");
    Serial.println(shapeWithTwoOccs);

    bool fnd1 = false;

    if (shapeWithTwoOccs != -1) {
        Serial.println("Entered shapeWithTwoOccs block");
        markShapeAsComplete(shapeWithTwoOccs, prev, complete);
        fnd1 = true;
        Serial.print("Moving block from: ");
        Serial.println(prev[shapeWithTwoOccs][0]);
        moveBlockFromTo(prev[shapeWithTwoOccs][0], out);
        Serial.print("Moving block from: ");
        Serial.println(prev[shapeWithTwoOccs][1]);
        moveBlockFromTo(prev[shapeWithTwoOccs][1], out);
    }

    bool fnd2 = false;

    if (!fnd1) {
        Serial.println("No shape with two occurrences found, picking random cell...");
        int rnd1 = pickRandomCell(currentMatrixState);
        Serial.print("Random cell 1 picked: ");
        Serial.println(rnd1);

        int cur1 = reveal(rnd1);
        Serial.print("Reveal result for rnd1: ");
        Serial.println(cur1);

        if (hasShapeBeenFoundBefore(prev, cur1)) {
            Serial.println("Shape already found before (cur1)");
            prev[cur1][1] = rnd1;
            prev[cur1][2]++;

            markShapeAsComplete(cur1, prev, complete);

            Serial.print("Moving block from (cur1): ");
            Serial.println(prev[cur1][0]);
            extraPositionsState[0] = out;
            dump();
            moveBlockFromTo(prev[cur1][0], out);

            fnd2 = true;
        } else {
            Serial.println("First time finding this shape (cur1)");
            prev[cur1][0] = rnd1;
            prev[cur1][2]++;
        }

        if (!fnd2) {
            Serial.println("Still no match, picking second random cell...");
            int rnd2 = pickRandomCell(currentMatrixState);
            Serial.print("Random cell 2 picked: ");
            Serial.println(rnd2);

            int cur2 = reveal(rnd2);
            Serial.print("Reveal result for rnd2: ");
            Serial.println(cur2);

            if (hasShapeBeenFoundBefore(prev, cur2)) {
                Serial.println("Shape already found before (cur2)");
                prev[cur2][1] = rnd2;
                prev[cur2][2]++;

                if (cur1 == cur2) {
                    Serial.println("cur1 and cur2 match — marking extraPositionsState");
                    markShapeAsComplete(cur1, prev, complete);
                    extraPositionsState[0] = out;
                    extraPositionsState[1] = out;
                }
            } else {
                Serial.println("First time finding this shape (cur2)");
                prev[cur2][0] = rnd2;
                prev[cur2][2]++;
            }
        }
    }
    Serial.println("Calling dump()");
    dump();
  }
}

void stopMemoryGame()
{
  Serial.println("Stopping Memory Game");
  changeConfig("none");

}