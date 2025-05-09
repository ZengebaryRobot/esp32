#include "xo_game.h"
#include "game_utils.h"
#include <Arduino.h>

extern void changeConfig(String command);
extern String getPythonData(String command);
extern void parseCSV(const char *csv, int arr[], int &count);
extern bool sendServoCommand(int a1, int a2, int a3);
extern bool sendStepperCommand(const int cmds[]);
extern void printOnLCD(const String &msg);

// Define board values
#define EMPTY 0
#define PLAYER_X 1
#define PLAYER_O 2

#define GRIP_CLOSED 75
#define GRIP_OPEN 105
#define DEFAULT_ANGLE_SHOULDER 90

// Define game states
enum GameState
{
  GAME_INIT,
  ROBOT_THINKING,
  ROBOT_GRABBING,
  ROBOT_PLACING,
  ROBOT_RETREATING,
  WAITING_FOR_PLAYER,
  CAPTURING_BOARD,
  GAME_OVER
};

int board[3][3] = {
    {EMPTY, EMPTY, EMPTY},
    {EMPTY, EMPTY, EMPTY},
    {EMPTY, EMPTY, EMPTY}};
int lastBoard[3][3] = {
    {EMPTY, EMPTY, EMPTY},
    {EMPTY, EMPTY, EMPTY},
    {EMPTY, EMPTY, EMPTY}};

const int angleData[3][3][4] = {
    {{118, 12, 70, 47}, {108, 18, 80, 52}, {94, 16, 75, 50}},
    {{122, 35, 112, 55}, {108, 43, 126, 70}, {93, 38, 116, 60}},
    {{128, 60, 150, 74}, {108, 60, 150, 68}, {89, 61, 150, 77}}};

const int stackAngleData[5][4] = {
  {79, 25, 92, 48},
  {79, 26, 90, 44},
  {79, 30, 90, 44},
  {79, 32, 87, 40},
  {79, 35, 87, 40}
};
// Retreat position angles
const int retreatAngles[4] = {90, 90, 90, 90};

// Structure to store move coordinates and score
typedef struct
{
  int row;
  int col;
  int score;
} Move;

static int stackCounter = 4;
static int turn = PLAYER_X;
static bool gameEnded = false;

// State machine variables
static GameState currentState = GAME_INIT;
static unsigned long stateStartTime = 0;
static unsigned long lastActionTime = 0;
static int servoMoveIndex = 0;
static int currentMotor = 0;
static int targetAngle = 0;
static int overShootValue = 0;
static Move robotMove = {-1, -1, 0};
static int moveAngles[4] = {0};

bool xoExecuteServoMove(ArmMotor motor, int angle, int overShoot)
{
  if (sendServoCommand(motor, angle, overShoot))
  {
    return true;
  }

  Serial.println("Servo command failed, will retry...");
  return false;
}

void startXOGame()
{
  Serial.println("Starting XO Game");
  changeConfig("xo");

  gameEnded = false;
  turn = PLAYER_X;
  stackCounter = 4;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      board[i][j] = EMPTY;

  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      lastBoard[i][j] = EMPTY;

  currentState = GAME_INIT;
  stateStartTime = millis();
}



// State machine servo move sequence setup
void setupServoMoveSequence(int baseAngle, int shoulderAngle, int elbowAngle, int wristAngle)
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
bool processServoMoveStep()
{
  if (millis() - lastActionTime < 200)
  {
    return false; // Wait a little between commands
  }

  lastActionTime = millis();

  bool success = xoExecuteServoMove((ArmMotor)currentMotor, targetAngle, overShootValue);
  if (success)
  {
    servoMoveIndex++;

    switch (servoMoveIndex)
    {
    case 1: // After shoulder default
      currentMotor = ArmMotor::BASE;
      targetAngle = moveAngles[0];
      overShootValue = 0; // No overshoot to any other servo
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
      targetAngle =  moveAngles[1];
      break;
    case 5: // After shoulder
      currentMotor = ArmMotor::GRIP;
      targetAngle = currentState == ROBOT_GRABBING ? GRIP_CLOSED : GRIP_OPEN;
      break;
    case 6:        // After grip
      return true; // Sequence complete
    }
  }
  return false; // Sequence not complete yet
}

void getAnglesForCell(int x, int y, int angles[4])
{
  for (int i = 0; i < 4; i++)
  {
    angles[i] = angleData[x][y][i];
  }
}

// Function to check if the board is full
bool isBoardFull()
{
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      if (board[i][j] == EMPTY)
        return false;
  return true;
}

