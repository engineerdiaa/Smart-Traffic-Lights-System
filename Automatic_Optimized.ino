// Smart Traffic Lights by @diaawastaken · github.com/engineerdiaa/Smart-Traffic-Lights-System · MIT

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

const byte POT_PIN = A0, LDR_A_PIN = A1, LDR_B_PIN = A2, FLASH_LED = A3, BTN_A = 2, BTN_B = 3, BUZZER = 1;
const byte OUT_PINS[10] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 13}; // Pins for A_RYG, B_RYG, PA_RG, PB_RG
const byte PED_A_R = 10, PED_B_R = 12;

LiquidCrystal_I2C lcd(0x27, 16, 2);

unsigned long currentMillis = 0, stateStartTime = 0, stateDuration = 5000;
unsigned long lcdOverrideTime = 0, flashPhaseStartTime = 0, buzzEndTime = 0;
unsigned long greenDuration = 5000;
volatile byte currentState = 0;
volatile bool needPedA = false, needPedB = false, isPedAPressed = false, isPedBPressed = false, buzzReq = false, maintMode = false;
bool stateShortened = false, isLcdOverride = false, flashQueued = false;
int lastPot = -1;
byte flashPhase = 0;

struct LdrCam { byte pin; int dark; int light; bool armed; bool pending; unsigned long trig; unsigned long flash; };
LdrCam camB = { LDR_B_PIN, 320, 430, false, false, 0, 0 };
LdrCam camA = { LDR_A_PIN, 330, 430, false, false, 0, 0 };

// Data-Driven State Machine (Binary representation of which pins are HIGH for each state)
const uint16_t STATE_PINS[10] = {417, 401, 393, 521, 585, 588, 586, 585, 137, 393};
const uint16_t STATE_DURS[10] = {0, 2000, 1000, 3000, 1000, 0, 2000, 1000, 3000, 1000}; // 0 triggers greenDuration override

void setup() {
  for (byte p : OUT_PINS) pinMode(p, OUTPUT);
  pinMode(FLASH_LED, OUTPUT); pinMode(BUZZER, OUTPUT);
  pinMode(BTN_A, INPUT_PULLUP); pinMode(BTN_B, INPUT_PULLUP);

  lcd.init(); lcd.backlight(); lcd.clear();
  attachInterrupt(digitalPinToInterrupt(BTN_A), btnA_ISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_B), btnB_ISR, FALLING);

  // Initial ALL RED state before starting normal cycle
  uint16_t bootMask = (1<<0) | (1<<3) | (1<<6) | (1<<8); 
  for (int i = 0; i < 10; i++) digitalWrite(OUT_PINS[i], (bootMask >> i) & 1);
  delay(1000);
  
  currentMillis = millis();
  enterState(0);
}

void loop() {
  currentMillis = millis();
  readPot();
  handleFlashAndBuzzer();

  if (maintMode) runMaint();
  else runLogic();

  // Color logic inferred entirely from the current state integer
  checkLdr(camB, currentState >= 2);
  checkLdr(camA, currentState != 5 && currentState != 6);
  updateLCD();
}

void btnA_ISR() {
  if (!maintMode && !isPedAPressed && currentState >= 3 && currentState <= 7) 
    isPedAPressed = needPedA = buzzReq = true;
}

void btnB_ISR() {
  if (!maintMode && !isPedBPressed && (currentState <= 2 || currentState >= 8)) 
    isPedBPressed = needPedB = buzzReq = true;
}

void enterState(byte st) {
  currentState = st;
  stateStartTime = currentMillis;
  stateShortened = false;

  // Apply bitmask to the 10 sequential pins
  for (int i = 0; i < 10; i++) digitalWrite(OUT_PINS[i], (STATE_PINS[st] >> i) & 1);
  stateDuration = (STATE_DURS[st] == 0) ? greenDuration : STATE_DURS[st];

  if (st == 3) needPedB = isPedBPressed = false;
  if (st == 8) needPedA = isPedAPressed = false;
}

void runLogic() {
  if (!stateShortened && ((currentState == 0 && needPedB) || (currentState == 5 && needPedA))) {
    if (currentMillis - stateStartTime < stateDuration - 1000) {
      stateStartTime = currentMillis - stateDuration + 1000;
      stateShortened = true;
    }
  }
  if (currentMillis - stateStartTime >= stateDuration) enterState((currentState + 1) % 10);

  if (currentState == 3) digitalWrite(PED_A_R, (currentMillis / 500) % 2);
  if (currentState == 8) digitalWrite(PED_B_R, (currentMillis / 500) % 2);
}

