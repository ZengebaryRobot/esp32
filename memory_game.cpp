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

// State machine states for the memory game
enum MemoryGameState
{
    GAME_IDLE,
    GAME_INIT,
    GAME_FIND_MATCH,
    GAME_PICK_RANDOM1,
    GAME_REVEAL1,
    GAME_PICK_RANDOM2,
    GAME_REVEAL2,
    GAME_COMPLETE_MATCH,
    GAME_DUMP,
    GAME_COMPLETED
};

// States for the grabbing and releasing sequences
enum ArmMoveState
{
    MOVE_IDLE,
    GRAB_OPEN_GRIP,
    GRAB_SHOULDER_DEFAULT,
    GRAB_SET_BASE,
    GRAB_SET_WRIST,
    GRAB_SET_ELBOW,
    GRAB_SET_SHOULDER,
    GRAB_CLOSE_GRIP,
    RELEASE_SHOULDER_DEFAULT,
    RELEASE_SET_ELBOW_INTERIM,
    RELEASE_SET_BASE,
    RELEASE_SET_WRIST_INTERIM,
    RELEASE_SET_ELBOW,
    RELEASE_SET_WRIST_MID,
    RELEASE_SET_SHOULDER_UP,
    RELEASE_SET_WRIST_FINAL,
    RELEASE_SET_SHOULDER_FINAL,
    RELEASE_OPEN_GRIP,
    MOVE_COMPLETE
};