int evaluateResult(int player, int opponent)
{
  for (int row = 0; row < 3; row++)
  {
    if (board[row][0] == board[row][1] && board[row][1] == board[row][2])
    {
      if (board[row][0] == player)
        return 10;
      else if (board[row][0] == opponent)
        return -10;
    }
  }

  for (int col = 0; col < 3; col++)
  {
    if (board[0][col] == board[1][col] && board[1][col] == board[2][col])
    {
      if (board[0][col] == player)
        return 10;
      else if (board[0][col] == opponent)
        return -10;
    }
  }

  if (board[0][0] == board[1][1] && board[1][1] == board[2][2])
  {
    if (board[0][0] == player)
      return 10;
    else if (board[0][0] == opponent)
      return -10;
  }

  if (board[0][2] == board[1][1] && board[1][1] == board[2][0])
  {
    if (board[0][2] == player)
      return 10;
    else if (board[0][2] == opponent)
      return -10;
  }

  return 0;
}

Move findBestMove()
{
  Move bestMove = {-1, -1, 0};

  // Try to win
  for (int i = 0; i < 3; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      if (board[i][j] == EMPTY)
      {
        board[i][j] = PLAYER_X;
        if (evaluateResult(PLAYER_X, PLAYER_O) == 10)
        {
          board[i][j] = EMPTY;
          return {i, j, 10};
        }
        board[i][j] = EMPTY;
      }
    }
  }

  // Try to block opponent from winning
  for (int i = 0; i < 3; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      if (board[i][j] == EMPTY)
      {
        board[i][j] = PLAYER_O;
        if (evaluateResult(PLAYER_O, PLAYER_X) == 10)
        {
          board[i][j] = EMPTY;
          return {i, j, 0};
        }
        board[i][j] = EMPTY;
      }
    }
  }

  // Take center if available
  if (board[1][1] == EMPTY)
  {
    return {1, 1, 0};
  }

  // Take any available corner
  int corners[4][2] = {{0, 0}, {0, 2}, {2, 0}, {2, 2}};
  for (int i = 0; i < 4; i++)
  {
    int r = corners[i][0];
    int c = corners[i][1];
    if (board[r][c] == EMPTY)
    {
      return {r, c, 0};
    }
  }

  // Take any available side
  for (int i = 0; i < 3; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      if (board[i][j] == EMPTY)
      {
        return {i, j, 0};
      }
    }
  }

  return bestMove; // Should never reach here if called when moves are available
}

void printBoard()
{
  Serial.println("Board:");
  for (int i = 0; i < 3; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      Serial.print(board[i][j]);
      Serial.print(" ");
    }
    Serial.println();
  }
}

// Validate that only one move was made and it's a valid O move
bool isValidOpponentMove()
{
  int newO = 0;
  int movedO = 0;
  int changedX = 0;
  int changes = 0;

  for (int i = 0; i < 3; i++)
  {
    for (int j = 0; j < 3; j++)
    {
      if (lastBoard[i][j] != board[i][j])
      {
        changes++;
        if (lastBoard[i][j] == EMPTY && board[i][j] == PLAYER_O)
          newO++;
        else if (lastBoard[i][j] == PLAYER_O && board[i][j] == EMPTY)
          movedO++;
        else if (board[i][j] == PLAYER_X || lastBoard[i][j] == PLAYER_X)
          changedX++;
      }
    }
  }
  if (newO == 1 && movedO == 0 && changedX == 0 && changes == 1)
    return true;
  if (changes == 0)
    Serial.println("No move detected");
  else
    Serial.println("Invalid opponent move detected");
  return false;
}

bool extractPlayableGrid(int cameraData[], uint8_t count)
{
  if (count < 15)
  {
    Serial.println("Invalid camera data count");
    return false;
  }
  for (int i = 0; i < 3; i++)
  {
    int col1 = cameraData[i * 5 + 1];
    int col2 = cameraData[i * 5 + 2];
    int col3 = cameraData[i * 5 + 3];
    if (col1 != EMPTY && col1 != PLAYER_X && col1 != PLAYER_O || col2 != EMPTY && col2 != PLAYER_X && col2 != PLAYER_O || col3 != EMPTY && col3 != PLAYER_X && col3 != PLAYER_O)
    {
      Serial.println("Invalid camera data values");
      return false;
    }
    board[i][0] = col1; // Column 1
    board[i][1] = col2; // Column 2
    board[i][2] = col3; // Column 3
  }
  return true;
}

