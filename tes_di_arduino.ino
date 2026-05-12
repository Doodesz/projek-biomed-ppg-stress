#include <Wire.h>
#include <MAX30105.h>

MAX30105 sensor;

void setup() {
  Serial.begin(9600);
  Serial.println("Initializing MAX30105...");

  Wire.begin();

  if (!sensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("Sensor not found. Check wiring!");
    while (1);
  }

  sensor.setup(); // konfigurasi default
}

void loop() {
  uint32_t irValue = sensor.getIR();
  uint32_t redValue = sensor.getRed();

  Serial.print("IR: ");
  Serial.print(irValue);
  Serial.print("   RED: ");
  Serial.println(redValue);

  delay(50);
}
