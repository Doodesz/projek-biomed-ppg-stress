#include <Arduino.h>
#include <Wire.h>
#include <MAX30105.h>
#include <math.h>
#include <string.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>

#ifndef M_PI
#define M_PI 3.14f
#endif

// ========== PIN SETUP ==========
#define TFT_CS    15
#define TFT_RST   4
#define TFT_DC    16
#define TFT_MOSI  17
#define TFT_SCLK  18

// ========== BUZZER PIN SETUP ==========
#define BUZZER_PIN 5

// ========== I2C PIN SETUP(MAX30102) ==========
const int SDA_PIN = 9;
const int SCL_PIN = 8;

// ========== TFT Display ==========
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

#include "../Model/rf_model.h"

// Instantiate exported model
RFModel model;

// ---------- CONFIG ----------
const uint16_t FS = 100;
const float DURATION_SEC = 8.0f;
const uint16_t WINDOW_NEEDED_DEFAULT = (uint16_t)round(FS * DURATION_SEC);
const uint16_t HOP_SAMPLES = FS;
const float BP_LOW = 0.5;
const float BP_HIGH = 5.0;
const float KURTOSIS_THRESH = 3.0;
const float MIN_BPM = 30.0;
const float MAX_BPM = 200.0;

const uint32_t FINGER_IR_THRESHOLD = 40000;
const double FINGER_AC_THRESHOLD = 800.0;
const uint16_t FINGER_DET_WINDOW = FS;

const int P_CAND = 3;
const double FUND_MARGIN_BPM = 10.0;
const double FUND_AMP_CUTOFF = 0.2;
const double HARM_TOL = 0.3;

bool plotterMode = false;

const int SSA_L = 40;
const int SSA_NCOMP = 3;

const uint16_t MAX_RING = 1024;
const uint16_t MAX_FFT_N = 1024;
const uint16_t MAX_PEAKS = 128;
const uint16_t MAX_SPEC_PEAKS = 64;
const uint16_t MAX_FUND_CAND = 16;

#ifndef N_FEATURES
    const int N_FEATURES = 3;
#endif

#ifndef N_CLASSES
    const int N_CLASSES = 3;
#endif

const char* STRESS_LABELS[N_CLASSES] = {"Stres Rendah", "Stres Sedang", "Stres Tinggi"};

// ========== LCD DISPLAY STATE ==========
bool displayInitialized = false;
unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_UPDATE_INTERVAL = 4000;

double lastBPM = -1;
double lastRMSSD = -1;
double lastSDNN = -1;
int lastStressClass = -1;
double lastCompTime = -1;

// ---------- GLOBAL BUFFERS ----------
static float ringBuffer[MAX_RING];
volatile uint32_t writeIndex = 0;
volatile uint32_t samplesCollected = 0;

static float g_ppg[MAX_FFT_N];
static float g_ppg_bp[MAX_FFT_N];
static float g_ssa_recon[MAX_FFT_N];
static float g_recon_total[MAX_FFT_N];
static int g_peak_idx[MAX_PEAKS];

static float g_rr_intervals[MAX_PEAKS];
static int g_rr_count = 0;

struct SpecPeak { int bin; float amp; float hz; float bpm; };

MAX30105 particleSensor;
uint16_t WINDOW_NEEDED = WINDOW_NEEDED_DEFAULT;
uint16_t FFT_N = 1;

// ---------- Biquad ----------
class Biquad {
public:
    float a0,a1,a2,b1,b2;
    float x1, x2, y1, y2;
    Biquad(){ a0=1; a1=a2=b1=b2=0; x1=x2=y1=y2=0; }
    void reset() { x1=x2=y1=y2=0; }
    void setBandpass(float fs, float f0, float Q) {
        float w0 = 2.0f * M_PI * f0 / fs;
        float alpha = sinf(w0) / (2.0f * Q);
        float cosw0 = cosf(w0);
        float b0 = alpha;
        float b1_ = 0.0f;
        float b2_ = -alpha;
        float a0_ = 1.0f + alpha;
        float a1_ = -2.0f * cosw0;
        float a2_ = 1.0f - alpha;
        a0 = b0 / a0_; a1 = b1_ / a0_; a2 = b2_ / a0_;
        b1 = a1_ / a0_; b2 = a2_ / a0_;
        x1 = x2 = y1 = y2 = 0.0;
    }
    float process(float x) {
        float y = a0*x + a1*x1 + a2*x2 - b1*y1 - b2*y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }
};

