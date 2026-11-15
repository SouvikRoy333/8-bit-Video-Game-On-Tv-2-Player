// ESP32 Second Remote Tester

const int SECOND_REMOTE_PINS[] = {4, 33, 35};
const int NUM_PINS = 3;

void setup() {
  Serial.begin(115200);
  delay(1000);

  for (int i = 0; i < NUM_PINS; i++) {
    pinMode(SECOND_REMOTE_PINS[i], INPUT);
  }

  Serial.println("=================================");
  Serial.println("      SECOND REMOTE TESTER       ");
  Serial.println("=================================");
}

void loop() {
  Serial.print("SECOND REMOTE -> ");
  
  for (int i = 0; i < NUM_PINS; i++) {
    int pin = SECOND_REMOTE_PINS[i];
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