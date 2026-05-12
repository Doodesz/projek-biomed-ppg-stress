#include <Wire.h>
#include "MAX30105.h"

// ========== I2C PIN SETUP (Sesuai kode asli Anda) ==========
const int SDA_PIN = 21;
const int SCL_PIN = 22;

MAX30105 particleSensor;

void setup() {
  Serial.begin(115200);
  Serial.println("MAX3010x Basic Read Test");

  // Inisialisasi I2C dengan pin khusus Anda
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);

  // Cek apakah sensor terdeteksi
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("Gagal mendeteksi sensor MAX3010x. Periksa kabel!");
    while (1);
  }

  Serial.println("Sensor terdeteksi!");

  // Konfigurasi Sensor (Parameter standar untuk deteksi detak jantung)
  byte ledBrightness = 0x1F; // 0=Off to 255=50mA
  byte sampleAverage = 4;    // 1, 2, 4, 8, 16, 32
  byte ledMode = 2;          // 1 = Red only, 2 = Red + IR
  int sampleRate = 100;      // 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth = 411;      // 69, 118, 215, 411
  int adcRange = 4096;       // 2048, 4096, 8192, 16384

  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
}

void loop() {
  // Ambil data IR dan Red
  uint32_t irValue = particleSensor.getIR();
  uint32_t redValue = particleSensor.getRed();

  if (irValue < 50000) {
    // Jika tidak ada jari (nilai rendah)
    Serial.print("Status: Letakkan Jari! ");
    Serial.print(" IR:");
    Serial.println(irValue);
  } else {
    // Jika ada jari, tampilkan format untuk Serial Plotter
    // Format: IR, Red
    Serial.print(irValue);
    Serial.print(",");
    Serial.println(redValue);
  }

  // Delay kecil agar tidak terlalu cepat memenuhi buffer serial
  delay(10); 
}
