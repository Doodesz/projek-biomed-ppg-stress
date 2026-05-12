// work

#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <StressPredictor.h>

MAX30105 particleSensor;

// ===== HRV CONFIG =====
const byte RR_SIZE = 15;
float rrList[RR_SIZE];
byte rrCount = 0;

unsigned long lastBeat = 0;

// Stress Predictor
StressPredictor stressModel;

void setup() {
  Serial.begin(115200);
  while (!Serial);

  Serial.println("=== HRV + Prediksi Stres ===");
  Serial.println("Inisialisasi sensor MAX30102...");

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("Sensor tidak ditemukan!");
    while (1);
  }

  particleSensor.setup();  
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeGreen(0);

  Serial.println("Sensor siap.");
}

float computeSDNN(float *rr, int n) {
  if (n < 2) return 0;

  float mean = 0;
  for (int i = 0; i < n; i++) mean += rr[i];
  mean /= n;

  float sumSq = 0;
  for (int i = 0; i < n; i++) {
    float diff = rr[i] - mean;
    sumSq += diff * diff;
  }

  return sqrt(sumSq / (n - 1));
}

float computeRMSSD(float *rr, int n) {
  if (n < 2) return 0;

  float sumSq = 0;
  int count = 0;

  for (int i = 1; i < n; i++) {
    float diff = rr[i] - rr[i - 1];
    sumSq += diff * diff;
    count++;
  }

  return sqrt(sumSq / count);
}

void loop() {
  long irValue = particleSensor.getIR();

  if (irValue < 50000) {
    Serial.println("Tempelkan jari...");
    delay(200);
    return;
  }

  if (checkForBeat(irValue)) {
    unsigned long now = millis();
    unsigned long rr = now - lastBeat;
    lastBeat = now;

    if (rrCount < RR_SIZE) {
      rrList[rrCount++] = rr;
    }

    float bpm = 60000.0 / rr;

    Serial.print("BPM: ");
    Serial.print(bpm);

    if (rrCount >= RR_SIZE) {
      float sdnn  = computeSDNN(rrList, RR_SIZE);
      float rmssd = computeRMSSD(rrList, RR_SIZE);

      Serial.print(" | SDNN: ");
      Serial.print(sdnn, 2);

      Serial.print(" | RMSSD: ");
      Serial.print(rmssd, 2);

      int stress = stressModel.predict(rmssd, sdnn, bpm);

      Serial.print(" | STRESS: ");
      Serial.println(stress);

      rrCount = 0;
    } else {
      Serial.print(" | Mengumpulkan RR: ");
      Serial.println(rrCount);
    }
  }
}
