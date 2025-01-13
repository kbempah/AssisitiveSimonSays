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
