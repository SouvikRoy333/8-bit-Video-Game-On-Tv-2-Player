#include <SPI.h>
#include <SD.h>

// SD Card Chip Select pin
const int SD_CS = 5;

// --- Player 1 Pins ---
const int P1_LR   = 34;
const int P1_UD   = 14;
const int P1_SEL  = 15;
const int P1_FIRE = 32;

// --- Player 2 Pins ---
const int P2_UD   = 4;   
const int P2_LR   = 33;  
const int P2_FJ   = 35;  

void setup() {
  Serial.begin(115200);
  while (!Serial);

  Serial.println("\n--- Dual Controller Diagnostic Test ---");
  Serial.println("Press buttons on Player 1 or Player 2 to test inputs...");

  // Initialize SD Card just to ensure it mounts
  if (!SD.begin(SD_CS)) {
    Serial.println("Warning: SD Card failed to initialize (check wiring).");
  } else {
    Serial.println("SD Card initialized successfully.");
  }
}

void loop() {
  // ==================== PLAYER 1 READINGS ====================
  int p1_lr   = analogRead(P1_LR);
  int p1_ud   = analogRead(P1_UD);
  int p1_sel  = analogRead(P1_SEL);
  int p1_fire = analogRead(P1_FIRE);

  // P1 Left / Right
  if (p1_lr >= 200 && p1_lr <= 400)      Serial.println("P1: RIGHT");
  else if (p1_lr >= 750 && p1_lr <= 850) Serial.println("P1: LEFT");

  // P1 Up / Down
  if (p1_ud >= 200 && p1_ud <= 400)      Serial.println("P1: UP");
  else if (p1_ud >= 750 && p1_ud <= 850) Serial.println("P1: DOWN");

  // P1 Left Select & Fire
  if (p1_sel >= 200 && p1_sel <= 400)    Serial.println("P1: LEFT SELECT");
  else if (p1_sel >= 790 && p1_sel <= 825) Serial.println("P1: FIRE (Left)");

  // P1 Right Select & Jump
  if (p1_fire >= 200 && p1_fire <= 400)  Serial.println("P1: RIGHT SELECT");
  else if (p1_fire >= 790 && p1_fire <= 825) Serial.println("P1: JUMP (Right)");


  // ==================== PLAYER 2 READINGS ====================
  int p2_ud = analogRead(P2_UD);
  int p2_lr = analogRead(P2_LR);
  int p2_fj = analogRead(P2_FJ);

  // P2 Up / Down (D4)
  if (p2_ud >= 550 && p2_ud <= 650)      Serial.println("P2: UP");
  else if (p2_ud >= 300 && p2_ud <= 400) Serial.println("P2: DOWN");

  // P2 Left / Right (D33)
  if (p2_lr >= 300 && p2_lr <= 400)      Serial.println("P2: RIGHT");
  else if (p2_lr >= 550 && p2_lr <= 650) Serial.println("P2: LEFT");

  // P2 Fire / Jump (D35)
  if (p2_fj >= 300 && p2_fj <= 400)      Serial.println("P2: FIRE");
  else if (p2_fj >= 200 && p2_fj <= 290) Serial.println("P2: JUMP");
  else if (p2_fj >= 600 && p2_fj <= 650) Serial.println("P2: FIRE + JUMP COMBO");

  delay(100); // Small delay to keep the Serial monitor readable
}