class Position
{
public:
    int base;
    int shoulder;
    int elbow;
    int wrist;
    Position(float base, float shoulder, float elbow, float wrist)
    {
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

// Global state variables
MemoryGameState gameState = GAME_IDLE;
ArmMoveState armState = MOVE_IDLE;
unsigned long lastStateChangeTime = 0;
const unsigned long STATE_DELAY = 100; // Minimum delay between state transitions

// Move operation state variables
int srcIdx = -1;
int destIdx = -1;


Position currentSrc(0,0,0,0);
Position currentDest(0,0,0,0);

// Game state variables
int complete = 0; // Counter to track the number of completed shapes
int currentMatrixState[ROWS][COLS];
int prev[SHAPES][3];
int extraPositionsState[2];
int out = 8; // Index of the next empty out
int rnd1 = -1;
int rnd2 = -1;
int cur1 = -1;
int cur2 = -1;
int shapeWithTwoOccs = -1;
bool fnd1 = false;
bool fnd2 = false;

Position getPosition(int idx)
{
    switch (idx)
    {
    case 0:
        return cell_0;
    case 1:
        return cell_1;
    case 2:
        return cell_2;
    case 3:
        return cell_3;
    case 4:
        return cell_4;
    case 5:
        return cell_5;
    case 6:
        return cell_6;
    case 7:
        return cell_7;
    case 8:
        return cell_8;
    default:
        return cell_8;
    }
}

bool executeServoMoveNonBlocking(ArmMotor motor, int angle, int overShoot)
{
    static unsigned long lastServoAttemptTime = 0;
    const unsigned long SERVO_RETRY_DELAY = 500; // ms

    unsigned long currentTime = millis();
    if (currentTime - lastServoAttemptTime < SERVO_RETRY_DELAY)
    {
        return false; // Not enough time has passed for retry
    }

    bool success = sendServoCommand(motor, angle, overShoot);
    lastServoAttemptTime = currentTime;

    return success;
}

void startMoveOperation(int from, int to)
{
    srcIdx = from;
    destIdx = to;
    currentSrc = getPosition(from);
    currentDest = getPosition(to);
    armState = GRAB_OPEN_GRIP;
    Serial.print("Starting move from ");
    Serial.print(from);
    Serial.print(" to ");
    Serial.println(to);
}

bool updateArmMove()
{
    static unsigned long lastAttemptTime = 0;
    const unsigned long RETRY_DELAY = 100; // ms

    // Ensure we don't update states too rapidly
    unsigned long currentTime = millis();
    if (currentTime - lastAttemptTime < RETRY_DELAY)
    {
        return false;
    }
    lastAttemptTime = currentTime;

    bool stateCompleted = false;

    switch (armState)
    {
    // Grab sequence states
    case GRAB_OPEN_GRIP:
        stateCompleted = executeServoMoveNonBlocking(ArmMotor::GRIP, GRIP_OPEN, 0);
        if (stateCompleted)
            armState = GRAB_SHOULDER_DEFAULT;
        break;

    case GRAB_SHOULDER_DEFAULT:
        stateCompleted = executeServoMoveNonBlocking(ArmMotor::SHOULDER, DEFAULT_ANGLE_SHOULDER, 10);
        if (stateCompleted)
            armState = GRAB_SET_BASE;
        break;

    case GRAB_SET_BASE:
        stateCompleted = executeServoMoveNonBlocking(ArmMotor::BASE, currentSrc.base, 0);
        if (stateCompleted)
            armState = GRAB_SET_WRIST;
        break;

    case GRAB_SET_WRIST:
        stateCompleted = executeServoMoveNonBlocking(ArmMotor::WRIST, currentSrc.wrist, 4);
        if (stateCompleted)
            armState = GRAB_SET_ELBOW;
        break;

    case GRAB_SET_ELBOW:
        stateCompleted = executeServoMoveNonBlocking(ArmMotor::ELBOW, currentSrc.elbow, 0);
        if (stateCompleted)
            armState = GRAB_SET_SHOULDER;
        break;

    case GRAB_SET_SHOULDER:
        stateCompleted = executeServoMoveNonBlocking(ArmMotor::SHOULDER, currentSrc.shoulder, 0);
        if (stateCompleted)
            armState = GRAB_CLOSE_GRIP;
        break;

    case GRAB_CLOSE_GRIP:
        stateCompleted = executeServoMoveNonBlocking(ArmMotor::GRIP, GRIP_CLOSED, 0);
        if (stateCompleted)
            armState = RELEASE_SHOULDER_DEFAULT;
        break;

    // Release sequence states
    case RELEASE_SHOULDER_DEFAULT:
        stateCompleted = executeServoMoveNonBlocking(ArmMotor::SHOULDER, DEFAULT_ANGLE_SHOULDER, 10);
        if (stateCompleted)
            armState = RELEASE_SET_ELBOW_INTERIM;
        break;

    case RELEASE_SET_ELBOW_INTERIM:
        stateCompleted = executeServoMoveNonBlocking(ArmMotor::ELBOW, 140, 0);
        if (stateCompleted)
            armState = RELEASE_SET_BASE;
        break;

    case RELEASE_SET_BASE:
        stateCompleted = executeServoMoveNonBlocking(ArmMotor::BASE, currentDest.base, 0);
        if (stateCompleted)
            armState = RELEASE_SET_WRIST_INTERIM;
        break;

    case RELEASE_SET_WRIST_INTERIM:
        stateCompleted = executeServoMoveNonBlocking(ArmMotor::WRIST, 45, 15);
        if (stateCompleted)
            armState = RELEASE_SET_ELBOW;
        break;

    case RELEASE_SET_ELBOW:
        stateCompleted = executeServoMoveNonBlocking(ArmMotor::ELBOW, currentDest.elbow, 0);
        if (stateCompleted)
            armState = RELEASE_SET_WRIST_MID;
        break;

    case RELEASE_SET_WRIST_MID:
        stateCompleted = executeServoMoveNonBlocking(ArmMotor::WRIST, 55, 0);
        if (stateCompleted)
            armState = RELEASE_SET_SHOULDER_UP;
        break;

    case RELEASE_SET_SHOULDER_UP:
        stateCompleted = executeServoMoveNonBlocking(ArmMotor::SHOULDER, currentDest.shoulder + 10, 0);
        if (stateCompleted)
            armState = RELEASE_SET_WRIST_FINAL;
        break;

    case RELEASE_SET_WRIST_FINAL:
        stateCompleted = executeServoMoveNonBlocking(ArmMotor::WRIST, currentDest.wrist, 0);
        if (stateCompleted)
            armState = RELEASE_SET_SHOULDER_FINAL;
        break;

    case RELEASE_SET_SHOULDER_FINAL:
        stateCompleted = executeServoMoveNonBlocking(ArmMotor::SHOULDER, currentDest.shoulder, 0);
        if (stateCompleted)
            armState = RELEASE_OPEN_GRIP;
        break;

    case RELEASE_OPEN_GRIP:
        stateCompleted = executeServoMoveNonBlocking(ArmMotor::GRIP, GRIP_OPEN, 0);
        if (stateCompleted)
        {
            armState = MOVE_COMPLETE;
            Serial.println("Move operation completed");
            return true; // Move operation is complete
        }
        break;

    case MOVE_IDLE:
    case MOVE_COMPLETE:
        return true; // Nothing to do, consider it complete

    default:
        Serial.println("Unknown arm state");
        armState = MOVE_IDLE;
        return true;
    }

    return false; // Move operation is still in progress
}

void initializeGameState()
{
    complete = 0;

    // Reset currentMatrixState to -1
    for (int i = 0; i < ROWS; i++)
    {
        for (int j = 0; j < COLS; j++)
        {
            currentMatrixState[i][j] = -1;
        }
    }

    // Reset prev array
    for (int i = 0; i < SHAPES; i++)
    {
        prev[i][0] = -1;
        prev[i][1] = -1;
        prev[i][2] = 0;
    }

    // Reset extraPositionsState to -1
    for (int i = 0; i < 2; i++)
    {
        extraPositionsState[i] = -1;
    }

    // Reset out index
    out = 8;

    // Reset game state variables
    rnd1 = -1;
    rnd2 = -1;
    cur1 = -1;
    cur2 = -1;
    shapeWithTwoOccs = -1;
    fnd1 = false;
    fnd2 = false;
}

int pickRandomCell(int currentMatrixState[][COLS])
{
    int rnd = rand() % CELLS_CNT;
    while (currentMatrixState[rnd / COLS][rnd % COLS] != -1)
    {
        rnd = rand() % CELLS_CNT;
    }
    return rnd;
}

int findShapeWithTwoOccurrences(int prev[SHAPES][3])
{
    if (prev[0][2] == 2)
        return 0; // if the first shape was found
    else if (prev[1][2] == 2)
        return 1; // if the second shape was found
    else if (prev[2][2] == 2)
        return 2; // if the third shape was found
    return -1;    // if no shape was found
}

void markShapeAsComplete(int fnd, int prev[SHAPES][3], int &complete)
{
    prev[fnd][2] = 3;
    complete++;
}

bool hasShapeBeenFoundBefore(int prev[SHAPES][3], int cur)
{
    return prev[cur][0] != -1;
}

void convetStringTo2D(String input, int length, int outputArray[2][3])
{
    int arr[CELLS_CNT];
    int count = 0;
    parseCSV(input.c_str(), arr, count);
    if (count > 0)
    {
        for (int i = 0; i < length; i++)
        {
            outputArray[i / COLS][i % COLS] = arr[i];
        }
    }
}

void module(int outputArray[2][3])
{
    String values = getPythonData("memory");
    convetStringTo2D(values, CELLS_CNT, outputArray);
}

int reveal(int cell)
{
    int row = cell / COLS;
    int col = cell % COLS;

    if (extraPositionsState[0] == -1)
    {
        startMoveOperation(cell, 6);
        extraPositionsState[0] = cell;
    }
    else
    {
        startMoveOperation(cell, 7);
        extraPositionsState[1] = cell;
    }

    int matrixFromCamera[2][3];
    module(matrixFromCamera);
    currentMatrixState[row][col] = matrixFromCamera[row][col];
    return currentMatrixState[row][col];
}

void startDump()
{
    if (extraPositionsState[1] != -1)
    {
        startMoveOperation(7, extraPositionsState[1]);
        extraPositionsState[1] = -1;
    }
    else if (extraPositionsState[0] != -1)
    {
        startMoveOperation(6, extraPositionsState[0]);
        extraPositionsState[0] = -1;
    }
    else
    {
        armState = MOVE_COMPLETE; // Nothing to dump
    }
}

void startMemoryGame()
{
    Serial.println("Starting Memory Game");
    changeConfig("memory");
    srand(time(nullptr));
    initializeGameState();
    gameState = GAME_INIT;
    printOnLCD("Memory Game Started");
}

void memoryGameLoop()
{
    unsigned long currentTime = millis();

    // Don't update states too rapidly
    if (currentTime - lastStateChangeTime < STATE_DELAY)
    {
        return;
    }

    // First, check if we're in the middle of an arm movement
    if (armState != MOVE_IDLE && armState != MOVE_COMPLETE)
    {
        bool moveCompleted = updateArmMove();
        if (!moveCompleted)
        {
            return; // Still moving, don't change game state
        }
        armState = MOVE_IDLE; // Reset arm state once complete
    }

    // Now handle game state transitions
    switch (gameState)
    {
    case GAME_IDLE:
        // Do nothing in idle state
        break;

    case GAME_INIT:
        // Initialize the game
        initializeGameState();
        gameState = GAME_FIND_MATCH;
        lastStateChangeTime = currentTime;
        break;

    case GAME_FIND_MATCH:
        if (complete >= SHAPES)
        {
            gameState = GAME_COMPLETED;
            printOnLCD("Game Completed!");
        }
        else
        {
            // Check if any shape has been found twice
            shapeWithTwoOccs = findShapeWithTwoOccurrences(prev);
            Serial.print("Shape with two occurrences: ");
            Serial.println(shapeWithTwoOccs);

            if (shapeWithTwoOccs != -1)
            {
                // A shape has been found twice, mark it as complete
                Serial.println("Entered shapeWithTwoOccs block");
                markShapeAsComplete(shapeWithTwoOccs, prev, complete);
                fnd1 = true;

                // Start moving the first piece to out position
                Serial.print("Moving block from: ");
                Serial.println(prev[shapeWithTwoOccs][0]);
                startMoveOperation(prev[shapeWithTwoOccs][0], out);
                gameState = GAME_COMPLETE_MATCH;
            }
            else
            {
                fnd1 = false;
                gameState = GAME_PICK_RANDOM1;
            }
        }
        lastStateChangeTime = currentTime;
        break;

    case GAME_COMPLETE_MATCH:
        if (armState == MOVE_IDLE)
        {
            // If first move is done, start second move
            Serial.print("Moving block from: ");
            Serial.println(prev[shapeWithTwoOccs][1]);
            startMoveOperation(prev[shapeWithTwoOccs][1], out);
            gameState = GAME_DUMP;
        }
        lastStateChangeTime = currentTime;
        break;

    case GAME_PICK_RANDOM1:
        rnd1 = pickRandomCell(currentMatrixState);
        Serial.print("Random cell 1 picked: ");
        Serial.println(rnd1);
        gameState = GAME_REVEAL1;
        lastStateChangeTime = currentTime;
        break;

    case GAME_REVEAL1:
        if (armState == MOVE_IDLE)
        {
            cur1 = reveal(rnd1);
            Serial.print("Reveal result for rnd1: ");
            Serial.println(cur1);

            if (hasShapeBeenFoundBefore(prev, cur1))
            {
                Serial.println("Shape already found before (cur1)");
                prev[cur1][1] = rnd1;
                prev[cur1][2]++;

                markShapeAsComplete(cur1, prev, complete);

                Serial.print("Moving block from (cur1): ");
                Serial.println(prev[cur1][0]);
                extraPositionsState[0] = out;
                gameState = GAME_DUMP;
                fnd2 = true;
            }
            else
            {
                Serial.println("First time finding this shape (cur1)");
                prev[cur1][0] = rnd1;
                prev[cur1][2]++;
                gameState = GAME_PICK_RANDOM2;
            }
            lastStateChangeTime = currentTime;
        }
        break;

    case GAME_PICK_RANDOM2:
        rnd2 = pickRandomCell(currentMatrixState);
        Serial.print("Random cell 2 picked: ");
        Serial.println(rnd2);
        gameState = GAME_REVEAL2;
        lastStateChangeTime = currentTime;
        break;

    case GAME_REVEAL2:
        if (armState == MOVE_IDLE)
        {
            cur2 = reveal(rnd2);
            Serial.print("Reveal result for rnd2: ");
            Serial.println(cur2);

            if (hasShapeBeenFoundBefore(prev, cur2))
            {
                Serial.println("Shape already found before (cur2)");
                prev[cur2][1] = rnd2;
                prev[cur2][2]++;

                if (cur1 == cur2)
                {
                    Serial.println("cur1 and cur2 match — marking extraPositionsState");
                    markShapeAsComplete(cur1, prev, complete);
                    extraPositionsState[0] = out;
                    extraPositionsState[1] = out;
                }
            }
            else
            {
                Serial.println("First time finding this shape (cur2)");
                prev[cur2][0] = rnd2;
                prev[cur2][2]++;
            }

            gameState = GAME_DUMP;
            lastStateChangeTime = currentTime;
        }
        break;

    case GAME_DUMP:
        if (armState == MOVE_IDLE)
        {
            Serial.println("Starting dump operation");
            startDump();
            if (armState == MOVE_COMPLETE)
            {
                // If nothing to dump, go directly to finding next match
                gameState = GAME_FIND_MATCH;
            }
            else
            {
                // We'll come back to this state after the move is complete
            }
            lastStateChangeTime = currentTime;
        }
        else if (armState == MOVE_COMPLETE)
        {
            if (extraPositionsState[0] == -1 && extraPositionsState[1] == -1)
            {
                // Dump is complete
                gameState = GAME_FIND_MATCH;
                lastStateChangeTime = currentTime;
            }
            else
            {
                // Continue dumping
                startDump();
            }
        }
        break;

    case GAME_COMPLETED:
        // Game is complete, do nothing
        break;

    default:
        Serial.println("Unknown game state");
        gameState = GAME_IDLE;
        break;
    }
}

void stopMemoryGame()
{
    Serial.println("Stopping Memory Game");
    changeConfig("none");
    gameState = GAME_IDLE;
    armState = MOVE_IDLE;
    printOnLCD("Memory Game Stopped");
}
