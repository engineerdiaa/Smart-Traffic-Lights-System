#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ==========================================
// PIN DEFINITIONS (LDRs named by traffic they watch)
// ==========================================
const int POT_PIN = A0;
const int LDR_TRAFFIC_A_PIN = A1;
const int LDR_TRAFFIC_B_PIN = A2;
const int FLASH_LED = A3;

const int TRAFFIC_A_R = 4;
const int TRAFFIC_A_Y = 5;
const int TRAFFIC_A_G = 6;

const int TRAFFIC_B_R = 7;
const int TRAFFIC_B_Y = 8;
const int TRAFFIC_B_G = 9;

const int PED_A_R = 10;
const int PED_A_G = 11;
const int PED_B_R = 12;
const int PED_B_G = 13;

const int BTN_A = 2;
const int BTN_B = 3;
const int BUZZER = 1;

// ==========================================
// SYSTEM VARIABLES
// ==========================================
LiquidCrystal_I2C lcd(0x27, 16, 2);

unsigned long currentMillis = 0;

// Interrupt Flags
volatile bool needPedA = false;
volatile bool needPedB = false;
volatile bool isPedAPressed = false;
volatile bool isPedBPressed = false;
volatile bool buzzRequest = false;

// State Machine
volatile byte currentState = 0;
unsigned long stateStartTime = 0;
unsigned long stateDuration = 5000;
bool stateShortened = false;

enum TrafficColor {
  TRAFFIC_RED,
  TRAFFIC_YELLOW,
  TRAFFIC_GREEN
};

TrafficColor trafficAColor = TRAFFIC_RED;
TrafficColor trafficBColor = TRAFFIC_RED;

// Potentiometer & Timing
unsigned long greenLightDuration = 5000;
volatile bool maintenanceMode = false;
int lastPotValue = -1;
const int POT_CHANGE_THRESHOLD = 8;

// LCD Interrupt Timer
unsigned long lcdOverrideTime = 0;
bool isLcdOverride = false;

// Camera Flash / Red-Light LDR Logic
const int LDR_TRAFFIC_B_DARK_THRESHOLD = 320;
const int LDR_TRAFFIC_B_LIGHT_THRESHOLD = 430;
const int LDR_TRAFFIC_A_DARK_THRESHOLD = 330;
const int LDR_TRAFFIC_A_LIGHT_THRESHOLD = 430;
const unsigned long LDR_CONFIRM_DELAY = 500;
const unsigned long FLASH_COOLDOWN = 2000;

struct LdrCamera {
  int pin;
  int darkThreshold;
  int lightThreshold;
  bool armed;
  bool pending;
  unsigned long triggerTime;
  unsigned long lastFlashTime;
};

LdrCamera cameraTrafficB = { LDR_TRAFFIC_B_PIN, LDR_TRAFFIC_B_DARK_THRESHOLD, LDR_TRAFFIC_B_LIGHT_THRESHOLD, false, false, 0, 0 };
LdrCamera cameraTrafficA = { LDR_TRAFFIC_A_PIN, LDR_TRAFFIC_A_DARK_THRESHOLD, LDR_TRAFFIC_A_LIGHT_THRESHOLD, false, false, 0, 0 };

// Phone-style camera flash timing
const unsigned long FLASH_PREFLASH_ON = 60;
const unsigned long FLASH_PREFLASH_GAP = 80;
const unsigned long FLASH_MAIN_ON = 160;
byte flashPhase = 0;
unsigned long flashPhaseStartTime = 0;
bool flashQueued = false;

// Buzzer Timer
unsigned long buzzEndTime = 0;

bool isTrafficARedState();
bool isTrafficBRedState();
bool canAcceptPedARequest();
bool canAcceptPedBRequest();
void buttonAPressed();
void buttonBPressed();
void readPotentiometer();
void runTrafficStateMachine();
void enterState(byte state);
void checkRedLightRunners();
void checkLdrSensor(LdrCamera &camera, bool trafficIsRed);
void resetCamera(LdrCamera &camera);
void stopCameraFlash();
void requestCameraFlash();
void handleBuzzerAndFlash();
void updatePhoneStyleFlash();
void runMaintenanceMode();
void updateLCD();
void setTrafficLights(bool ar, bool ay, bool ag, bool br, bool by, bool bg);
void setPedLights(bool ar, bool ag, bool br, bool bg);
TrafficColor trafficColorFromPins(bool red, bool yellow, bool green);
void printTrafficStatus();
void printTrafficLabel(const char *label, TrafficColor color);