// ========== BUZZER FUNCTIONS ==========
void initBuzzer() {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, HIGH);
    Serial.println("✓ Buzzer initialized on pin " + String(BUZZER_PIN));
}

// ---------- Utilities ----------
float compute_mean(const float *x, int N) {
    if (N<=0) return 0.0;
    float s=0;
    for (int i=0;i<N;i++) s+=x[i];
    return s/N;
}

float compute_std(const float *x, int N, float mean) {
    if (N<=1) return 0.0;
    float s=0;
    for (int i=0;i<N;i++){ float d=x[i]-mean; s+=d*d; }
    return sqrt(s/(N-1));
}

float compute_kurtosis(const float *x, int N, float mean, float stdv) {
    if (N<4 || stdv <= 1e-12) return 0.0;
    float m4=0;
    for (int i=0;i<N;i++){ float d=x[i]-mean; m4 += d*d*d*d; }
    m4 /= N;
    return m4 / (stdv*stdv*stdv*stdv);
}

int find_peaks_simple(const float *x, int N, int min_dist_samples,float threshold, int *idx_out, int max_peaks=MAX_PEAKS) {
    int count = 0;
    for (int i=1;i<N-1;i++){
        if (x[i] > x[i-1] && x[i] > x[i+1] && x[i] > threshold) {
            if (count==0 || (i - idx_out[count-1]) >= min_dist_samples) {
                if (count < max_peaks) idx_out[count++] = i;
            }
        }
    }
    return count;
}

bool detectFinger(uint32_t writeIdx) {
    uint16_t w = (uint16_t)min((uint32_t)FINGER_DET_WINDOW, writeIdx);
    if (w == 0) return false;
    float s=0; float vmin = ringBuffer[(writeIdx-1) % MAX_RING];
    float vmax = vmin;
    for (uint16_t i=0;i<w;i++) {
        float val = ringBuffer[(writeIdx - 1 - i) % MAX_RING];
        s += val;
        if (val < vmin) vmin = val;
        if (val > vmax) vmax = val;
    }
    float mean = s / w;
    float ac = vmax - vmin;
    if (mean >= (double)FINGER_IR_THRESHOLD) return true;
    if (ac >= FINGER_AC_THRESHOLD) return true;
    return false;
}

uint16_t next_pow2(uint16_t x) {
    uint16_t p=1; while (p < x && p < MAX_FFT_N) p <<= 1; return p;
}

double calculate_rmssd(const float *rr_intervals, int count) {
    if (count < 2) return NAN;
    double sum_squared_diff = 0.0;
    int valid_diffs = 0;
    for (int i = 1; i < count; i++) {
        double diff = (rr_intervals[i] - rr_intervals[i-1]) * 1000.0;
        sum_squared_diff += diff * diff;
        valid_diffs++;
    }
    if (valid_diffs == 0) return NAN;
    return sqrt(sum_squared_diff / valid_diffs);
}

double calculate_sdnn(const float *rr_intervals, int count) {
    if (count < 2) return NAN;
    float rr_ms[MAX_PEAKS];
    for (int i = 0; i < count; i++) {
        rr_ms[i] = rr_intervals[i] * 1000.0;
    }
    float mean = compute_mean(rr_ms, count);
    return compute_std(rr_ms, count, mean);
}

void extract_rr_intervals(const int *peak_indices, int n_peaks, int fs, float *rr_out, int *count_out) {
    *count_out = 0;
    if (n_peaks < 2) return;
    for (int i = 1; i < n_peaks; i++) {
        float rr_sec = (peak_indices[i] - peak_indices[i-1]) / (float)fs;
        if (rr_sec >= 0.3 && rr_sec <= 2.0) {
            rr_out[*count_out] = rr_sec;
            (*count_out)++;
        }
    }
}

bool ssa_reconstruct_hankel_svd(const float* x, int N, int L, int ncomp, float* y_out) {
    if (!x || !y_out || N <= 0) return false;
    memcpy(y_out, x, sizeof(float) * (size_t)N);
    return true;
}

