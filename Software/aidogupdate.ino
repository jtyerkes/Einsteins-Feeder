// ----------------------------
// Arduino Full Code for Nextion + Stepper + Schedule + Live Clock
// ----------------------------

#define STEP_PIN 3
#define DIR_PIN 4

#include <SoftwareSerial.h>
SoftwareSerial nextion(10, 11); // RX, TX for Nextion (adjust if needed)

const int stepDelay = 500; // microseconds per step
bool motorRunning = false;
bool motorDirection = true; // true = CW, false = CCW

// Scheduled run variables
int targetHour = -1;
int targetMinute = -1;
int runDuration = 0; // seconds
unsigned long runStartMillis = 0;
bool scheduledRunActive = false;
static int lastTriggerMinute = -1;

// Simple clock using millis()
int currentHour = 0;
int currentMinute = 0;

// ----------------------------
// Setup
// ----------------------------
void setup() {
  Serial.begin(9600);      // Serial monitor
  nextion.begin(9600);     // Nextion serial
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
}

// ----------------------------
// Loop
// ----------------------------
void loop() {
  handleNextion();
  updateClock();
  checkSchedule();
  checkAutoStop();
  runMotor();
}

// ----------------------------
// Motor stepping function
// ----------------------------
void runMotor() {
  if (motorRunning) {
    digitalWrite(DIR_PIN, motorDirection);
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(stepDelay);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(stepDelay);
  }
}

// ----------------------------
// Clock update
// ----------------------------
void updateClock() {
  unsigned long totalSeconds = millis() / 1000;
  currentMinute = (totalSeconds / 60) % 60;
  currentHour = (totalSeconds / 3600) % 24;
}

// ----------------------------
// Handle Nextion commands
// ----------------------------
void handleNextion() {
  if (nextion.available()) {
    String cmd = nextion.readString();
    cmd.trim();

    if (cmd.startsWith("START")) {
      int commaIndex = cmd.indexOf(',');
      int dirVal = cmd.substring(commaIndex + 1).toInt();
      motorDirection = (dirVal == 0);
      motorRunning = true;
    } 
    else if (cmd == "STOP") {
      motorRunning = false;
      scheduledRunActive = false;
    } 
    else if (cmd.startsWith("SET")) {
      // Parse scheduled start
      int p1 = cmd.indexOf(',');
      int p2 = cmd.indexOf(',', p1 + 1);
      int p3 = cmd.indexOf(',', p2 + 1);

      if (p1 > 0 && p2 > p1 && p3 > p2) {
        targetHour = cmd.substring(p1 + 1, p2).toInt();
        if(targetHour < 0) targetHour = 0;
        if(targetHour > 23) targetHour = 23;

        targetMinute = cmd.substring(p2 + 1, p3).toInt();
        if(targetMinute < 0) targetMinute = 0;
        if(targetMinute > 59) targetMinute = 59;

        runDuration = cmd.substring(p3 + 1).toInt();
        if(runDuration < 1) runDuration = 1;
      }
    }
    else if (cmd == "GETTIME") {
      sendTimeToNextion();
    }
  }
}

// ----------------------------
// Scheduled run check
// ----------------------------
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

// ----------------------------
// Auto-stop after duration
// ----------------------------
void checkAutoStop() {
  if (scheduledRunActive) {
    if (millis() - runStartMillis >= (unsigned long)runDuration * 1000) {
      motorRunning = false;
      scheduledRunActive = false;
    }
  }
}

// ----------------------------
// Send live clock to Nextion
// ----------------------------
void sendTimeToNextion() {
  char buffer[20];
  int seconds = (millis() / 1000) % 60;
  sprintf(buffer, "t0.txt=\"%02d:%02d:%02d\"", currentHour, currentMinute, seconds);
  nextion.print(buffer);
  nextion.write(0xFF);
  nextion.write(0xFF);
  nextion.write(0xFF);
}