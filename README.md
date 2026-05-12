# projek-biomed-ppg-stress

Biomedical stress monitoring project based on PPG / HRV signals, a trained Random Forest classifier, and Arduino-compatible firmware. The repository combines data preparation, model training, model export, reusable Arduino library code, and embedded application sketches for heart-rate and stress monitoring.

## What this project does

The system reads PPG data from a MAX30102 sensor, computes heart-rate related features, and predicts stress level with a compact machine-learning model. The trained predictor uses three HRV features as input:

1. RMSSD (ms)
2. SDNN (ms)
3. BPM

The stress output is mapped to three classes:

- 0 = Stress Low / Stres Rendah
- 1 = Stress Medium / Stres Sedang
- 2 = Stress High / Stres Tinggi

## Repository layout

- `1_Train-model/` - Jupyter notebook used to train the Random Forest model and export Arduino-compatible code.
- `Dataset/` - Training data used by the notebook.
- `Model/` - Generated model header used by the embedded firmware.
- `2_Main-program/` - Main embedded application sketch for the full stress monitoring system.
- `aris/ppg_hr_spo2/` - Example sketch for heart rate and SpO2 monitoring.
- `aris/StressPredictor/` - Arduino library package that wraps the ML model for reuse.
- `Test codes/` - Experiment and validation sketches used during development.

## Model and training

The training notebook is [1_Train-model/train_random_forest.ipynb](1_Train-model/train_random_forest.ipynb). It trains a `RandomForestClassifier` on the dataset in [Dataset/DATA TRAINING.csv](Dataset/DATA%20TRAINING.csv) using these features:

- Start Time (s)
- End Time (s)
- RMSSD (ms)
- SDNN (ms)
- BPM
- Num R-peaks

The notebook also exports the model into Arduino-compatible C++ code. The saved notebook output shows a test-split accuracy of 100.00%.

## Arduino library

The reusable library lives in [aris/StressPredictor/StressPredictor](aris/StressPredictor/StressPredictor). Its public API is intentionally small:

- `StressPredictor.h` exposes `StressPredictor::predict(float rmssd, float sdnn, float bpm)`.
- `StressPredictor.cpp` forwards the three features into the exported model in the correct order.
- `examples/BasicPrediction/BasicPrediction.ino` shows how to call the predictor from a sketch.

To use the library in Arduino IDE, copy the `StressPredictor` folder into your Arduino libraries directory and open the example sketch from the IDE examples menu.

## Main firmware

The main application is [2_Main-program/2_Main-program.ino](2_Main-program/2_Main-program.ino). It integrates:

- MAX30105 / MAX30102 PPG sensing
- FFT and signal-processing utilities
- stress classification with the exported Random Forest model
- TFT display output
- buzzer feedback

The sketch also contains the initialization and display logic for the monitoring interface.

## Heart rate and SpO2 example

The sketch in [aris/ppg_hr_spo2/ppg_hr_spo2.ino](aris/ppg_hr_spo2/ppg_hr_spo2.ino) is a simpler sensor demo focused on BPM and SpO2. It is useful as a sensor bring-up or baseline validation sketch before running the full stress system.

## Typical workflow

1. Train or retrain the model in [1_Train-model/train_random_forest.ipynb](1_Train-model/train_random_forest.ipynb).
2. Export the model header and place it in [Model/rf_model.h](Model/rf_model.h) and the Arduino library wrapper if needed.
3. Build the Arduino library in [aris/StressPredictor/StressPredictor](aris/StressPredictor/StressPredictor).
4. Upload either the example sketch or [2_Main-program/2_Main-program.ino](2_Main-program/2_Main-program.ino) to the target board.
5. Validate the sensor readings and stress output on Serial Monitor or the TFT display.

## Hardware notes

The code base is centered around the following hardware components:

- MAX30102 / MAX30105 PPG sensor
- Arduino-compatible microcontroller
- TFT display for the main program
- buzzer for alert feedback

Some sketches also use OLED-based output for early sensor experiments.

## Notes

- The repository mixes training assets, library packaging, and firmware sketches, so file names are intentionally kept close to their experiment stages.
- The stress model expects feature order to stay consistent with the notebook and exported header.
- If you retrain the model, regenerate the exported header before rebuilding the firmware.