// ==========================================
// STATE HELPERS
// ==========================================
bool isTrafficARedState() {
  return trafficAColor == TRAFFIC_RED;
}

bool isTrafficBRedState() {
  return trafficBColor == TRAFFIC_RED;
}

bool canAcceptPedARequest() {
  return currentState >= 3 && currentState <= 7;
}

bool canAcceptPedBRequest() {
  return currentState <= 2 || currentState >= 8;
}

// ==========================================
// HARDWARE INTERRUPTS
// ==========================================
void buttonAPressed() {
  if (!maintenanceMode && !isPedAPressed && canAcceptPedARequest()) {
    isPedAPressed = true;
    needPedA = true;
    buzzRequest = true;
  }
}

void buttonBPressed() {
  if (!maintenanceMode && !isPedBPressed && canAcceptPedBRequest()) {
    isPedBPressed = true;
    needPedB = true;
    buzzRequest = true;
  }
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  pinMode(TRAFFIC_A_R, OUTPUT); pinMode(TRAFFIC_A_Y, OUTPUT); pinMode(TRAFFIC_A_G, OUTPUT);
  pinMode(TRAFFIC_B_R, OUTPUT); pinMode(TRAFFIC_B_Y, OUTPUT); pinMode(TRAFFIC_B_G, OUTPUT);
  pinMode(PED_A_R, OUTPUT); pinMode(PED_A_G, OUTPUT);
  pinMode(PED_B_R, OUTPUT); pinMode(PED_B_G, OUTPUT);
  pinMode(FLASH_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  pinMode(BTN_A, INPUT_PULLUP);
  pinMode(BTN_B, INPUT_PULLUP);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  attachInterrupt(digitalPinToInterrupt(BTN_A), buttonAPressed, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_B), buttonBPressed, FALLING);

  setTrafficLights(HIGH, LOW, LOW, HIGH, LOW, LOW);
  setPedLights(HIGH, LOW, HIGH, LOW);

  delay(1000);
  currentMillis = millis();
  enterState(0);
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
  currentMillis = millis();

  readPotentiometer();
  handleBuzzerAndFlash();

  if (maintenanceMode) {
    runMaintenanceMode();
  } else {
    runTrafficStateMachine();
  }

  checkRedLightRunners();
  updateLCD();
}

// ==========================================
// 10-PHASE AUTOMATIC STATE MACHINE
// ==========================================
void runTrafficStateMachine() {

  // State 0 leads into state 3, where PED_B is served.
  if (currentState == 0 && needPedB) {
    if (!stateShortened && (currentMillis - stateStartTime < stateDuration - 1000)) {
      stateStartTime = currentMillis - stateDuration + 1000;
      stateShortened = true;
    }
  }

  // State 5 leads into state 8, where PED_A is served.
  if (currentState == 5 && needPedA) {
    if (!stateShortened && (currentMillis - stateStartTime < stateDuration - 1000)) {
      stateStartTime = currentMillis - stateDuration + 1000;
      stateShortened = true;
    }
  }

  if (currentMillis - stateStartTime >= stateDuration) {
    currentState++;
    if (currentState > 9) currentState = 0;
    enterState(currentState);
  }

  bool blinkPhase = (currentMillis / 500) % 2;
  if (currentState == 3) digitalWrite(PED_A_R, blinkPhase);
  if (currentState == 8) digitalWrite(PED_B_R, blinkPhase);
}

void enterState(byte state) {
  currentState = state;
  stateStartTime = currentMillis;
  stateShortened = false;

  switch(state) {
    case 0:
      setTrafficLights(HIGH, LOW, LOW,  LOW, LOW, HIGH);
      setPedLights(LOW, HIGH, HIGH, LOW);
      stateDuration = greenLightDuration;
      break;
    case 1:
      setTrafficLights(HIGH, LOW, LOW,  LOW, HIGH, LOW);
      stateDuration = 2000;
      break;
    case 2:
      setTrafficLights(HIGH, LOW, LOW,  HIGH, LOW, LOW);
      stateDuration = 1000;
      break;
    case 3:
      digitalWrite(PED_A_G, LOW);
      digitalWrite(PED_B_R, LOW); digitalWrite(PED_B_G, HIGH);
      needPedB = false;
      isPedBPressed = false;
      stateDuration = 3000;
      break;
    case 4:
      setPedLights(HIGH, LOW, LOW, HIGH);
      stateDuration = 1000;
      break;
    case 5:
      setTrafficLights(LOW, LOW, HIGH,  HIGH, LOW, LOW);
      setPedLights(HIGH, LOW, LOW, HIGH);
      stateDuration = greenLightDuration;
      break;
    case 6:
      setTrafficLights(LOW, HIGH, LOW,  HIGH, LOW, LOW);
      stateDuration = 2000;
      break;
    case 7:
      setTrafficLights(HIGH, LOW, LOW,  HIGH, LOW, LOW);
      stateDuration = 1000;
      break;
    case 8:
      digitalWrite(PED_B_G, LOW);
      digitalWrite(PED_A_R, LOW); digitalWrite(PED_A_G, HIGH);
      needPedA = false;
      isPedAPressed = false;
      stateDuration = 3000;
      break;
    case 9:
      setPedLights(LOW, HIGH, HIGH, LOW);
      stateDuration = 1000;
      break;
  }
}

