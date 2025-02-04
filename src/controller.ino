#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <DFRobotDFPlayerMini.h>
#include <SoftwareSerial.h>
#include <Wire.h>

// this can be moved to a header file for better organization
typedef enum {
  EASY,
  MEDIUM,
  HARD
} difficulty_t;

const char *difficultyNames[] = {
  "EASY",
  "MEDIUM",
  "HARD"
};

typedef enum {
  INPUT_TIMEOUT,
  INPUT_VALID,
  INPUT_INVALID
} input_t;

typedef struct datapacket {
  int isValid;
  int nextSequenceInput;
  int nextLevel;
  bool reset;
} datapacket_t;

typedef struct difficultyconfig {
  int initialRetries;
  int speed;                          // Delay between LEDs in seconds
  unsigned long triggerTimeout;  // Timeout in milliseconds
} difficultyconfig_t;

// Difficulty settings defaults
const difficultyconfig_t difficultySettings[] = {
  { 99, 4000, 8000 },  // EASY: unlimited retries, 2s speed, 8s timeout
  { 3, 1500, 5000 },   // MEDIUM: 3 retries, 1.5s speed, 5s timeout
  { 0, 1000, 4000 }    // HARD: 0 retries, 1s speed, 4s timeout
};

const int NUM_LEDS = 4;
const int MAX_LVL = 100;
const int NUM_BRIGHTNESS_LVL = 3;
const int RESET_PIN = 7;  // This pin pulls the arduino reset button LOW effectively resetting the game
const int INTERRUPT_PIN = 2;
const int MODE_SELECT_PIN = A3;           // Slide switch for game mode selection
const int DIFFICULTY_SELECTION_PIN = A0;  // Slide switch for difficulty selection

const int CE_PIN = 8;
const int CSN_PIN = 10;

difficulty_t currentDifficulty;
int retriesLeft;
int level = 1;
int speed;
unsigned long triggerTimeout;
int sequence[MAX_LVL];      // Max level = 100; stores the LED sequence
int userSequence[MAX_LVL];
int userInputIndex = 0;     // Tracks user's progress in the sequence for multiplayer
bool radio = 1;             // uses address[1] to transmit to peripheral device

volatile int brightnessIndex = 2;   // Starting brightness index
volatile bool gameStarted = false;  // Flag to track if the game has started
volatile bool gameOver = true;      // Flag to signal game end
volatile bool startPressed = false;
volatile bool resetQueued = false;

bool singlePlayerMode;  // Game mode: true for single-player, false for multiplayer
bool multiPlayerMode;

const int ledPins[] = { 5, 9, 6, 3 };                // 0 red, 1 blue, 2 green, 3 yellow
const int brightnessLevels[] = { 0x55, 0xAA, 0xFF };  // Define brightness levels (85, 170, 255)
const uint8_t address[][6] = { "1Node", "2Node" };
// int buttonPins[] = { A2, A3, A4, A5 };  // Analog pins for the buttons corresponding to the LEDs -> 0 red, 1 blue, 2 green, 3 yellow
const int buttonPins[] = { 0x10, 0x40, 0x20, 0x80 };
const int buttonTones[] = { 1, 2, 3, 4 };     // tones for button presses

SoftwareSerial softwareSerial(/*rx =*/0, /*tx =*/1);
DFRobotDFPlayerMini dfPlayer;

RF24 controller(CE_PIN, CSN_PIN);

void initializeNRFModule(void) {
  if (!controller.begin()) {
    Serial.println("Could not initialize controller radio");
    while (true) {}  // hold an infinite loop to prevent progress from here.
  }
  controller.setPALevel(RF24_PA_LOW);
  // set the TX address of the RX node into the TX pipe
  controller.openWritingPipe(address[radio]);
  // set the RX address of the TX node into a RX pipe
  controller.openReadingPipe(1, address[!radio]);
  controller.stopListening();
}

void initializeGPIOModule(void) {
  for (int i = 0; i < NUM_LEDS; ++i) {
    pinMode(ledPins[i], OUTPUT);  // Set all LED pins as outputs
  }

  for (int i = 0; i < NUM_LEDS; ++i) {
    pinMode(buttonPins[i], INPUT_PULLUP);
  }

  // Set up button pin as input with pull-up resistor
  pinMode(INTERRUPT_PIN, INPUT_PULLUP);      // initially high
  pinMode(MODE_SELECT_PIN, INPUT);           // Game mode slide switch
  pinMode(DIFFICULTY_SELECTION_PIN, INPUT);  // Difficulty level slide switch

  // Attach interrupt to the button pin
  attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), handleStartReset, FALLING);
}

void initializeDFPlayerModule(void) {
  if (!dfPlayer.begin(softwareSerial)) {
    Serial.println("Could not initialize dfplayer");
    while (true) {} // hold an infinite loop to prevent progress from here.
  }
  set initial volume
  dfPlayer.volume(10);
}

void setup() {
  softwareSerial.begin(9600);
  Serial.begin(115200);

  initializeDFPlayerModule();	
  initializeNRFModule();
  initializeGPIOModule();
}

