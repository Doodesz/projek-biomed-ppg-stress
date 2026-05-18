// work, tapi bukan dari kode original paper

#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "../Model/model.h"


MAX30105 particleSensor;

// ===== HRV CONFIG =====
const byte RR_SIZE = 15;
float rrList[RR_SIZE];
byte rrCount = 0;

unsigned long lastBeat = 0;
float lastBPM = -1.0;
const float BPM_THRESHOLD = 25.0;
const byte CALIBRATION_BEATS = 5;
float calibrationBpmSum = 0;
byte calibrationBpmCount = 0;
bool isCalibrating = true;
float restingBPM = -1.0;

// Stress Predictor
Eloquent::ML::Port::StressPredictorRF stressModel;

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
    rrCount = 0;
    lastBPM = -1.0;
    calibrationBpmSum = 0;
    calibrationBpmCount = 0;
    isCalibrating = true;
    restingBPM = -1.0;
    delay(200);
    return;
  }

  if (checkForBeat(irValue)) {
    unsigned long now = millis();
    unsigned long rr = now - lastBeat;
    lastBeat = now;
    
    float bpm = 60000.0 / rr;
    
    if (bpm < 35 || bpm > 220) {
      Serial.println("BPM tidak masuk akal, diabaikan.");
      return;
    }
    
    if (isCalibrating) {
      calibrationBpmSum += bpm;
      calibrationBpmCount++;
      
      Serial.print("Kalibrasi BPM: ");
      Serial.print(calibrationBpmCount);
      Serial.print("/");
      Serial.println(CALIBRATION_BEATS);
      
      if (calibrationBpmCount < CALIBRATION_BEATS) {
        return;
      }

      restingBPM = calibrationBpmSum / calibrationBpmCount;
      lastBPM = restingBPM;
      isCalibrating = false;

      Serial.print("Resting BPM terkalibrasi: ");
      Serial.println(restingBPM, 1);
      return;
    }
    
    // Check if BPM is too different from last reading (outlier detection)
    if (lastBPM >= 0 && abs(bpm - lastBPM) > BPM_THRESHOLD) {
      Serial.println("BPM melompat terlalu jauh, diabaikan.");
      return;
    }

    if (rrCount < RR_SIZE) {
      rrList[rrCount++] = rr;
    }

    lastBPM = bpm;

    Serial.print("BPM: ");
    Serial.print(bpm);

    if (rrCount >= RR_SIZE) {
      float sdnn  = computeSDNN(rrList, RR_SIZE);
      float rmssd = computeRMSSD(rrList, RR_SIZE);

      Serial.print(" | SDNN: ");
      Serial.print(sdnn, 2);

      Serial.print(" | RMSSD: ");
      Serial.print(rmssd, 2);

      float features[3] = {rmssd, sdnn, bpm};
      int stress = stressModel.predict(features);

      Serial.print(" | STRESS: ");
      Serial.println(stress);
      Serial.println("/2 (0: Rendah, 1: Sedang, 2: Tinggi)");

      rrCount = 0;
    } else {
      Serial.print(" | Mengumpulkan RR: ");
      Serial.println(rrCount);
    }
  }
}