void xoGameLoop()
{
  if (gameEnded)
  {
    if (currentState != GAME_OVER)
    {
      currentState = GAME_OVER;
      printOnLCD("Game Over");
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
      printOnLCD("XO Game Started");
      if (turn == PLAYER_X)
      {
        currentState = ROBOT_THINKING;
      }
      else
      {
        currentState = WAITING_FOR_PLAYER;
      }
      stateStartTime = currentTime;
    }
    break;

  case ROBOT_THINKING:
    // Calculate robot's move
    robotMove = findBestMove();
    board[robotMove.row][robotMove.col] = PLAYER_X;
    lastBoard[robotMove.row][robotMove.col] = PLAYER_X;

    Serial.print("X → Row ");
    Serial.print(robotMove.row);
    Serial.print(", Col ");
    Serial.println(robotMove.col);

    // Setup for grabbing phase
    setupServoMoveSequence(
        stackAngleData[stackCounter][0],
        stackAngleData[stackCounter][1],
        stackAngleData[stackCounter][2],
        stackAngleData[stackCounter][3]);

    currentState = ROBOT_GRABBING;
    stateStartTime = currentTime;
    printOnLCD("Robot's turn...");
    break;

  case ROBOT_GRABBING:
    // Execute grabbing sequence
    if (processServoMoveStep())
    {
      // Grabbing complete, prepare to place piece
      getAnglesForCell(robotMove.row, robotMove.col, moveAngles);
      setupServoMoveSequence(
          moveAngles[0],
          moveAngles[1],
          moveAngles[2],
          moveAngles[3]);

      currentState = ROBOT_PLACING;
      stateStartTime = currentTime;
    }
    break;

  case ROBOT_PLACING:
    // Execute placing sequence
    if (processServoMoveStep())
    {
      // Placing complete
      if (--stackCounter < 0)
      {
        Serial.println("Stack underflow");
        gameEnded = true;
        currentState = GAME_OVER;
      }
      else
      {
        printBoard();
        // Setup for retreating phase
        setupServoMoveSequence(
            retreatAngles[0],
            retreatAngles[1],
            retreatAngles[2],
            retreatAngles[3]);

        currentState = ROBOT_RETREATING;
        stateStartTime = currentTime;
        printOnLCD("Robot retreating...");
      }
    }
    break;

  case ROBOT_RETREATING:
    // Execute retreating sequence
    if (processServoMoveStep())
    {
      // Retreating complete
      turn = PLAYER_O;
      currentState = WAITING_FOR_PLAYER;
      stateStartTime = currentTime;
      printOnLCD("Your turn...");
    }
    break;

  case WAITING_FOR_PLAYER:
    // Wait for player to make a move
    if (currentTime - stateStartTime > 4000)
    { // Wait a bit before checking camera
      currentState = CAPTURING_BOARD;
      stateStartTime = currentTime;
    }
    break;

  case CAPTURING_BOARD:
  {
    // Enclose this case block in braces to scope the variable
    Serial.println("Player O Move: ");
    String res = getPythonData("xo");

    if (res != "ERROR")
    {
      int cam[20];
      int cnt;
      parseCSV(res.c_str(), cam, cnt);

      if (extractPlayableGrid(cam, cnt))
      {
        Serial.println("Grid extracted correctly");

        if (isValidOpponentMove())
        {
          for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
              lastBoard[i][j] = board[i][j];

          Serial.println("Player O moved correctly");
          printBoard();
          turn = PLAYER_X;
          currentState = ROBOT_THINKING;
        }
        else
        {
          // Invalid move, wait and try again
          currentState = WAITING_FOR_PLAYER;
          stateStartTime = millis() + 2000; // Wait a bit before retrying
          printOnLCD("Invalid move!");
        }
      }
      else
      {
        // Couldn't extract grid, retry
        currentState = WAITING_FOR_PLAYER;
        stateStartTime = millis() + 1000;
      }
    }
    else
    {
      Serial.println("Camera failed");
      currentState = WAITING_FOR_PLAYER;
      stateStartTime = millis() + 1000;
      printOnLCD("Camera error!");
    }
    break;
  }

  case GAME_OVER:
    // Game has ended
    break;
  }

  // Check for game result in any state
  if (currentState == ROBOT_THINKING || currentState == ROBOT_RETREATING)
  {
    int res = evaluateResult(PLAYER_X, PLAYER_O);
    if (res == 10)
    {
      Serial.println("I win");
      gameEnded = true;
      printOnLCD("Robot wins!");
    }
    else if (res == -10)
    {
      Serial.println("I lose");
      gameEnded = true;
      printOnLCD("You win!");
    }
    else if (isBoardFull())
    {
      Serial.println("Tie");
      gameEnded = true;
      printOnLCD("It's a tie!");
    }
  }
}

void stopXOGame()
{
  Serial.println("Stopping XO Game");
  changeConfig("none");
  gameEnded = true;
  currentState = GAME_OVER;
}