void runMaint() {
  bool yp = (currentMillis / 500) % 2;
  uint16_t mask = (yp<<1) | (yp<<4) | (1<<6) | (1<<8); // Blink Traffic Yellows (bits 1/4), Peds stay Red (bits 6/8)
  for (int i = 0; i < 10; i++) digitalWrite(OUT_PINS[i], (mask >> i) & 1);
  digitalWrite(BUZZER, (currentMillis % 4000) < 1000);
  needPedA = needPedB = isPedAPressed = isPedBPressed = false;
}

void readPot() {
  int val = analogRead(POT_PIN);
  bool newMaint = val < 50;

  if (abs(val - lastPot) >= 8 && lastPot != -1) {
    isLcdOverride = true; lcdOverrideTime = currentMillis; lastPot = val;
  }
  if (lastPot == -1) lastPot = val;

  if (newMaint != maintMode) {
    maintMode = newMaint;
    if (newMaint) { camA.armed = camA.pending = camB.armed = camB.pending = false; stopFlash(); }
    else enterState(currentState);
  }

  if (!maintMode) {
    greenDuration = map(val, 50, 1023, 5000, 12000);
    if (!stateShortened && (currentState == 0 || currentState == 5)) stateDuration = greenDuration;
  }
}

void checkLdr(LdrCam &cam, bool isRed) {
  if (maintMode || !isRed) { cam.armed = cam.pending = false; return; }
  analogRead(cam.pin); // ADC Cross-talk fix
  int val = analogRead(cam.pin);

  if (val < cam.dark) { cam.armed = true; cam.pending = false; return; }
  if (val > cam.light && cam.armed && !cam.pending) { cam.pending = true; cam.trig = currentMillis; return; }

  if (cam.pending && currentMillis - cam.trig >= 500) {
    if (val > cam.light && currentMillis - cam.flash >= 2000) {
      cam.flash = currentMillis;
      if (flashPhase == 0) { digitalWrite(FLASH_LED, HIGH); flashPhase = 1; flashPhaseStartTime = currentMillis; buzzReq = true; }
      else flashQueued = true;
    }
    cam.pending = cam.armed = false;
  }
}

void stopFlash() { flashPhase = 0; flashQueued = false; digitalWrite(FLASH_LED, LOW); }

void handleFlashAndBuzzer() {
  if (flashPhase > 0) {
    unsigned long el = currentMillis - flashPhaseStartTime;
    if (flashPhase == 1 && el >= 60) { digitalWrite(FLASH_LED, LOW); flashPhase = 2; flashPhaseStartTime = currentMillis; }
    else if (flashPhase == 2 && el >= 80) { digitalWrite(FLASH_LED, HIGH); flashPhase = 3; flashPhaseStartTime = currentMillis; }
    else if (flashPhase == 3 && el >= 160) {
      digitalWrite(FLASH_LED, LOW); flashPhase = 0;
      if (flashQueued && !maintMode) {
        digitalWrite(FLASH_LED, HIGH); flashPhase = 1; flashPhaseStartTime = currentMillis; buzzReq = true;
      }
      flashQueued = false;
    }
  }
  if (buzzReq) { buzzEndTime = currentMillis + 50; buzzReq = false; }
  digitalWrite(BUZZER, (!maintMode && currentMillis < buzzEndTime) ? HIGH : LOW);
}

void updateLCD() {
  static unsigned long lastUpd = 0;
  if (currentMillis - lastUpd < 200) return;
  lastUpd = currentMillis;

  if (isLcdOverride) {
    if (currentMillis - lcdOverrideTime < 3000) {
      lcd.setCursor(0, 0); lcd.print("Timer Adjusted  ");
      lcd.setCursor(0, 1);
      if (maintMode) lcd.print("Maintenance     ");
      else { lcd.print("Green: "); lcd.print((greenDuration + 500) / 1000); lcd.print(" sec       "); }
      return;
    } else { isLcdOverride = false; lcd.clear(); }
  }

  if (maintMode) {
    lcd.setCursor(0, 0); lcd.print(" SYSTEM OFFLINE ");
    lcd.setCursor(0, 1);
    lcd.print(((currentMillis / 500) % 2) ? "*MAINTENANCE ON*" : "                ");
    return;
  }

  lcd.setCursor(0, 0);
  printLabel("A:", 5, 6); lcd.print("  ");
  printLabel("B:", 0, 1); lcd.print("    ");

  lcd.setCursor(0, 1);
  if (currentState == 3 || currentState == 8) lcd.print("PED: BLINKING   ");
  else if (needPedA || needPedB) lcd.print("PED: BTN PRESSED");
  else lcd.print("PED: AUTO CYCLE ");
}

void printLabel(const char *lbl, byte gState, byte yState) {
  lcd.print(lbl);
  if (currentState == gState) lcd.print("GO ");
  else if (currentState == yState) lcd.print("YEL");
  else lcd.print("STP");
}