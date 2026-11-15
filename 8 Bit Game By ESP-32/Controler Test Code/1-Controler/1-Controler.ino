// ESP32 Main Remote Tester

const int MAIN_REMOTE_PINS[] = {14, 15, 32, 34};
const int NUM_PINS = 4;

void setup() {
  Serial.begin(115200);
  delay(1000);

  for (int i = 0; i < NUM_PINS; i++) {
    pinMode(MAIN_REMOTE_PINS[i], INPUT);
  }

  Serial.println("=================================");
  Serial.println("       MAIN REMOTE TESTER        ");
  Serial.println("=================================");
}

void loop() {
  Serial.print("MAIN REMOTE -> ");
  
  for (int i = 0; i < NUM_PINS; i++) {
    int pin = MAIN_REMOTE_PINS[i];
    int val = analogRead(pin);
    
    Serial.print("D");
    Serial.print(pin);
    Serial.print(": ");
    Serial.print(val);
    
    if (i < NUM_PINS - 1) {
      Serial.print("  |  ");
    }
  }

  Serial.println();
  delay(500); // 0.5 second pause between readings for easy scanning
}