// ==========================================
// HARDWARE SENSORS
// ==========================================
void readPotentiometer() {
  int potVal = analogRead(POT_PIN);
  bool newMaintenanceMode = potVal < 50;

  if (abs(potVal - lastPotValue) >= POT_CHANGE_THRESHOLD && lastPotValue != -1) {
    isLcdOverride = true;
    lcdOverrideTime = currentMillis;
    lastPotValue = potVal;
  }
  if (lastPotValue == -1) lastPotValue = potVal;

  if (newMaintenanceMode && !maintenanceMode) {
    maintenanceMode = true;
    resetCamera(cameraTrafficA);
    resetCamera(cameraTrafficB);
    stopCameraFlash();
  } else if (!newMaintenanceMode && maintenanceMode) {
    maintenanceMode = false;
    enterState(currentState);
  } else {
    maintenanceMode = newMaintenanceMode;
  }

  if (maintenanceMode) {
    return;
  } else {
    greenLightDuration = map(potVal, 50, 1023, 5000, 12000);

    if (!stateShortened && (currentState == 0 || currentState == 5)) {
        stateDuration = greenLightDuration;
    }
  }
}

void checkRedLightRunners() {
  checkLdrSensor(cameraTrafficB, isTrafficBRedState());
  checkLdrSensor(cameraTrafficA, isTrafficARedState());
}

void checkLdrSensor(LdrCamera &camera, bool trafficIsRed) {
  // 1. Check color FIRST. If it's not red, ignore entirely.
  if (maintenanceMode || !trafficIsRed) {
    resetCamera(camera);
    return;
  }

  // 2. Double-read the analog pin to clear out any residual voltage (crosstalk)
  analogRead(camera.pin); 
  int reading = analogRead(camera.pin);

  // 3. Normal threshold logic
  if (reading < camera.darkThreshold) {
    camera.armed = true;
    camera.pending = false;
    return;
  }

  if (reading > camera.lightThreshold && camera.armed && !camera.pending) {
    camera.pending = true;
    camera.triggerTime = currentMillis;
    return;
  }

  if (camera.pending && currentMillis - camera.triggerTime >= LDR_CONFIRM_DELAY) {
    if (reading > camera.lightThreshold && currentMillis - camera.lastFlashTime >= FLASH_COOLDOWN) {
      camera.lastFlashTime = currentMillis;
      requestCameraFlash();
    }

    camera.pending = false;
    camera.armed = false;
  }
}

void resetCamera(LdrCamera &camera) {
  camera.armed = false;
  camera.pending = false;
}

void stopCameraFlash() {
  flashPhase = 0;
  flashQueued = false;
  digitalWrite(FLASH_LED, LOW);
}

void requestCameraFlash() {
  if (flashPhase != 0) {
    flashQueued = true;
    return;
  }

  digitalWrite(FLASH_LED, HIGH);
  flashPhase = 1;
  flashPhaseStartTime = currentMillis;
  buzzRequest = true;
}

void handleBuzzerAndFlash() {
  updatePhoneStyleFlash();

  if (buzzRequest) {
    buzzEndTime = currentMillis + 50;
    buzzRequest = false;
  }

  if (currentMillis < buzzEndTime && !maintenanceMode) {
    digitalWrite(BUZZER, HIGH);
  } else if (!maintenanceMode) {
    digitalWrite(BUZZER, LOW);
  }
}