// ========== TFT Display ==========
void initDisplayLayout() {
    if (displayInitialized) return;

    tft.fillScreen(ILI9341_BLACK);
    tft.fillRoundRect(5, 5, 310, 35, 6, 0x0208);
    tft.setTextSize(2);
    tft.setTextColor(ILI9341_CYAN);
    tft.setCursor(35, 14);
    tft.print("STRESS MONITOR");
    
    tft.drawFastVLine(160, 50, 190, 0x4208);
    
    tft.fillRoundRect(10, 55, 140, 55, 5, 0x2104);
    tft.setTextSize(1);
    tft.setTextColor(ILI9341_LIGHTGREY);
    tft.setCursor(15, 60);
    tft.print("HEART RATE");
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(110, 95);
    tft.print("BPM");

    tft.fillRoundRect(10, 118, 140, 55, 5, 0x2104);
    tft.setTextColor(ILI9341_LIGHTGREY);
    tft.setCursor(15, 123);
    tft.print("RMSSD");
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(115, 158);
    tft.print("ms");
    
    tft.fillRoundRect(10, 181, 140, 55, 5, 0x2104);
    tft.setTextColor(ILI9341_LIGHTGREY);
    tft.setCursor(15, 186);
    tft.print("SDNN");
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(115, 221);
    tft.print("ms");
    
    tft.fillRoundRect(170, 55, 140, 181, 5, 0x2104);
    tft.setTextSize(1);
    tft.setTextColor(ILI9341_LIGHTGREY);
    tft.setCursor(175, 60);
    tft.print("TINGKAT STRESS");

    tft.drawFastHLine(175, 195, 130, 0x4208);
    tft.setCursor(175, 205);
    tft.print("COMPUTATION TIME");
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(285, 221);
    tft.setTextSize(1);
    tft.print("ms");
    
    displayInitialized = true;
}

void clearValueArea(int16_t x, int16_t y, int16_t w, int16_t h) {
    tft.fillRect(x, y, w, h, 0x2104);
}

void updateBPM(double bpm) {
    if (fabs(bpm - lastBPM) < 0.5 && lastBPM > 0) return;
    
    clearValueArea(15, 75, 130, 25);
    tft.setTextSize(3);
    tft.setCursor(20, 78);
    
    if (isfinite(bpm) && bpm > 0) {
        tft.setTextColor(ILI9341_GREEN);
        tft.printf("%.0f", bpm);
    } else {
        tft.setTextColor(ILI9341_RED);
        tft.print("---");
    }

    lastBPM = bpm;
}

void updateRMSSD(double rmssd) {
    if (fabs(rmssd - lastRMSSD) < 0.5 && lastRMSSD > 0) return;

    clearValueArea(15, 138, 130, 25);
    tft.setTextSize(2);
    tft.setCursor(20, 141);
    
    if (isfinite(rmssd)) {
        tft.setTextColor(ILI9341_YELLOW);
        tft.printf("%.1f", rmssd);
    } else {
        tft.setTextColor(ILI9341_RED);
        tft.print("---");
    }

    lastRMSSD = rmssd;
}
void updateSDNN(double sdnn) {
    if (fabs(sdnn - lastSDNN) < 0.5 && lastSDNN > 0) return;
    
    clearValueArea(15, 201, 130, 25);
    tft.setTextSize(2);
    tft.setCursor(20, 204);
    
    if (isfinite(sdnn)) {
        tft.setTextColor(ILI9341_YELLOW);
        tft.printf("%.1f", sdnn);
    } else {
        tft.setTextColor(ILI9341_RED);
        tft.print("---");
    }

    lastSDNN = sdnn;
}

void updateStressClassification(int stress_class) {
    if (stress_class == lastStressClass) return;

    clearValueArea(175, 80, 130, 140);
    
    uint16_t color;
    const char* label;
    
    if (stress_class == 0) {
        color = ILI9341_GREEN;
        label = "RENDAH";
    } else if (stress_class == 1) {
        color = ILI9341_YELLOW;
        label = "SEDANG";
    } else if (stress_class == 2) {
        color = ILI9341_RED;
        label = "TINGGI";
    } else {
        color = ILI9341_RED;
        label = "ERROR";
    }

    tft.setTextSize(3);
    tft.setTextColor(color);
    tft.setCursor(180, 130);
    tft.print(label);

    lastStressClass = stress_class;
}

void updateComputationTime(double compTime) {
    if (fabs(compTime - lastCompTime) < 0.5 && lastCompTime > 0) return;

    clearValueArea(175, 220, 105, 20);
    tft.setTextSize(2);
    tft.setCursor(180, 221);
    
    if (isfinite(compTime)) {
        tft.setTextColor(ILI9341_CYAN);
        tft.printf("%.0f", compTime);
    } else {
        tft.setTextColor(ILI9341_RED);
        tft.print("---");
    }

    lastCompTime = compTime;
}

void displayOnTFT(double bpm, double rmssd, double sdnn, int stress_class, double computationTimeMs) {
    if (!displayInitialized) {
        initDisplayLayout();
    }
    
    updateBPM(bpm);
    updateRMSSD(rmssd);
    updateSDNN(sdnn);
    updateStressClassification(stress_class);
    updateComputationTime(computationTimeMs);
}

