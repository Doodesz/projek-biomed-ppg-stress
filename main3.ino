#include <StressPredictor.h>
#include <Wire.h>

// Pin I2C untuk ESP32
#define SDA_PIN 21
#define SCL_PIN 22

// Inisialisasi object dari library
StressPredictor stressModel;

// Buffer untuk menyimpan data Heart Rate (RR interval dalam ms)
#define BUFFER_SIZE 256
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
  Serial.println("Menginisialisasi model Random Forest...");
  delay(1000);
  
  Serial.println("\nSiap membaca data dari sensor...");
  Serial.println("Menunggu 10 detik untuk kalibrasi sensor...");
  delay(10000);
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
  // Baca data dari sensor I2C
  // CATATAN: Sesuaikan I2C address dan register sesuai sensor Anda
  // Contoh untuk MAX30102: address = 0x57
  
  Wire.beginTransmission(0x57); // Ubah address sesuai sensor Anda
  Wire.write(0x07); // Register FIFO data (sesuaikan dengan sensor)
  Wire.endTransmission();
  
  Wire.requestFrom(0x57, 6); // Minta 6 bytes data (sesuaikan dengan sensor)
  if (Wire.available() >= 6) {
    uint32_t irValue = 0;
    uint32_t redValue = 0;
    
    irValue = (Wire.read() << 16) | (Wire.read() << 8) | Wire.read();
    redValue = (Wire.read() << 16) | (Wire.read() << 8) | Wire.read();
    
    // Deteksi beat (simplified)
    if (irValue > 50000) { // Threshold untuk beat detection
      currentBeatTime = millis();
      if (lastBeatTime > 0) {
        uint32_t rrInterval = currentBeatTime - lastBeatTime;
        if (rrInterval > 300 && rrInterval < 2000) { // Filter outliers
          rrIntervals[bufferIndex++] = rrInterval;
        }
      }
      lastBeatTime = currentBeatTime;
    }
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