void loop() {
  if (gameOver) {
    singlePlayerMode = digitalRead(MODE_SELECT_PIN) == LOW;
    multiPlayerMode = !singlePlayerMode;
    if (singlePlayerMode) {  // only get difficulty when in single player mode
      currentDifficulty = getDifficulty();
    }
    if (multiPlayerMode) {
      speed = 1;
    }
    gameOver = false;  // because new game started
  }

  // Serial.println("Before I press start button for singlePlayerMode");
  if (singlePlayerMode && gameStarted) {  // user has hit start/stop button and game mode is singleplayer
    initializeGame();
    generateSequence();
    startSinglePlayerMode();
  }

  if (multiPlayerMode && gameStarted && !startPressed) {  // user has hit start button on controller module therefore gameStarted == true
    startPressed = true;                                  // this way we only go into this loop once
    bool syncd = false;
    datapacket_t syn = { -1, -1 };
    Serial.println("Initiating contact with peripheral module");
    sendPacket(syn);

    // if here, we successfully talked to receiver, now our turn to receive
    datapacket_t synack = getPacket();
    if (synack.isValid == syn.isValid && synack.nextSequenceInput == syn.nextSequenceInput) {
      Serial.println("SYNACK packet successfully received, moving on");
      syncd = true;
    }

    if (syncd) {
      Serial.println("controller: let the games begin");
      startMultiPlayerMode();
    }
  }
}

bool sendPacket(datapacket_t packet) {
  bool writeSuccess = false;

  controller.stopListening();  // puts the controller into transmit mode

  Serial.println("Attempting write");
  while (!writeSuccess) {  // true means we got an acknowledgement from the receiver
    if (controller.write((void *)&packet, sizeof(packet))) {
      writeSuccess = true;
    } else {
      Serial.println("Failed to write to peripheral");
    }
  }
  Serial.println("Successfully wrote to  peripheral");

  return writeSuccess;
}

datapacket_t getPacket(void) {
  datapacket_t temp;
  bool dataRead = false;

  controller.startListening();

  while (!dataRead) {
    if (controller.available()) {
      controller.read((void *)&temp, sizeof(temp));
      dataRead = true;
    } else {
      Serial.println("Inside getPacket(): No bytes available to read, checking again");
    }
  }

  return temp;
}

void displayWaitSequence() {
  for (int i = 0; i < NUM_LEDS; ++i) {
    analogWrite(ledPins[i], brightnessLevels[brightnessIndex]);  // max brightness
  }
  delay(1000);
  for (int i = 0; i < NUM_LEDS; ++i) {
    analogWrite(ledPins[i], 0);
  }
  delay(1000);
}

void startSinglePlayerMode() {
  Serial.println("Entering startSinglePlayerMode()");
  input_t status;

  /*
  Ready 
  Set
  Go
  */
  
  while (!gameOver) {
    Serial.print("Starting level "); Serial.print(level); Serial.print(" in "); Serial.print(difficultyNames[(int)currentDifficulty]); Serial.println(" difficulty.");
    displaySequence();
    status = handleInputSP();
    if (status == INPUT_VALID) {
      rightSequence();
    } else if (status == INPUT_TIMEOUT) {
      Serial.println("Wait for input timed out");
      wrongSequence();
    } else { // must be bad input
      Serial.println("Incorrect input");
      wrongSequence();
    }
  }

  Serial.println("Exiting startSinglePlayerMode()");
}

void promptUserForInput(void) {
  if (level == 1) {
    Serial.println("Enter first input");
    // dfPlayer.play(); // insert track to prompt user to enter first color
  } else {
    Serial.println("Enter input");
    // dfPlayer.play(); // insert track number to prompt for regular sequence.
  }
}

void startMultiPlayerMode() {
  Serial.println("Entering startMultiPlayerMode()");
  int extension;
  input_t status;  // to handle result of extending sequence
  bool validToken = true;
  datapacket_t token;

  while (!gameOver) {
    if (validToken) {
      promptUserForInput();  // do this once per round
      displaySequenceMP();
      status = handleInputMP(&extension);
      Serial.print("Extension to sequence ");
      Serial.println(extension);
      if (status == INPUT_VALID) { // valid input
        updatePeripheral(token, 0, extension, level + 1, resetQueued); // advance level for peripheral
        token = getPacket();
        validToken = verifyRoundToken(token);
      } else { // timeout or invalid input
        updatePeripheral(token, -1, extension, level, resetQueued); // we don't care about level here because game is ending anyway
        resetGameState();
      }
    } else {  // we are here because isValid was not 0 (assuming it's -1)
      // rightSequence(); // right sequence for you but the game ends now
      resetGameState();
    }
  }
  Serial.println("Exiting startMultiPlayerMode()");
}

bool verifyRoundToken(datapacket_t roundToken) {
  Serial.println("Entering verifyRoundToken()");
  bool validToken = false;

  if (roundToken.isValid == 0) {
    validToken = true;
    level = roundToken.nextLevel;
    userSequence[userInputIndex++] = roundToken.nextSequenceInput;
    for (int i = 0; i < level - 1; i++) {
      Serial.print(userSequence[i]);
      Serial.print(" ");
    }
    Serial.println();
    Serial.println("Game state updated");
  }

  Serial.print("Current level is: ");
  Serial.println(level);

  Serial.println("Exiting verifyRoundToken()");
  return validToken;
}

void initializeGame() {
  Serial.println("Entering initializeGame()");
  const difficultyconfig_t &config = difficultySettings[static_cast<int>(currentDifficulty)];
  retriesLeft = config.initialRetries;
  speed = config.speed;
  triggerTimeout = config.triggerTimeout;
  Serial.println("Exiting initializeGame()");
}

void generateSequence(void) {
  Serial.println("Entering generateSequence()");
  randomSeed(millis());
  for (int i = 0; i < MAX_LVL; ++i) { // fill up all the possible moves for user, not a fan on this because it can get boring very easily
    // sequence[i] = random(0, NUM_LEDS);  // Random LED index
    sequence[i] = 2; // force green LED
  }
  Serial.println("Exiting generateSequence()");
}

void resetGameState() {
  Serial.println("Entering resetGameState()");
  level = 1;
  gameOver = true;
  gameStarted = false;
  startPressed = false;
  resetQueued = false;
  userInputIndex = 0;
  brightnessIndex = 2;
  Serial.println("Exiting resetGameState()");
}

