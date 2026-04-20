#include <Wire.h>
#include <RTClib.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include "HX711.h"

// ----------------------------
// Pins
// ----------------------------
#define STEP_PIN 3
#define DIR_PIN 4

#define HX_DOUT 6
#define HX_SCK  7

SoftwareSerial nextion(10, 11);
RTC_DS3231 rtc;
HX711 scale;

// ----------------------------
// Motor
// ----------------------------
const int stepDelay = 500;
bool motorRunning = false;
bool motorDirection = true;

// ----------------------------
// Schedule
// ----------------------------
int targetHour = -1;
int targetMinute = -1;
int runDuration = 0;

unsigned long runStartMillis = 0;
bool scheduledRunActive = false;
int lastTriggerMinute = -1;

// ----------------------------
// Time
// ----------------------------
DateTime now;
int currentHour, currentMinute, currentSecond;
int lastSentSecond = -1;

// ----------------------------
// Weight
// ----------------------------
float targetWeight = 0;
float currentWeight = 0;
bool weightEnabled = false;

// ----------------------------
// EEPROM (ONLY calibration)
// ----------------------------
struct Settings {
  float calibration;
};

Settings settings;

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(9600);
  nextion.begin(9600);

  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);

  Wire.begin();

  if (!rtc.begin()) {
    while (1);
  }

  // ----------------------------
  // LOAD CALIBRATION FROM EEPROM
  // ----------------------------
  EEPROM.get(0, settings);

  // fallback if EEPROM empty
  if (isnan(settings.calibration) || settings.calibration == 0) {
    settings.calibration = -7050;  // default placeholder
  }

  scale.begin(HX_DOUT, HX_SCK);
  scale.set_scale(settings.calibration);
  scale.tare();
}

// ============================================================
// LOOP
// ============================================================
void loop() {

  updateTime();
  updateWeight();

  handleNextionSafe();

  checkSchedule();
  checkAutoStop();

  runMotor();
}

// ============================================================
// TIME
// ============================================================
void updateTime() {
  now = rtc.now();
  currentHour = now.hour();
  currentMinute = now.minute();
  currentSecond = now.second();

  if (currentSecond != lastSentSecond) {
    sendTimeToNextion();
    lastSentSecond = currentSecond;
  }
}

// ============================================================
// WEIGHT
// ============================================================
void updateWeight() {
  currentWeight = scale.get_units(5);
  sendWeightToNextion();
}

// ============================================================
// MOTOR
// ============================================================
void runMotor() {
  if (!motorRunning) return;

  digitalWrite(DIR_PIN, motorDirection);

  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(stepDelay);
  digitalWrite(STEP_PIN, LOW);
  delayMicroseconds(stepDelay);
}

// ============================================================
// STOP LOGIC (TIME + WEIGHT)
// ============================================================
void checkAutoStop() {

  if (!motorRunning) return;

  // TIME STOP
  if (scheduledRunActive) {
    if (millis() - runStartMillis >= (unsigned long)runDuration * 1000) {
      motorRunning = false;
      scheduledRunActive = false;
      return;
    }
  }

  // WEIGHT STOP
  if (weightEnabled && targetWeight > 0) {
    if (currentWeight >= targetWeight * 0.95) {
      motorRunning = false;
      scheduledRunActive = false;
      return;
    }
  }
}

// ============================================================
// SCHEDULE
// ============================================================
void checkSchedule() {

  if (!scheduledRunActive &&
      currentHour == targetHour &&
      currentMinute == targetMinute &&
      currentMinute != lastTriggerMinute) {

    motorRunning = true;
    scheduledRunActive = true;
    runStartMillis = millis();
    lastTriggerMinute = currentMinute;
  }
}

// ============================================================
// NEXTION SAFE PARSER
// ============================================================
void handleNextionSafe() {

  static String buffer = "";

  while (nextion.available()) {
    uint8_t c = nextion.read();

    if (c == 0xFF) {
      if (nextion.peek() == 0xFF) {
        nextion.read();
        if (nextion.peek() == 0xFF) {
          nextion.read();

          processCommand(buffer);
          buffer = "";
          return;
        }
      }
    } else {
      buffer += (char)c;
    }
  }
}

// ============================================================
// COMMANDS
// ============================================================
void processCommand(String cmd) {

  cmd.trim();
  Serial.println(cmd);

  // ----------------------------
  // START
  // ----------------------------
  if (cmd.startsWith("START")) {
    int comma = cmd.indexOf(',');
    int dir = cmd.substring(comma + 1).toInt();

    motorDirection = (dir == 0);
    motorRunning = true;
    scheduledRunActive = false;
  }

  // ----------------------------
  // STOP
  // ----------------------------
  else if (cmd == "STOP") {
    motorRunning = false;
    scheduledRunActive = false;
  }

  // ----------------------------
  // SET SCHEDULE
  // ----------------------------
  else if (cmd.startsWith("SET,")) {

    int h, m, d;
    parse3(cmd, h, m, d);

    targetHour = h;
    targetMinute = m;
    runDuration = d;
  }

  // ----------------------------
  // SET WEIGHT
  // ----------------------------
  else if (cmd.startsWith("WEIGHT,")) {
    targetWeight = cmd.substring(cmd.indexOf(',') + 1).toFloat();
    weightEnabled = true;
  }

  // ----------------------------
  // WEIGHT OFF
  // ----------------------------
  else if (cmd == "WEIGHTOFF") {
    weightEnabled = false;
  }

  // ----------------------------
  // TARE
  // ----------------------------
  else if (cmd == "TARE") {
    scale.tare();
  }

  // ----------------------------
  // SET CALIBRATION (ONLY EEPROM USE)
  // Example: CAL,-7050
  // ----------------------------
  else if (cmd.startsWith("CAL,")) {

    settings.calibration =
      cmd.substring(cmd.indexOf(',') + 1).toFloat();

    scale.set_scale(settings.calibration);

    EEPROM.put(0, settings); // ONLY EEPROM WRITE
  }
}

// ============================================================
// DISPLAY
// ============================================================
void sendTimeToNextion() {
  char buf[30];
  sprintf(buf, "t0.txt=\"%02d:%02d:%02d\"",
          currentHour, currentMinute, currentSecond);

  sendCommand(buf);
}

void sendWeightToNextion() {
  char buf[30];
  sprintf(buf, "t1.txt=\"%.1f g\"", currentWeight);

  sendCommand(buf);
}

void sendCommand(const char* cmd) {
  nextion.print(cmd);
  nextion.write(0xFF);
  nextion.write(0xFF);
  nextion.write(0xFF);
}

// ============================================================
// PARSER
// ============================================================
void parse3(String cmd, int &a, int &b, int &c) {

  int p1 = cmd.indexOf(',');
  int p2 = cmd.indexOf(',', p1 + 1);

  a = cmd.substring(p1 + 1, p2).toInt();
  b = cmd.substring(p2 + 1, cmd.lastIndexOf(',')).toInt();
  c = cmd.substring(cmd.lastIndexOf(',') + 1).toInt();
}