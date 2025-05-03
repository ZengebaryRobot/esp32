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

#define GRIP_CLOSED 72
#define GRIP_OPEN 100
#define DEFAULT_ANGLE_SHOULDER 90

int board[3][3] = {
    {EMPTY, EMPTY, EMPTY},
    {EMPTY, EMPTY, EMPTY},
    {EMPTY, EMPTY, EMPTY}};
int lastBoard[3][3] = {
    {EMPTY, EMPTY, EMPTY},
    {EMPTY, EMPTY, EMPTY},
    {EMPTY, EMPTY, EMPTY}};

const int angleData[3][3][4] = {
    {{118, 12, 70, 47}, {108, 18, 80, 52}, {95, 16, 75, 50}},
    {{122, 35, 112, 55}, {108, 43, 126, 70}, {93, 38, 116, 60}},
    {{126, 60, 150, 74}, {108, 60, 150, 68}, {90, 61, 150, 76}}};

const int stackAngleData[5][4] = {
    {79, 23, 85, 41},
    {79, 26, 85, 41},
    {79, 28, 85, 39},
    {79, 33, 88, 41},
    {79, 36, 88, 40}};

// Structure to store move coordinates and score
typedef struct
{
  int row;
  int col;
  int score;
} Move;

int stackCounter = 4;
int turn = PLAYER_X;
bool gameEnded = false;

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
}

void executeServoMove(ArmMotor motor, int angle, int overShoot)
{
  while (true)
  {
    if (sendServoCommand(motor, angle, overShoot))
      break;

    Serial.println("Servo command failed, retrying...");

    delay(500);
  }
}

void goGrab(int requiredBaseAngle, int requiredShoulderAngle, int requiredElbowAngle, int requiredWristAngle)
{
  executeServoMove(ArmMotor::GRIP, GRIP_OPEN, 0);
  executeServoMove(ArmMotor::SHOULDER, DEFAULT_ANGLE_SHOULDER, 10);
  executeServoMove(ArmMotor::BASE, requiredBaseAngle, 0);
  executeServoMove(ArmMotor::WRIST, requiredWristAngle, 4);
  executeServoMove(ArmMotor::ELBOW, requiredElbowAngle, 0);
  executeServoMove(ArmMotor::SHOULDER, requiredShoulderAngle, 10);
  executeServoMove(ArmMotor::GRIP, GRIP_CLOSED, 0);
}

bool goRelease(int requiredBaseAngle, int requiredShoulderAngle, int requiredElbowAngle, int requiredWristAngle)
{
  executeServoMove(ArmMotor::SHOULDER, DEFAULT_ANGLE_SHOULDER, 10);
  executeServoMove(ArmMotor::BASE, requiredBaseAngle, 0);
  executeServoMove(ArmMotor::WRIST, requiredWristAngle, 4);
  executeServoMove(ArmMotor::ELBOW, requiredElbowAngle, 0);
  executeServoMove(ArmMotor::SHOULDER, requiredShoulderAngle, 10);
  executeServoMove(ArmMotor::GRIP, GRIP_OPEN, 0);
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
    return;

  if (turn == PLAYER_X)
  {
    Serial.println("Player X Move: ");
    Move mv = findBestMove();
    board[mv.row][mv.col] = PLAYER_X;
    lastBoard[mv.row][mv.col] = PLAYER_X;
    Serial.print("X → Row ");
    Serial.print(mv.row);
    Serial.print(", Col ");
    Serial.println(mv.col);

    int ang[4];
    getAnglesForCell(mv.row, mv.col, ang);

    goGrab(stackAngleData[stackCounter][0], stackAngleData[stackCounter][1], stackAngleData[stackCounter][2], stackAngleData[stackCounter][3]);
    delay(1000);
    goRelease(ang[0], ang[1], ang[2], ang[3]);

    printBoard();

    if (--stackCounter < 0)
    {
      Serial.println("Stack underflow");
      gameEnded = true;
      return;
    }
    turn = PLAYER_O;
  }
  else
  {
    Serial.println("Player O Move: ");
    delay(4000);

    String res = getPythonData("xo");

    if (res != "ERROR")
    {
      int cam[20];
      int cnt;
      parseCSV(res.c_str(), cam, cnt);

      if (extractPlayableGrid(cam, cnt))
      {
        Serial.println("Grid extracted correctly");
      }

      if (isValidOpponentMove())
      {
        for (int i = 0; i < 3; i++)
          for (int j = 0; j < 3; j++)
            lastBoard[i][j] = board[i][j];

        Serial.println("Player O moved correctly");
        printBoard();
        turn = PLAYER_X;
      }
    }
    else
    {
      Serial.println("Camera failed");
    }

    delay(1000);
  }

  int res = evaluateResult(PLAYER_X, PLAYER_O);
  if (res == 10)
  {
    Serial.println("I win");
    gameEnded = true;
  }
  else if (res == -10)
  {
    Serial.println("I lose");
    gameEnded = true;
  }
  else if (isBoardFull())
  {
    Serial.println("Tie");
    gameEnded = true;
  }
}

void stopXOGame()
{
  Serial.println("Stopping XO Game");
  changeConfig("none");

  //
}