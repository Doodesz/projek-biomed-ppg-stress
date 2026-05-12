// work sebagian

#include <StressPredictor.h>
#include <Wire.h>
#include "MAX30105.h"

// Pin I2C untuk ESP32
#define SDA_PIN 21
#define SCL_PIN 22

// Inisialisasi object dari library
StressPredictor stressModel;
MAX30105 particleSensor;

// Buffer untuk menyimpan data Heart Rate (RR interval dalam ms)
// Jendela yang lebih kecil membuat hasil klasifikasi keluar lebih cepat.
#define BUFFER_SIZE 32
uint32_t rrIntervals[BUFFER_SIZE];
uint16_t bufferIndex = 0;
unsigned long lastBeatTime = 0;
unsigned long currentBeatTime = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial);

  // Inisialisasi I2C dengan pin custom
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000); // 400kHz I2C clock

  Serial.println("=== Sistem Prediksi Stres Real-time ===");
  Serial.println("Menginisialisasi I2C pada SDA: 21, SCL: 22");
  Serial.println("Menginisialisasi sensor MAX30105...");
  
  // Cek apakah sensor terdeteksi
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("Gagal mendeteksi sensor MAX30105. Periksa kabel!");
    while (1);
  }
  
  Serial.println("Sensor MAX30105 terdeteksi!");

  // Konfigurasi Sensor (Parameter standar untuk deteksi detak jantung)
  byte ledBrightness = 0x1F; // 0=Off to 255=50mA
  byte sampleAverage = 4;    // 1, 2, 4, 8, 16, 32
  byte ledMode = 2;          // 1 = Red only, 2 = Red + IR
  int sampleRate = 100;      // 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth = 411;      // 69, 118, 215, 411
  int adcRange = 4096;       // 2048, 4096, 8192, 16384

  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
  
  Serial.println("Siap membaca data dari sensor...");
  Serial.println("Letakkan jari pada sensor untuk memulai...");
  delay(2000);
}

void loop() {
  // Baca data dari sensor I2C
  // Adjust I2C address sesuai sensor yang Anda gunakan (default 0x57 untuk MAX30102)
  readSensorData();
  
  // Jika buffer sudah penuh dengan 256 data point
  if (bufferIndex >= BUFFER_SIZE) {
    // Hitung RMSSD, SDNN, dan BPM dari RR intervals
    float rmssd = calculateRMSSD();
    float sdnn = calculateSDNN();
    float bpm = calculateBPM();
    
    // Lakukan prediksi
    prediksiDanPrint("Real-time Sensor", rmssd, sdnn, bpm);
    
    // Reset buffer
    bufferIndex = 0;
  }
  
  delay(10); // Sampling interval 10ms
}

void readSensorData() {
  // Ambil sampel terbaru dari FIFO sensor sebelum membaca IR/Red
  while (particleSensor.available() == false) {
    particleSensor.check();
  }

  // Ambil data IR dan Red dari sensor MAX30105
  uint32_t irValue = particleSensor.getIR();
  uint32_t redValue = particleSensor.getRed();
  particleSensor.nextSample();

  if (irValue < 50000) {
    // Jika tidak ada jari (nilai rendah)
    Serial.print("Status: Letakkan Jari! IR: ");
    Serial.println(irValue);
  } else {
    // Jika ada jari, deteksi beat
    // Deteksi beat ketika IR melewati threshold
    static uint32_t lastIRValue = 0;
    static bool wasLow = false;
    
    if (irValue < lastIRValue && wasLow == false) {
      // Peak terdeteksi (naik kemudian turun)
      currentBeatTime = millis();
      if (lastBeatTime > 0) {
        uint32_t rrInterval = currentBeatTime - lastBeatTime;
        if (rrInterval > 300 && rrInterval < 2000) { // Filter outliers
          rrIntervals[bufferIndex++] = rrInterval;
        }
      }
      lastBeatTime = currentBeatTime;
      wasLow = true;
    }
    if (irValue > lastIRValue) {
      wasLow = false;
    }
    lastIRValue = irValue;
  }
}

float calculateRMSSD() {
  // Root Mean Square of Successive Differences
  if (bufferIndex < 2) return 0;
  
  float sumSquares = 0;
  for (uint16_t i = 1; i < bufferIndex; i++) {
    int32_t diff = rrIntervals[i] - rrIntervals[i-1];
    sumSquares += (diff * diff);
  }
  return sqrt(sumSquares / (bufferIndex - 1));
}

float calculateSDNN() {
  // Standard Deviation of NN intervals
  if (bufferIndex < 2) return 0;
  
  float mean = 0;
  for (uint16_t i = 0; i < bufferIndex; i++) {
    mean += rrIntervals[i];
  }
  mean /= bufferIndex;
  
  float sumSquares = 0;
  for (uint16_t i = 0; i < bufferIndex; i++) {
    float diff = rrIntervals[i] - mean;
    sumSquares += (diff * diff);
  }
  return sqrt(sumSquares / bufferIndex);
}

float calculateBPM() {
  // Calculate BPM from mean RR interval
  if (bufferIndex < 1) return 0;
  
  float meanInterval = 0;
  for (uint16_t i = 0; i < bufferIndex; i++) {
    meanInterval += rrIntervals[i];
  }
  meanInterval /= bufferIndex;
  
  return (60000.0 / meanInterval); // Convert ms to BPM
}

void prediksiDanPrint(String label, float rmssd, float sdnn, float bpm) {
  Serial.print(label);
  Serial.print(" | RMSSD: "); Serial.print(rmssd);
  Serial.print(", SDNN: "); Serial.print(sdnn);
  Serial.print(", BPM: "); Serial.print(bpm);
  
  Serial.print(" | Memproses klasifikasi...");
  int hasil = stressModel.predict(rmssd, sdnn, bpm);
  
  Serial.print(" => HASIL: ");
  if (hasil == 0) {
    Serial.println("Stres Rendah (0)");
  } else if (hasil == 1) {
    Serial.println("Stres Sedang (1)");
  } else if (hasil == 2) {
    Serial.println("Stres Tinggi (2)");
  } else {
    Serial.println("Tidak Diketahui");
  }
}