void updatePhoneStyleFlash() {
  if (flashPhase == 0) return;

  unsigned long phaseElapsed = currentMillis - flashPhaseStartTime;

  if (flashPhase == 1 && phaseElapsed >= FLASH_PREFLASH_ON) {
    digitalWrite(FLASH_LED, LOW);
    flashPhase = 2;
    flashPhaseStartTime = currentMillis;
  } else if (flashPhase == 2 && phaseElapsed >= FLASH_PREFLASH_GAP) {
    digitalWrite(FLASH_LED, HIGH);
    flashPhase = 3;
    flashPhaseStartTime = currentMillis;
  } else if (flashPhase == 3 && phaseElapsed >= FLASH_MAIN_ON) {
    digitalWrite(FLASH_LED, LOW);
    flashPhase = 0;

    if (flashQueued && !maintenanceMode) {
      flashQueued = false;
      requestCameraFlash();
    } else {
      flashQueued = false;
    }
  }
}

// ==========================================
// MAINTENANCE MODE
// ==========================================
void runMaintenanceMode() {
  bool yellowPhase = (currentMillis / 500) % 2;
  bool buzzPulse = (currentMillis % 4000) < 1000;

  setTrafficLights(LOW, yellowPhase, LOW, LOW, yellowPhase, LOW);
  setPedLights(HIGH, LOW, HIGH, LOW);
  digitalWrite(BUZZER, buzzPulse);

  needPedA = false;
  needPedB = false;
  isPedAPressed = false;
  isPedBPressed = false;
}

// ==========================================
// LCD DISPLAY
// ==========================================
void updateLCD() {
  static unsigned long lastUpdate = 0;
  if (currentMillis - lastUpdate < 200) return;
  lastUpdate = currentMillis;

  if (isLcdOverride) {
    if (currentMillis - lcdOverrideTime < 3000) {
      lcd.setCursor(0, 0);
      lcd.print("Timer Adjusted  ");
      lcd.setCursor(0, 1);
      if (maintenanceMode) {
        lcd.print("Maintenance     ");
      } else {
        lcd.print("Green: ");
        lcd.print((greenLightDuration + 500) / 1000);
        lcd.print(" sec       ");
      }
      return;
    } else {
      isLcdOverride = false;
      lcd.clear();
    }
  }

  if (maintenanceMode) {
    lcd.setCursor(0, 0);
    lcd.print(" SYSTEM OFFLINE ");
    lcd.setCursor(0, 1);
    bool blinkPhase = (currentMillis / 500) % 2;
    if (blinkPhase) lcd.print("*MAINTENANCE ON*");
    else lcd.print("                ");
    return;
  }

  lcd.setCursor(0, 0);
  printTrafficStatus();

  lcd.setCursor(0, 1);
  if (currentState == 3 || currentState == 8) lcd.print("PED: BLINKING   ");
  else if (needPedA || needPedB) lcd.print("PED: BTN PRESSED");
  else lcd.print("PED: AUTO CYCLE ");
}

// ==========================================
// LIGHT HELPERS
// ==========================================
void setTrafficLights(bool ar, bool ay, bool ag, bool br, bool by, bool bg) {
  digitalWrite(TRAFFIC_A_R, ar); digitalWrite(TRAFFIC_A_Y, ay); digitalWrite(TRAFFIC_A_G, ag);
  digitalWrite(TRAFFIC_B_R, br); digitalWrite(TRAFFIC_B_Y, by); digitalWrite(TRAFFIC_B_G, bg);

  trafficAColor = trafficColorFromPins(ar, ay, ag);
  trafficBColor = trafficColorFromPins(br, by, bg);
}

void setPedLights(bool ar, bool ag, bool br, bool bg) {
  digitalWrite(PED_A_R, ar); digitalWrite(PED_A_G, ag);
  digitalWrite(PED_B_R, br); digitalWrite(PED_B_G, bg);
}

TrafficColor trafficColorFromPins(bool red, bool yellow, bool green) {
  if (red) return TRAFFIC_RED;
  if (yellow) return TRAFFIC_YELLOW;
  if (green) return TRAFFIC_GREEN;
  return TRAFFIC_RED;
}

void printTrafficStatus() {
  printTrafficLabel("A:", trafficAColor);
  lcd.print("  ");
  printTrafficLabel("B:", trafficBColor);
  lcd.print("    ");
}

void printTrafficLabel(const char *label, TrafficColor color) {
  lcd.print(label);

  if (color == TRAFFIC_GREEN) {
    lcd.print("GO ");
  } else if (color == TRAFFIC_YELLOW) {
    lcd.print("YEL");
  } else {
    lcd.print("STP");
  }
}
