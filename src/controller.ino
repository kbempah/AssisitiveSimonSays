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
const int NUM_LEDS = 4;
const int MAX_LVL = 100;
const int NUM_BRIGHTNESS_LVL = 3;
const int RESET_PIN = 7;  // This pin pulls the arduino reset button LOW effectively resetting the game
const int INTERRUPT_PIN = 2;
const int MODE_SELECT_PIN = A3;           // Slide switch for game mode selection
const int DIFFICULTY_SELECTION_PIN = A0;  // Slide switch for difficulty selection

const int CE_PIN = 8;
const int CSN_PIN = 10;

const int ledPins[] = { 5, 9, 6, 3 };                // 0 red, 1 blue, 2 green, 3 yellow
const int brightnessLevels[] = { 0x55, 0xAA, 0xFF };  // Define brightness levels (85, 170, 255)
const uint8_t address[][6] = { "1Node", "2Node" };
const int buttonPins[] = { 0x10, 0x40, 0x20, 0x80 };
const int buttonTones[] = { 1, 2, 3, 4 };     // tones for button presses

