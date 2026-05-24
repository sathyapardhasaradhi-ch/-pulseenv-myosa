# PULSEENV

**Real-time Human-Environment Stress Co-Monitoring**

![Team](https://img.shields.io/badge/Team-Noventra-blue) ![College](https://img.shields.io/badge/CVR%20College%20of%20Engineering-Hyderabad-green) ![Event](https://img.shields.io/badge/IEEE%20MYOSA-5.0-red)

---

## The Problem

Occupational health monitoring has a fundamental gap. Environment sensors watch temperature, pressure and air quality. Health monitors track heart rate and movement. Neither system asks what happens the moment a deteriorating environment begins affecting the human body inside it.

That gap has consequences. A factory worker in a hot enclosure. A student in a poorly ventilated classroom. An elderly person in a sealed room. In every case the environment degrades first — the body follows. By the time symptoms are obvious, the harm is already done.

**PULSEENV closes that gap.** It monitors both simultaneously and tells you exactly when one starts causing the other.

---

## What It Does

PULSEENV uses every component in the MYOSA Mini Kit to co-monitor six parameters across two domains, fusing them into a single **Stress Correlation Index (SCI)** computed every second.

```
ENVIRONMENT                         HUMAN BODY
───────────                         ──────────
Temperature        (BMP180)         Pulse Rate      (APDS9960)
Air Pressure       (BMP180)         Micro-Tremors   (MPU6050)
Ambient Light      (APDS9960)       Postural Shift  (MPU6050)
                        │                 │
                        └────────┬────────┘
                                 │
                    Stress Correlation Index
                          SCI  (0 – 100)
                                 │
                    ┌────────────┼────────────┐
                    │            │            │
               OLED Display  WiFi Dashboard  Buzzer Alert
```

---

## Technical Innovation

None of the sensors in the MYOSA kit are designed for physiological monitoring. Using them for exactly that is the core engineering decision in PULSEENV.

| Sensor | Datasheet Purpose | How PULSEENV Uses It |
|--------|-------------------|----------------------|
| APDS9960 | Gesture and color detection | PPG pulse monitor — IR LED shines through fingertip, blood flow variation maps to heartbeat |
| BMP180 | Barometric altimeter | Thermal stress detector + enclosure pressure monitor |
| MPU6050 | Orientation and motion tracking | Micro-tremor index — sub-1g acceleration deviation reveals physical stress response |

---

## SCI Formula

```
SCI = 0.40 × pulse_deviation
    + 0.25 × temp_deviation × 5
    + 0.15 × pressure_deviation × 0.1
    + 0.15 × tremor_deviation
    + 0.05 × light_deviation
```

Pulse carries the highest weight as the most direct physiological signal available. Temperature is weighted heavily because thermal stress elevates heart rate before the person registers discomfort. Tremor and pressure are slower-moving signals. All deviations are measured against a **personal 60-second baseline** calibrated on startup — not a population average.

---

## Alert Levels

| Level | SCI | Buzzer | Meaning |
|-------|-----|--------|---------|
| SAFE | 0 – 30 | Silent | All parameters within personal baseline |
| WARNING | 30 – 60 | Slow beep every 3s | Deviations detected — investigate |
| CRITICAL | 60 – 100 | Rapid beep every 400ms | Multiple stressors compounding — act now |

---

## Live Dashboard

The ESP32 creates a WiFi hotspot. Any phone or laptop connects and opens **192.168.4.1** to see:

- Animated SCI ring with real-time color shift (green → amber → red)
- Live pulse waveform on HTML5 Canvas
- 60-point SCI history trend chart
- Individual sensor cards with threshold-based color alerts
- Timestamped event log for every status change
- 3D animated avatar in Three.js that walks faster as stress climbs
- Downloadable CSV data log at `/log`

---

## Real-World Applications

**Factory floors** — detect when rising temperature and pressure begin affecting worker physiology before an incident occurs. A single device provides continuous monitoring at a fraction of the cost of industrial systems.

**Classrooms** — correlate temperature and light conditions with measurable student physiological responses during long sessions. Data justifies facility improvements.

**Home health** — continuous passive monitoring for elderly individuals in enclosed spaces. Alerts trigger when environmental conditions exceed safe thresholds for that specific person.

---

## Hardware Setup

```
MYOSA Motherboard
      │
      ├── JST cable ──── APDS9960   (hand icon — pulse + light)
      ├── JST cable ──── BMP180     (temperature icon — pressure + temp)
      ├── JST cable ──── MPU6050    (lightning icon — tremor + motion)
      ├── JST cable ──── OLED       (display module)
      └── 3 wires  ──── Buzzer
                         SIG → D12
                         5V  → VIN
                         GND → GND

Power: USB Type-C to any 5V source
```

---

## Quick Start

**1. Flash firmware**
```
Open pulseenv.ino in Arduino IDE
Board: ESP32 Dev Module
Port: your COM port
Upload speed: 115200
Click Upload
```

**2. Calibrate**
```
Power on
OLED shows sensor check then CALIBRATING
Keep still for 60 seconds
Two beeps = calibration complete, monitoring begins
```

**3. Connect and monitor**
```
WiFi network : PULSEENV
Password     : pulseenv123
Dashboard    : http://192.168.4.1
Data feed    : http://192.168.4.1/data
Data log CSV : http://192.168.4.1/log
```

---

## Required Libraries

Install via Arduino IDE Tools > Manage Libraries:

```
Adafruit BMP085 Unified
SparkFun APDS9960
MPU6050 by Electronic Cats
Adafruit SSD1306
Adafruit GFX Library
```

ESP32 board package URL for Boards Manager:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

---

## Live Demo Sequence

**Baseline phase (60 seconds)**
Volunteer sits still. System calibrates. Dashboard shows green. All readings stable.

**Environmental stress phase (90 seconds)**
Heat source applied near BMP180. Temperature rises 2-3 degrees. SCI climbs. WARNING triggers. Buzzer beeps. OLED turns amber.

**Physiological correlation phase (90 seconds)**
Finger placed on APDS9960. BPM appears. As heat continues: pulse rises. MPU6050 detects tremors. Dashboard shows pulse and tremor rising together — the correlation is visible in real time.

**Recovery phase (30 seconds)**
Heat removed. SCI drops. System normalizes. Event log shows the full timeline.

---

## Customization

**Adjust alert thresholds**
```cpp
// In loop() around line 242
if (sci_score < 30) status_label = "SAFE";
else if (sci_score < 60) status_label = "WARNING";
else status_label = "CRITICAL";
```

**Change sensor weights**
```cpp
// In computeSCI()
float sci = (0.40 * pulse_dev)          +
            (0.25 * temp_dev * 5)       +
            (0.15 * pressure_dev * 0.1) +
            (0.15 * tremor_dev)         +
            (0.05 * light_dev);
```

---

## Repository Contents

```
pulseenv-myosa/
├── README.md                   This file
├── pulseenv.md                 Blog submission (MYOSA required format)
├── pulseenv.ino                ESP32 firmware
├── pulseenv-dashboard.html     Standalone advanced dashboard
├── cover-image.jpg             Full kit in original packaging
├── circuit.jpg                 All sensors wired and running
├── oled.jpg                    Live OLED readout
├── dashboard.jpg               Dashboard on device
├── working.jpg                 System in active use
└── pulseenv-demo.mp4           Full working demonstration
```

---

## Notes from the Build

The APDS9960 light sensor returns zero unless a 500ms warmup delay follows `enableLightSensor()` — not documented in the library. The MPU6050 tremor baseline was near zero when calibrated on a flat surface, which caused any normal handling to spike the contribution to maximum post-calibration — fixed by clamping raw tremor deviation to 0.5g before the SCI formula. The buzzer on 5V via VIN is significantly louder than 3.3V. The ESP32 soft AP disconnects connected devices from the internet — expected and intentional.

---

## Team

**Noventra** — CVR College of Engineering, Hyderabad, Telangana

IEEE MYOSA 5.0 — APSCON 2026

MIT License
