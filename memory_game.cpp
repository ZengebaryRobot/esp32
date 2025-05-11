#include "memory_game.h"
#include "game_utils.h"
#include <Arduino.h>
#include <stdlib.h>
// #include <ctime>
#define ROWS 2
#define COLS 3
#define CELLS_CNT 6
#define SHAPES 3
#define GRIP_OPEN 120
#define GRIP_CLOSED 60
#define DEFAULT_ANGLE_SHOULDER 105

extern void changeConfig(String command);
extern String getPythonData(String command);
extern void parseCSV(const char *csv, int arr[], int &count);
extern bool sendServoCommand(int a1, int a2, int a3);
extern bool sendStepperCommand(const int cmds[]);
extern void printOnLCD(const String &msg);

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
  GAME_MATCH_FOUND,        // New state for when a match is found
  GAME_MOVE_MATCHED_CARD1, // New state to move first matched card
  GAME_MOVE_MATCHED_CARD2, // New state to move second matched card
  GAME_RETURN_UNMATCHED,   // New state for returning unmatched cards
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

Position cell_0(126, 20, 75, 34);
Position cell_1(111, 29, 90, 41);
Position cell_2(96, 26, 85, 41);
Position cell_3(133, 48, 124, 50);
Position cell_4(113, 55, 135, 55);
Position cell_5(91, 55, 134, 62);
Position cell_6(82, 85, 169, 66);  // Temporary position 1
Position cell_7(147, 78, 165, 72); // Temporary position 2
Position cell_8(30, 105, 124, 50); // Output position

// Global state variables
MemoryGameState gameState = GAME_IDLE;
ArmMoveState armState = MOVE_IDLE;
unsigned long lastStateChangeTime = 0;
const unsigned long STATE_DELAY = 100; // Minimum delay between state transitions

// Move operation state variables
int srcIdx = -1;
int destIdx = -1;
Position currentSrc(0, 0, 0, 0);
Position currentDest(0, 0, 0, 0);

// Game state variables
int complete = 0;                   // Counter to track the number of completed shapes
int currentMatrixState[ROWS][COLS]; // Tracks the shapes in each position
int cardPositions[SHAPES][2];       // Tracks positions of each shape: cardPositions[shape][0/1]
bool cardMatched[SHAPES];           // Tracks if a shape has been matched
int tempPositions[2] = {-1, -1};    // Cards currently in temporary positions 6 and 7
int outPosition = 8;                // Start index for output positions
int rnd1 = -1;                      // First random cell selected
int rnd2 = -1;                      // Second random cell selected
int currentShape1 = -1;             // Shape found in first selected cell
int currentShape2 = -1;             // Shape found in second selected cell
bool matchFound = false;            // Flag to indicate if a match was found

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
  if(to == 6 || to == 7){
    printOnLCD("Reveal cell " + String(from));
  }
  if(to >= 8){
    printOnLCD("Dumping...");
  }
  Serial.print("Starting move from ");
  Serial.print(from);
  Serial.print(" to ");
  Serial.println(to);
}

bool updateArmMove()
{
  static unsigned long lastAttemptTime = 0;
  const unsigned long RETRY_DELAY = 100; // ms

  unsigned long currentTime = millis();
  if (currentTime - lastAttemptTime < RETRY_DELAY)
  {
    return false;
  }
  lastAttemptTime = currentTime;

  bool stateCompleted = false;

  switch (armState)
  {
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
    stateCompleted = executeServoMoveNonBlocking(ArmMotor::WRIST, currentSrc.wrist, 0);
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
    stateCompleted = executeServoMoveNonBlocking(ArmMotor::WRIST, 45, 0);
    if (stateCompleted)
      armState = RELEASE_SET_ELBOW;
    break;
  case RELEASE_SET_ELBOW:
    stateCompleted = executeServoMoveNonBlocking(ArmMotor::ELBOW, currentDest.elbow, 0);
    if (stateCompleted){
      if(destIdx == 8) armState = RELEASE_OPEN_GRIP;
      else armState = RELEASE_SET_WRIST_MID;
    }
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
      return true;
    }
    break;
  case MOVE_IDLE:
  case MOVE_COMPLETE:
    return true;
  default:
    Serial.println("Unknown arm state");
    armState = MOVE_IDLE;
    return true;
  }
  return false;
}

