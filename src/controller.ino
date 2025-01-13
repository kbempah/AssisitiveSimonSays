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