// ========== Stress Classification ==========
void classify_stress(double rmssd, double sdnn, double bpm, double computationTimeMs) { 
    
    int stress_class = -1;

    if (!isfinite(rmssd) || !isfinite(sdnn) || !isfinite(bpm)) {
        Serial.println("\n Cannot classify: Invalid RMSSD, SDNN, or BPM values");
        digitalWrite(BUZZER_PIN, HIGH);
        
        unsigned long currentTime = millis();
        if (currentTime - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL || !displayInitialized) {
            displayOnTFT(bpm, rmssd, sdnn, stress_class, computationTimeMs);
            lastDisplayUpdate = currentTime;
        }
        return;
    }

    float features[N_FEATURES];
    features[0] = (float)rmssd;
    features[1] = (float)sdnn;
    features[2] = (float)bpm;
    
    // Use the exported Random Forest model instance
    stress_class = model.predict(features);
    
    Serial.println("\n╔════════════════════════════════════════════╗");
    Serial.println("║ STRESS LEVEL CLASSIFICATION ║");
    Serial.println("╚════════════════════════════════════════════╝");
    Serial.printf("├─ RMSSD: %.2f ms\n", rmssd);
    Serial.printf("├─ SDNN:  %.2f ms\n", sdnn);
    Serial.printf("├─ BPM:   %.1f\n", bpm);
    Serial.printf("├─ Class: %d - %s\n", stress_class, STRESS_LABELS[stress_class]);
    Serial.printf("└─ Time:  %.1f ms\n", computationTimeMs);
    
    if (stress_class == 2) {
        Serial.println("HIGH STRESS DETECTED! Buzzer ON");
        digitalWrite(BUZZER_PIN, LOW);
    } else {
        digitalWrite(BUZZER_PIN, HIGH);
    }

    unsigned long currentTime = millis();
    if (currentTime - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL || !displayInitialized) {
        displayOnTFT(bpm, rmssd, sdnn, stress_class, computationTimeMs);
        lastDisplayUpdate = currentTime;
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n╔════════════════════════════════════════════╗");
    Serial.println("║ PPG + RF Stress + ILI9341 Display ║");
    Serial.println("╚════════════════════════════════════════════╝");

    initBuzzer();
    
    Serial.println("\nInitializing ILI9341 Display...");
    tft.begin();
    tft.setRotation(1);
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(60, 140);
    tft.println("Initializing...");
    Serial.println("✓ ILI9341 Display OK (320x240)");
    
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(400000);
    delay(50);
    
    bool ok = false;
    for (int attempt = 0; attempt < 5 && !ok; ++attempt) {
        Serial.printf("Initializing MAX3010x (try %d)...\n", attempt + 1);
        if (particleSensor.begin(Wire)) { ok = true; break; }
        delay(150);
    }

    if (!ok) {
        Serial.println("ERROR: MAX3010x not found!");
        tft.fillScreen(ILI9341_RED);
        tft.setTextColor(ILI9341_WHITE);
        tft.setTextSize(2);
        tft.setCursor(60, 140);
        tft.println("Sensor Error!");
        while (1) delay(1000);
    }

    Serial.println("✓ MAX3010x initialized");
    particleSensor.setup(0x1F, 4, 2, 100, 411, 4096);
    
    WINDOW_NEEDED = max((uint16_t)1, (uint16_t)round(FS * DURATION_SEC));
    FFT_N = next_pow2(WINDOW_NEEDED);
    if (FFT_N > MAX_FFT_N) FFT_N = MAX_FFT_N;

    Serial.printf("\n✓ System Ready (FS=%d, window=%d, FFT=%d)\n", FS, WINDOW_NEEDED, FFT_N);
    Serial.println("Buzzer: Solid ON on HIGH STRESS only.");

    tft.fillScreen(ILI9341_BLACK);
    tft.fillRoundRect(50, 90, 220, 60, 8, ILI9341_DARKGREEN);
    tft.setCursor(95, 105);
    tft.setTextColor(ILI9341_GREEN);
    tft.setTextSize(2);
    tft.println("READY!");
    tft.setCursor(50, 125);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(1);
    tft.println("Place finger on sensor...");
    
    delay(2000);
}

unsigned long lastSampleMicros = 0;
const unsigned long samplePeriodUs = 1000000UL / FS;

void loop() {
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'P' || c == 'p') {
            plotterMode = !plotterMode;
            Serial.printf("Plotter %s\n", plotterMode ? "ON" : "OFF");
        }
    }

    unsigned long now = micros();
    if (now - lastSampleMicros >= samplePeriodUs) {
        lastSampleMicros += samplePeriodUs;
        uint32_t ir = particleSensor.getIR();
        double val = (double)ir;
        ringBuffer[writeIndex % MAX_RING] = val;
        writeIndex++;
        if (samplesCollected < WINDOW_NEEDED) samplesCollected++;
    }

    if (samplesCollected >= WINDOW_NEEDED) {
        unsigned long startTime = micros();

        int N = WINDOW_NEEDED;
        uint32_t startIndex = (writeIndex >= (uint32_t)N) ? (writeIndex - N) : 0;
        for (int i = 0; i < N; i++) g_ppg[i] = ringBuffer[(startIndex + i) % MAX_RING];
        
        bool fingerPresent = detectFinger(writeIndex);
        if (!fingerPresent) {
            if (!plotterMode) Serial.printf("[%lu] No Finger\n", millis());
            digitalWrite(BUZZER_PIN, HIGH);

            if (lastStressClass != -1 || lastBPM != -1) {
                displayOnTFT(NAN, NAN, NAN, -1, NAN);
                lastBPM = -1;
                lastRMSSD = -1;
                lastSDNN = -1;
                lastStressClass = -1;
                lastCompTime = -1;
            }

            if (samplesCollected > HOP_SAMPLES) samplesCollected -= HOP_SAMPLES;
            else samplesCollected = 0;
            return;
        }

        double mean = compute_mean(g_ppg, N);
        for (int i = 0; i < N; i++) g_ppg[i] -= mean;
        double stdv = compute_std(g_ppg, N, 0.0);
        if (stdv < 1e-6) stdv = 1.0;
        for (int i = 0; i < N; i++) g_ppg[i] /= stdv;
        
        Biquad bp1, bp2;
        double center = sqrt(BP_LOW * BP_HIGH);
        double Qbp = center / (BP_HIGH - BP_LOW);
        if (Qbp < 0.7) Qbp = 0.7;
        bp1.setBandpass(FS, center, Qbp);
        bp2.setBandpass(FS, center, Qbp);
        for (int i = 0; i < N; i++) {
            double y1 = bp1.process(g_ppg[i]);
            g_ppg_bp[i] = bp2.process(y1);
        }

        if (N > SSA_L + 1) {
            ssa_reconstruct_hankel_svd(g_ppg_bp, N, SSA_L, SSA_NCOMP,
            g_ssa_recon);
        } else {
            for (int i = 0; i < N; i++) g_ssa_recon[i] = g_ppg_bp[i];
        }

        for (int i = 0; i < N; i++) g_recon_total[i] = g_ssa_recon[i];
        
        double vmax = g_recon_total[0], vmin = g_recon_total[0];
        for (int i = 1; i < N; i++) {
            if (g_recon_total[i] > vmax) vmax = g_recon_total[i];
            if (g_recon_total[i] < vmin) vmin = g_recon_total[i];
        }

        int min_dist_samples = max(1, (int)round(0.48 * FS));
        double threshold = vmin + (vmax - vmin) * 0.15;
        int n_peaks = find_peaks_simple(g_recon_total, N,
        min_dist_samples, threshold, g_peak_idx, MAX_PEAKS);
        
        g_rr_count = 0;
        double rmssd = NAN, sdnn = NAN, bpm_mean = NAN;
        
        if (n_peaks >= 2) {
            extract_rr_intervals(g_peak_idx, n_peaks, FS, g_rr_intervals, &g_rr_count);
            if (g_rr_count >= 2) {
                rmssd = calculate_rmssd(g_rr_intervals, g_rr_count);
                sdnn = calculate_sdnn(g_rr_intervals, g_rr_count);
                double mean_rr = compute_mean(g_rr_intervals, g_rr_count);
                bpm_mean = 60.0 / mean_rr;
            }
        }

        unsigned long endTime = micros();
        double computationTimeMs = (double)(endTime - startTime) / 1000.0;
        
        if (!plotterMode) {
            Serial.printf("\n[%lu] BPM: %.1f | Peaks: %d\n", millis(), isfinite(bpm_mean) ? bpm_mean : 0.0, n_peaks);
            classify_stress(rmssd, sdnn, bpm_mean, computationTimeMs);
        }
        
        if (samplesCollected > HOP_SAMPLES) samplesCollected -= HOP_SAMPLES;
        else samplesCollected = 0;
    }
}