void initializeGameState()
{
  Serial.println("Initializing game state");

  // Reset game state variables
  complete = 0;

  // Clear board state
  for (int i = 0; i < ROWS; i++)
  {
    for (int j = 0; j < COLS; j++)
    {
      currentMatrixState[i][j] = -1;
    }
  }

  // Reset card position tracking
  for (int i = 0; i < SHAPES; i++)
  {
    cardPositions[i][0] = -1;
    cardPositions[i][1] = -1;
    cardMatched[i] = false;
  }

  // Reset temporary positions
  tempPositions[0] = -1;
  tempPositions[1] = -1;

  // Reset output position
  outPosition = 8;

  // Reset selection variables
  rnd1 = -1;
  rnd2 = -1;
  currentShape1 = -1;
  currentShape2 = -1;
  matchFound = false;

  Serial.println("Game state initialized");
}

int pickRandomCell(int currentMatrixState[][COLS])
{
  const int OUT_POSITION = 8; // Consistent with the game loop
  // Create a list of valid positions to pick from
  int validPositions[CELLS_CNT];
  int validCount = 0;


  Serial.println("currest state matrix in random");
  for (int i = 0; i < ROWS; i++)
  {
    for (int j = 0; j < COLS; j++)
    {
      Serial.print(currentMatrixState[i][j]);
      Serial.print(", ");
    }
    Serial.println(" ");
  }
  

  // Find all valid positions (positions that still have cards and are not matched)
  for (int i = 0; i < ROWS; i++)
  {
    for (int j = 0; j < COLS; j++)
    {
      int shape = currentMatrixState[i][j];
      int pos = i * COLS + j;
      /*
      Serial.print("shape: ");
      Serial.println(shape);
      Serial.print("pos: ");
      Serial.println(pos);
      */

      if (shape == -1)
      {
        validPositions[validCount] = pos;
        validCount++;
      }
    }
  }
  
  Serial.print("validCount: ");
  Serial.println(validCount);

  Serial.println("valid position arr");
  for (int i = 0; i < validCount; i++)
  {
    Serial.print(validPositions[i]);
    Serial.print(", ");
  }
  Serial.println("");
  
  // If we have valid positions, pick one randomly
  if (validCount > 0)
  {
    int randomIndex = millis() % validCount;
    int selectedPosition = validPositions[randomIndex];
    // int selectedPosition = validPositions[0];

    Serial.print("Picked random cell: ");
    Serial.print(selectedPosition);
    Serial.print(" (row=");
    Serial.print(selectedPosition / COLS);
    Serial.print(", col=");
    Serial.print(selectedPosition % COLS);
    Serial.println(")");

    Serial.println("exit random function");

    return selectedPosition;
  }
}

void recordCardPosition(int shape, int position)
{
  // Don't record if we've already matched this shape
  if (cardMatched[shape])
  {
    Serial.print("Warning: Trying to record position for already matched shape ");
    Serial.println(shape);
    return;
  }

  if (cardPositions[shape][0] == -1)
  {
    cardPositions[shape][0] = position;
    // currentMatrixState[position / 2][position % 2] = shape;
  }
  else if (cardPositions[shape][1] == -1)
  {
    cardPositions[shape][1] = position;
    // currentMatrixState[position / 2][position % 2] = shape;
  }
  else
  {
    Serial.print("Error: Trying to record a third position for shape ");
    Serial.println(shape);
  }
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
      int row = i / COLS;
      int col = i % COLS;
      if (row < ROWS && col < COLS)
      {
        outputArray[row][col] = arr[i];
      }
    }
    Serial.println("Converted 2D Array:");
    for (int i = 0; i < ROWS; i++)
    {
      for (int j = 0; j < COLS; j++)
      {
        Serial.print(outputArray[i][j]);
        Serial.print(" ");
      }
      Serial.println();
    }
  }
  else
  {
    Serial.println("Error: parseCSV returned count 0 or less.");
  }
}

