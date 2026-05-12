# Pembuatan Library Arduino: StressPredictor

Library **StressPredictor** berhasil dibuat dengan sempurna. Seluruh folder `StressPredictor` sekarang sudah menjadi sebuah *Library Arduino* standar yang siap digunakan.

## Fitur yang Digunakan
Model telah dilatih secara khusus untuk menerima 3 parameter input. Urutan parameter ini penting saat dipanggil di Arduino:
1. `RMSSD (ms)`
2. `SDNN (ms)`
3. `BPM` (Detak Jantung)

## Algoritma & Akurasi
Sesuai dengan permintaan Anda, model menggunakan algoritma **Random Forest**. 
Untuk memastikan modelnya ringan berjalan di mikrokontroler (Arduino), saya menggunakan `max_depth = 5` dan `n_estimators = 10`.

> [!TIP]
> Pada proses pelatihan menggunakan `DATA TRAINING.csv`, model Random Forest yang sangat ringan ini berhasil mendapatkan **Akurasi sebesar 98.97%** pada data validasi.

## Struktur Direktori Library
Folder `StressPredictor` telah dibuat dengan isi sebagai berikut:
```text
StressPredictor/
├── src/
│   ├── [StressPredictor.h](file:///Users/sir/Documents/Projects/biomedSignal/StressPredictor/src/StressPredictor.h)
│   ├── [StressPredictor.cpp](file:///Users/sir/Documents/Projects/biomedSignal/StressPredictor/src/StressPredictor.cpp)
│   └── [model.h](file:///Users/sir/Documents/Projects/biomedSignal/StressPredictor/src/model.h)
├── examples/
│   └── BasicPrediction/
│       └── [BasicPrediction.ino](file:///Users/sir/Documents/Projects/biomedSignal/StressPredictor/examples/BasicPrediction/BasicPrediction.ino)
├── [keywords.txt](file:///Users/sir/Documents/Projects/biomedSignal/StressPredictor/keywords.txt)
└── [library.properties](file:///Users/sir/Documents/Projects/biomedSignal/StressPredictor/library.properties)
```

## Cara Penggunaan di Komputer Anda
1. Pindahkan atau salin folder `StressPredictor` yang baru saja saya buat ke dalam folder instalasi library Arduino Anda (biasanya di `Documents/Arduino/libraries/`).
2. Buka aplikasi Arduino IDE.
3. Buka menu **File** -> **Examples** -> **StressPredictor** -> **BasicPrediction**.
4. Anda bisa langsung melakukan Compile dan Upload ke Mikrokontroler Anda (Arduino Uno, ESP32, atau lainnya).

Fungsi intinya sangat mudah dipanggil:
```cpp
#include <StressPredictor.h>

StressPredictor stressModel;

void setup() {
  // Masukkan nilai RMSSD, SDNN, dan BPM
  int hasil = stressModel.predict(79.01, 85.38, 86.97);
  
  // hasil = 0 (Rendah), 1 (Sedang), 2 (Tinggi)
}
```