void module(int outputArray[2][3])
{
  String values = getPythonData("memory");
  convetStringTo2D(values, CELLS_CNT, outputArray);
}

void startMemoryGame()
{
  Serial.println("Starting Memory Game");
  changeConfig("memory");
  initializeGameState();
  gameState = GAME_INIT;
}

void memoryGameLoop()
{
  const int OUT_POSITION = 8; // Constant for the output position
  int pos1 = -1;
  int pos2 = -1;
  int secondCardPos = -1;

  unsigned long currentTime = millis();

  // Enforce minimum delay between state transitions
  if (currentTime - lastStateChangeTime < STATE_DELAY)
  {
    return;
  }

  // If the arm is moving, wait for it to complete
  if (armState != MOVE_IDLE && armState != MOVE_COMPLETE)
  {
    bool moveCompleted = updateArmMove();
    if (!moveCompleted)
    {
      return;
    }
    armState = MOVE_IDLE;
    lastStateChangeTime = currentTime;
  }

  // Main game state machine
  if (armState == MOVE_IDLE || armState == MOVE_COMPLETE)
  {
    switch (gameState)
    {
    case GAME_IDLE:
      // Do nothing in idle state
      break;

    case GAME_INIT:
      // Initialize game state
      initializeGameState();
      gameState = GAME_FIND_MATCH;
      lastStateChangeTime = currentTime;
      Serial.println("Game initialized, looking for matches");
      break;

    case GAME_FIND_MATCH:
      // Check if game is complete
      Serial.print("complete: ");

      Serial.println(complete);
      if (complete >= SHAPES)
      {
        gameState = GAME_COMPLETED;
        printOnLCD("Game Completed!");
        Serial.println("Game completed!");
      }
      else
      {
        // Debug: print current state of cardPositions and cardMatched
        Serial.println("Current card tracking state:");
        for (int i = 0; i < SHAPES; i++)
        {
          Serial.print("Shape ");
          Serial.print(i);
          Serial.print(": Pos1=");
          Serial.print(cardPositions[i][0]);
          Serial.print(", Pos2=");
          Serial.print(cardPositions[i][1]);
          Serial.print(", Matched=");
          Serial.println(cardMatched[i] ? "Yes" : "No");
        }

        if (complete == SHAPES - 1)
        {
          int count = 0;
          int foundPos1 = -1;
          int foundPos2 = -1;
          int shape = -1;

          for (int i = 0; i < SHAPES; i++)
          {
            if (!cardMatched[i])
            {
              shape = i;
              break;
            }
          }

          for (int i = 0; i < ROWS; i++)
          {
            for (int j = 0; j < COLS; j++)
            {
              if (currentMatrixState[i][j] == -1)
              {
                count++;

                if (foundPos1 == -1)
                {
                  foundPos1 = i * COLS + j;
                }
                else
                {
                  foundPos2 = i * COLS + j;
                }
              }
            }
          }

          if (count == 2)
          {
            cardPositions[shape][0] = foundPos1;
            cardPositions[shape][1] = foundPos2;
          }
          else if (count == 1)
          {
            if (cardPositions[shape][0] == -1)
            {
              cardPositions[shape][0] = foundPos1;
            }
            else if (cardPositions[shape][1] == -1)
            {
              cardPositions[shape][1] = foundPos1;
            }
            else
            {
              Serial.println("Conflict in founding 1 position");
            }
          }
          else
          {
            Serial.println("Conflict in founding last shape");
          }
        }

        // Look for known matches
        bool foundKnownMatch = false;
        for (int shape = 0; shape < SHAPES; shape++)
        {
          if (!cardMatched[shape] && cardPositions[shape][0] != -1 && cardPositions[shape][1] != -1)
          {
            // Found a known match
            cardMatched[shape] = true;
            complete++;
            currentShape1 = shape;
            foundKnownMatch = true;
            matchFound = true;
            Serial.print("Found known match for shape: ");
            Serial.println(shape);
            gameState = GAME_MOVE_MATCHED_CARD1;
            break;
          }
        }

        if (!foundKnownMatch)
        {
          // No known matches, pick a random card
          gameState = GAME_PICK_RANDOM1;
        }
      }
      lastStateChangeTime = currentTime;
      break;

    case GAME_PICK_RANDOM1:
      // Pick first random card
      rnd1 = pickRandomCell(currentMatrixState);

      // Check if we could find a valid position
      if (rnd1 == -1)
      {
        // No valid positions - this could mean the game is complete
        // or there's an issue with our game state tracking
        Serial.println("No valid positions to pick - checking game state");

        // Count how many shapes are matched
        int matchedCount = 0;
        for (int i = 0; i < SHAPES; i++)
        {
          if (cardMatched[i])
          {
            matchedCount++;
          }
        }

        if (matchedCount >= SHAPES)
        {
          // All shapes are matched, game is complete
          complete = SHAPES;
          gameState = GAME_COMPLETED;
          //Serial.println("All cards matched - game complete!");
        }
        else
        {
          // Something's wrong - force a state refresh
          Serial.println("Game state inconsistency detected - returning to FIND_MATCH");
          gameState = GAME_FIND_MATCH;
        }
        lastStateChangeTime = currentTime;
        break;
      }

      Serial.print("Random cell 1 picked: ");
      Serial.println(rnd1);

      // Move the card to temporary position 1
      startMoveOperation(rnd1, 6);
      tempPositions[0] = rnd1;

      gameState = GAME_REVEAL1;
      lastStateChangeTime = currentTime;
      break;

    case GAME_REVEAL1:
      if (armState == MOVE_IDLE)
      {
        // Read the card with camera
        int matrixFromCamera[2][3];
        module(matrixFromCamera);

        // Get the shape of the card
        int row = rnd1 / COLS;
        int col = rnd1 % COLS;
        currentShape1 = matrixFromCamera[row][col];
        currentMatrixState[row][col] = currentShape1;
        
        Serial.println("currest state matrix in reveal1");
        for (int i = 0; i < ROWS; i++)
        {
          for (int j = 0; j < COLS; j++)
          {
            Serial.print(currentMatrixState[i][j]);
            Serial.print(", ");
          }
          Serial.println(" ");
        }
        
        Serial.print("Reveal result for rnd1 (shape): ");
        Serial.println(currentShape1);

        // Record the card position for this shape
        recordCardPosition(currentShape1, rnd1);

        // Check if we already know the other card of this shape
        if (cardPositions[currentShape1][0] != -1 && cardPositions[currentShape1][1] != -1 && !cardMatched[currentShape1])
        {
          // We already know both cards for this shape, it's a match!
          printOnLCD("Match found!");
          matchFound = true;
          gameState = GAME_MOVE_MATCHED_CARD1;
        }
        else
        {
          // Pick a second random card
          gameState = GAME_PICK_RANDOM2;
        }
        lastStateChangeTime = currentTime;
      }
      break;

    case GAME_PICK_RANDOM2:
      // Pick second random card, different from the first
      do
      {
        rnd2 = pickRandomCell(currentMatrixState);

        // Check if we could find a valid position
        if (rnd2 == -1)
        {
          Serial.println("No valid positions for second card - returning first card");
          // Return the first card and go back to finding matches
          gameState = GAME_RETURN_UNMATCHED;
          lastStateChangeTime = currentTime;
          return; // Exit the function early
        }
      } while (rnd2 == rnd1);

      Serial.print("Random cell 2 picked: ");
      Serial.println(rnd2);

      // Move the card to temporary position 2
      startMoveOperation(rnd2, 7);
      tempPositions[1] = rnd2;

      gameState = GAME_REVEAL2;
      lastStateChangeTime = currentTime;
      break;
    case GAME_REVEAL2:
      if (armState == MOVE_IDLE)
      {
        // Read the card with camera
        int matrixFromCamera[2][3];
        module(matrixFromCamera);

        // Get the shape of the card
        int row = rnd2 / COLS;
        int col = rnd2 % COLS;
        currentShape2 = matrixFromCamera[row][col];
        currentMatrixState[row][col] = currentShape2;

        Serial.println("currest state matrix in reveal2");
        for (int i = 0; i < ROWS; i++)
        {
          for (int j = 0; j < COLS; j++)
          {
            Serial.print(currentMatrixState[i][j]);
            Serial.print(", ");
          }
          Serial.println(" ");
        }

        Serial.print("Reveal result for rnd2 (shape): ");
        Serial.println(currentShape2);

        // Record the card position for this shape
        recordCardPosition(currentShape2, rnd2);

        Serial.print("currShape1: ");
        Serial.println(currentShape1);

        Serial.print("currShape2: ");
        Serial.println(currentShape2);
        // Check if it's a match with the first card
        if (currentShape1 == currentShape2)
        {
          Serial.println("Match found!");
          printOnLCD("Match found!");
          // Mark shape as matched
          cardMatched[currentShape1] = true;
          complete++;
          matchFound = true;
          gameState = GAME_MOVE_MATCHED_CARD1;
        }
        else
        {
          // No match - return both cards to their original positions
          matchFound = false;
          gameState = GAME_RETURN_UNMATCHED;
        }
        lastStateChangeTime = currentTime;
      }
      break;

    case GAME_MOVE_MATCHED_CARD1:
      // Handle moving the first matched card to output area
      if (tempPositions[0] != -1 && (cardPositions[currentShape1][0] == tempPositions[0] || cardPositions[currentShape1][1] == tempPositions[0]))
      {
        // Card 1 is already in temp position 6
        startMoveOperation(6, OUT_POSITION);

        // Store which position we've moved from
        int pos = tempPositions[0];
        tempPositions[0] = -1;

        // Update the card position to show it's in the output area
        if (cardPositions[currentShape1][0] == pos)
        {
          cardPositions[currentShape1][0] = OUT_POSITION;
        }
        else
        {
          cardPositions[currentShape1][1] = OUT_POSITION;
        }
      }
      else if (tempPositions[1] != -1 && (cardPositions[currentShape1][0] == tempPositions[1] || cardPositions[currentShape1][1] == tempPositions[1]))
      {
        // Card 1 is already in temp position 7
        startMoveOperation(7, OUT_POSITION);

        // Store which position we've moved from
        int pos = tempPositions[1];
        tempPositions[1] = -1;

        // Update the card position to show it's in the output area
        if (cardPositions[currentShape1][0] == pos)
        {
          cardPositions[currentShape1][0] = OUT_POSITION;
        }
        else
        {
          cardPositions[currentShape1][1] = OUT_POSITION;
        }
      }
      else
      {
        // Card 1 is in its original position on the board
        // Find which position has a valid card on the board (not in OUT_POSITION)
        int cardPos = -1;

        if (cardPositions[currentShape1][0] < 6)
        { // Valid board position (0-5)
          cardPos = cardPositions[currentShape1][0];
        }
        else if (cardPositions[currentShape1][1] < 6)
        { // Valid board position (0-5)
          cardPos = cardPositions[currentShape1][1];
        }

        // Make sure this position is valid before moving
        if (cardPos >= 0 && cardPos < 6)
        {
          int row = cardPos / COLS;
          int col = cardPos % COLS;

          // Verify the card is still in the position we think it is
          if (row >= 0 && row < ROWS && col >= 0 && col < COLS && currentMatrixState[row][col] == currentShape1)
          {
            startMoveOperation(cardPos, OUT_POSITION);

            // Update the card position to show it's in the output area
            if (cardPositions[currentShape1][0] == cardPos)
            {
              cardPositions[currentShape1][0] = OUT_POSITION;
            }
            else
            {
              cardPositions[currentShape1][1] = OUT_POSITION;
            }
          }
          else
          {
            Serial.print("Warning: Card not found at expected position: ");
            Serial.println(cardPos);
          }
        }
        else
        {
          Serial.print("Warning: Invalid position for first matched card: ");
          Serial.println(cardPos);
        }
      }

      gameState = GAME_MOVE_MATCHED_CARD2;
      lastStateChangeTime = currentTime;
      break;

    case GAME_MOVE_MATCHED_CARD2:
      // Handle moving the second matched card to output area
      pos1 = cardPositions[currentShape1][0];
      pos2 = cardPositions[currentShape1][1];
      secondCardPos = -1;

      Serial.print("pos1: ");
      Serial.println(pos1);
      Serial.print("pos2: ");
      Serial.println(pos2);
      // Find which position still has a card on the board
      if (pos1 != OUT_POSITION && pos1 >= 0 && pos1 < 6)
      {
        secondCardPos = pos1;
      }
      else if (pos2 != OUT_POSITION && pos2 >= 0 && pos2 < 6)
      {
        secondCardPos = pos2;
      }

      Serial.print("secondCardPos: ");
      Serial.println(secondCardPos);

      // Check temporary positions if we haven't found the card on the board
      if (secondCardPos == -1)
      {
        if (tempPositions[0] != -1)
        {
          int row = tempPositions[0] / COLS;
          int col = tempPositions[0] % COLS;
          if (currentMatrixState[row][col] == currentShape1)
          {
            secondCardPos = 6; // temp position 6
          }
        }

        if (tempPositions[1] != -1)
        {
          int row = tempPositions[1] / COLS;
          int col = tempPositions[1] % COLS;
          if (currentMatrixState[row][col] == currentShape1)
          {
            secondCardPos = 7; // temp position 7
          }
        }
      }

      // Move the second card if found
      if (secondCardPos != -1)
      {
        if (secondCardPos == tempPositions[0])
        {
          secondCardPos = 6;
        }
        else if (secondCardPos == tempPositions[1])
        {
          secondCardPos = 7;
        }

        if (secondCardPos == 6)
        { // Using temp position constants for clarity
          startMoveOperation(6, OUT_POSITION);
          tempPositions[0] = -1;
        }
        else if (secondCardPos == 7)
        {
          startMoveOperation(7, OUT_POSITION);
          tempPositions[1] = -1;
        }
        else
        {
          startMoveOperation(secondCardPos, OUT_POSITION);
        }

        // Update card position to show it's now in output position
        if (pos1 != OUT_POSITION)
        {
          cardPositions[currentShape1][0] = OUT_POSITION;
        }
        else
        {
          cardPositions[currentShape1][1] = OUT_POSITION;
        }
      }
      else
      {
        Serial.print("Warning: Could not find second card for matched shape: ");
        Serial.println(currentShape1);
      }

      // Look for more matches after this one is complete
      gameState = GAME_FIND_MATCH;
      lastStateChangeTime = currentTime;
      break;

    case GAME_RETURN_UNMATCHED:
      // Return unmatched cards to their original positions
      if (tempPositions[1] != -1)
      {
        // Return second card first
        startMoveOperation(7, tempPositions[1]);
        tempPositions[1] = -1;
      }
      else if (tempPositions[0] != -1)
      {
        // Then return first card
        startMoveOperation(6, tempPositions[0]);
        tempPositions[0] = -1;
      }
      else
      {
        // All cards returned, look for next match
        gameState = GAME_FIND_MATCH;
      }
      lastStateChangeTime = currentTime;
      break;

    case GAME_COMPLETED:
      // Game is complete - stay in this state
     // Serial.println("in game complete state");
      break;

    default:
      Serial.println("Unknown game state");
      gameState = GAME_IDLE;
      break;
    }
  }
}

void stopMemoryGame()
{
  Serial.println("Stopping Memory Game");
  changeConfig("none");
  gameState = GAME_IDLE;
  armState = MOVE_IDLE;
}