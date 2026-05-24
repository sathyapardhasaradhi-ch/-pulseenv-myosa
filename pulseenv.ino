/*
 * ╔═══════════════════════════════════════════════════════════════════════════╗
 * ║                          PULSEENV v1.0                                    ║
 * ║   Simultaneous Human Physiological & Environmental Stress Co-Monitoring  ║
 * ║                                                                           ║
 * ║   Team: Noventra | CVR College of Engineering, Hyderabad                 ║
 * ║   Event: IEEE MYOSA 5.0 — APSCON 2026                                    ║
 * ║   Hardware: MYOSA Mini Kit (ESP32 + APDS9960 + BMP180 + MPU6050 + OLED) ║
 * ║                                                                           ║
 * ║   License: MIT — Free to use, modify, and distribute with attribution    ║
 * ╚═══════════════════════════════════════════════════════════════════════════╝
 */

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP085_U.h>
#include <SparkFun_APDS9960.h>
#include <MPU6050.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>

// ═══════════════════════════════════════════════════════════════════════════
// ║                         HARDWARE CONFIGURATION                          ║
// ═══════════════════════════════════════════════════════════════════════════

#define BUZZER_PIN 12                    // D12 — Buzzer SIG pin (active buzzer)
#define SCREEN_WIDTH 128                 // OLED pixel width
#define SCREEN_HEIGHT 64                 // OLED pixel height
#define OLED_RESET -1                    // OLED reset pin (not used)

// ═══════════════════════════════════════════════════════════════════════════
// ║                          WIFI HOTSPOT CONFIG                            ║
// ═══════════════════════════════════════════════════════════════════════════

const char* ssid     = "PULSEENV";
const char* password = "pulseenv123";

// ═══════════════════════════════════════════════════════════════════════════
// ║                           SENSOR OBJECTS                                ║
// ═══════════════════════════════════════════════════════════════════════════

Adafruit_BMP085_Unified bmp;             // Pressure + Temperature sensor
SparkFun_APDS9960       apds;            // Light + Gesture + PPG sensor
MPU6050                 mpu;             // Accelerometer + Gyroscope
Adafruit_SSD1306        display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WebServer               server(80);      // Web server for dashboard + API

// ═══════════════════════════════════════════════════════════════════════════
// ║                      CALIBRATION VARIABLES                              ║
// ═══════════════════════════════════════════════════════════════════════════

float baseline_pulse    = 72;            // Default resting heart rate
float baseline_temp     = 0;
float baseline_pressure = 0;
float baseline_tremor   = 0;
bool  calibrated        = false;
int   calibration_count = 0;
int   pulse_cal_count   = 0;

// ═══════════════════════════════════════════════════════════════════════════
// ║                       CURRENT SENSOR READINGS                           ║
// ═══════════════════════════════════════════════════════════════════════════

float current_pulse    = 0;              // Heart rate in BPM
float current_temp     = 0;              // Temperature in °C
float current_pressure = 0;              // Pressure in hPa
float current_tremor   = 0;              // Tremor magnitude (0-0.5)
float current_light    = 0;              // Ambient light intensity
float sci_score        = 0;              // Stress Correlation Index (0-100)

// ═══════════════════════════════════════════════════════════════════════════
// ║                         STATUS & LOGGING                                ║
// ═══════════════════════════════════════════════════════════════════════════

String dominant_stressor = "Calibrating";
String status_label      = "SAFE";       // SAFE | WARNING | CRITICAL
String log_data          = "time,pulse,temp,pressure,tremor,light,sci\n";

// ═══════════════════════════════════════════════════════════════════════════
// ║                           TIMING VARIABLES                              ║
// ═══════════════════════════════════════════════════════════════════════════

unsigned long last_sample = 0;
unsigned long last_buzz   = 0;
unsigned long start_time  = 0;

// ═══════════════════════════════════════════════════════════════════════════
// ║                         PPG BUFFER (APDS9960)                           ║
// ═══════════════════════════════════════════════════════════════════════════

int ppg_buffer[10];                      // 10-sample rolling buffer for pulse
int ppg_index = 0;

// ═══════════════════════════════════════════════════════════════════════════
// ║                         BUZZER CONTROL                                  ║
// ═══════════════════════════════════════════════════════════════════════════
/*
 * Buzzer is an active buzzer connected to D12 with 5V via VIN.
 * Produces sound when pin goes HIGH.
 *
 * Alert Patterns:
 *   SAFE     : Silent
 *   WARNING  : Slow beep every 3 seconds
 *   CRITICAL : Rapid beep every 400 milliseconds
 */

void buzzAlert(String level) {
  unsigned long now = millis();

  if (level == "SAFE") {
    // Silent — no beep
    digitalWrite(BUZZER_PIN, LOW);

  } else if (level == "WARNING") {
    // Slow beep every 3 seconds
    if (now - last_buzz >= 3000) {
      digitalWrite(BUZZER_PIN, HIGH);
      delay(300);
      digitalWrite(BUZZER_PIN, LOW);
      last_buzz = now;
    }

  } else if (level == "CRITICAL") {
    // Rapid beep every 400 milliseconds
    if (now - last_buzz >= 400) {
      digitalWrite(BUZZER_PIN, HIGH);
      delay(150);
      digitalWrite(BUZZER_PIN, LOW);
      last_buzz = now;
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// ║                    READ PULSE (PPG) FROM APDS9960                       ║
// ═══════════════════════════════════════════════════════════════════════════
/*
 * Repurposes APDS9960 as a photoplethysmography (PPG) monitor.
 * Finger placed on IR LED allows detection of blood flow variation.
 * Returns estimated BPM (50-120 range) or 0 if no finger detected.
 */

float readPPG() {
  uint16_t r, g, b, c;

  // Read all color channels (red carries heart rate info)
  apds.readAmbientLight(c);
  apds.readRedLight(r);
  apds.readGreenLight(g);
  apds.readBlueLight(b);

  // Store red value in rolling buffer
  ppg_buffer[ppg_index % 10] = r;
  ppg_index++;

  // Need at least 10 samples to calculate variation
  if (ppg_index < 10) return 0;

  // Find peak and trough in last 10 samples
  int max_val = 0, min_val = 9999;
  for (int i = 0; i < 10; i++) {
    if (ppg_buffer[i] > max_val) max_val = ppg_buffer[i];
    if (ppg_buffer[i] < min_val) min_val = ppg_buffer[i];
  }

  float variation = max_val - min_val;

  // No finger = no variation
  if (variation < 5) return 0;

  // Map variation to BPM range
  float bpm = 55 + (variation / 80.0) * 50;
  return constrain(bpm, 50, 120);
}

// ═══════════════════════════════════════════════════════════════════════════
// ║                  READ TREMOR (MICRO-VIBRATION) FROM MPU6050             ║
// ═══════════════════════════════════════════════════════════════════════════
/*
 * Detects sub-1g acceleration as indicator of physical stress (tremors, fidgeting).
 * Takes 5 rapid samples and averages to reduce noise.
 * Clamped to 0-0.5 range to prevent post-calibration saturation.
 */

float readTremor() {
  float total = 0;

  // Take 5 rapid samples and average
  for (int i = 0; i < 5; i++) {
    int16_t ax, ay, az, gx, gy, gz;
    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

    // Calculate acceleration magnitude relative to gravity (1g)
    float mag = sqrt(
      pow(ax / 16384.0, 2) +
      pow(ay / 16384.0, 2) +
      pow(az / 16384.0, 2)
    );

    // Deviation from 1g baseline
    total += abs(mag - 1.0);
    delay(2);
  }

  float tremor = total / 5.0;

  // Filter out noise below 0.05g
  if (tremor < 0.05) tremor = 0;

  // Clamp to 0-0.5g range (prevents saturation from table vibrations)
  return constrain(tremor, 0, 0.5);
}

// ═══════════════════════════════════════════════════════════════════════════
// ║                   READ AMBIENT LIGHT FROM APDS9960                      ║
// ═══════════════════════════════════════════════════════════════════════════
/*
 * Detects sudden brightness changes that cause visual stress.
 * Uses the APDS9960 ambient light sensor (separate from PPG channel).
 * Note: Requires 500ms warmup after enableLightSensor(true) in setup.
 */

float readLight() {
  uint16_t light = 0;

  if (apds.readAmbientLight(light)) {
    return (float)light;
  }

  return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// ║                    COMPUTE STRESS CORRELATION INDEX                     ║
// ═══════════════════════════════════════════════════════════════════════════
/*
 * SCI fuses 5 weighted parameters into a 0-100 stress score.
 * All deviations measured against personal 60-second baseline.
 * Dominant stressor is identified and displayed.
 *
 * Formula:
 *   SCI = 0.40 × pulse_dev
 *       + 0.25 × temp_dev × 5
 *       + 0.15 × pressure_dev × 0.1
 *       + 0.15 × tremor_dev
 *       + 0.05 × light_dev
 *
 * Weights reasoning:
 *   - Pulse: Highest weight (most direct physiological signal)
 *   - Temperature: High weight (thermal stress elevates HR before awareness)
 *   - Tremor: Medium weight (slower stress response)
 *   - Pressure: Medium-low weight (enclosure stress less common)
 *   - Light: Lowest weight (visual stress least critical)
 */

float computeSCI() {
  if (!calibrated) return 0;

  // Calculate percentage deviation from baseline for each parameter
  float pulse_dev = (baseline_pulse > 0 && current_pulse > 0)
                    ? abs(current_pulse - baseline_pulse) / baseline_pulse * 100
                    : 0;

  float temp_dev     = abs(current_temp     - baseline_temp);
  float pressure_dev = abs(current_pressure - baseline_pressure);

  // Tremor relative to minimum baseline of 0.05g (prevents division by zero)
  float effective_baseline_tremor = max(baseline_tremor, 0.05f);
  float tremor_dev = abs(current_tremor - effective_baseline_tremor)
                     / effective_baseline_tremor * 100;

  float light_dev = (current_light >= 0)
                    ? abs(current_light - 500.0f) / 500.0f * 100
                    : 0;

  // Cap individual contributions to prevent outliers
  pulse_dev    = constrain(pulse_dev,    0, 100);
  temp_dev     = constrain(temp_dev,     0, 20);
  pressure_dev = constrain(pressure_dev, 0, 50);
  tremor_dev   = constrain(tremor_dev,   0, 50);
  light_dev    = constrain(light_dev,    0, 100);

  // Weighted sum
  float sci = (0.40 * pulse_dev)          +
              (0.25 * temp_dev * 5)       +
              (0.15 * pressure_dev * 0.1) +
              (0.15 * tremor_dev)         +
              (0.05 * light_dev);

  // Determine dominant stressor
  float p  = pulse_dev    * 0.40;
  float t  = temp_dev * 5 * 0.25;
  float pr = pressure_dev * 0.1 * 0.15;
  float tr = tremor_dev   * 0.15;
  float l  = light_dev    * 0.05;

  float mx = max({p, t, pr, tr, l});

  if      (mx == p)  dominant_stressor = "Pulse";
  else if (mx == t)  dominant_stressor = "Heat";
  else if (mx == pr) dominant_stressor = "Pressure";
  else if (mx == tr) dominant_stressor = "Motion";
  else               dominant_stressor = "Light";

  return constrain(sci, 0, 100);
}

// ═══════════════════════════════════════════════════════════════════════════
// ║                         UPDATE STATUS LABEL                             ║
// ═══════════════════════════════════════════════════════════════════════════

void updateStatus() {
  if      (sci_score < 30) status_label = "SAFE";
  else if (sci_score < 60) status_label = "WARNING";
  else                     status_label = "CRITICAL";
}

// ═══════════════════════════════════════════════════════════════════════════
// ║                    UPDATE OLED DISPLAY (128x64)                         ║
// ═══════════════════════════════════════════════════════════════════════════
/*
 * 5-line OLED layout:
 *   Row 1: SCI + Status
 *   Row 2: BPM (or "no finger" prompt)
 *   Row 3: Temperature + Pressure
 *   Row 4: Tremor + Light intensity
 *   Row 5: Dominant stressor indicator
 */

void updateOLED() {
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);

  // Row 1 — SCI + Status (SAFE/WARNING/CRITICAL)
  display.setCursor(0, 0);
  display.print("SCI:");
  display.print(sci_score, 1);
  display.print("  ");
  display.print(status_label);

  // Row 2 — BPM (with "no finger" indicator)
  display.setCursor(0, 13);
  if (current_pulse == 0)
    display.print("BPM:-- (no finger)");
  else {
    display.print("BPM:");
    display.print(current_pulse, 0);
    display.print(" bpm");
  }

  // Row 3 — Temperature + Pressure
  display.setCursor(0, 26);
  display.print("T:");
  display.print(current_temp, 1);
  display.print("C P:");
  display.print(current_pressure, 0);

  // Row 4 — Tremor + Light
  display.setCursor(0, 39);
  display.print("TR:");
  display.print(current_tremor * 100, 1);
  display.print(" L:");
  display.print(current_light, 0);

  // Row 5 — Dominant stressor
  display.setCursor(0, 52);
  display.print(">>");
  display.print(dominant_stressor);

  display.display();
}

// ═══════════════════════════════════════════════════════════════════════════
// ║                      WEB DASHBOARD HTML (RESPONSIVE)                    ║
// ═══════════════════════════════════════════════════════════════════════════

String getDashboardHTML() {
  String sci_color;
  if      (sci_score < 30) sci_color = "#00ff88";
  else if (sci_score < 60) sci_color = "#ffaa00";
  else                     sci_color = "#ff4444";

  String html = "<!DOCTYPE html><html><head>";
  html += "<title>PULSEENV</title>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='2'>";
  html += "<style>";
  html += "*{box-sizing:border-box;margin:0;padding:0}";
  html += "body{font-family:Arial;background:#0a0a0a;color:#fff;padding:16px}";
  html += "h1{color:#00ff88;text-align:center;font-size:20px;margin-bottom:2px}";
  html += ".sub{text-align:center;color:#444;font-size:11px;margin-bottom:14px}";
  html += ".sci-box{background:#111827;border-radius:14px;padding:18px;";
  html += "text-align:center;margin-bottom:14px;border:1px solid #1f2937}";
  html += ".sci-label{color:#6b7280;font-size:11px;letter-spacing:2px}";
  html += ".sci-val{font-size:68px;font-weight:bold;color:" + sci_color + "}";
  html += ".sci-status{font-size:20px;font-weight:bold;color:" + sci_color + ";margin:6px 0 4px}";
  html += ".sci-stress{color:#9ca3af;font-size:12px;margin-bottom:10px}";
  html += ".bar-bg{background:#1f2937;border-radius:8px;height:8px}";
  html += ".bar-fill{height:8px;border-radius:8px;background:" + sci_color + ";";
  html += "width:" + String((int)constrain(sci_score, 0, 100)) + "%}";
  html += ".grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-bottom:14px}";
  html += ".card{background:#111827;border-radius:10px;padding:10px;";
  html += "text-align:center;border:1px solid #1f2937}";
  html += ".lbl{color:#6b7280;font-size:10px;margin-bottom:4px}";
  html += ".val{font-size:22px;font-weight:bold;color:#00ff88}";
  html += ".warn{color:#ffaa00}.crit{color:#ff4444}.dim{color:#6b7280;font-size:14px}";
  html += ".footer{text-align:center;color:#374151;font-size:10px;margin-top:10px}";
  html += "</style></head><body>";

  html += "<h1>PULSEENV</h1>";
  html += "<p class='sub'>Team Noventra | CVR College of Engineering</p>";

  // SCI Box with progress bar
  html += "<div class='sci-box'>";
  html += "<div class='sci-label'>STRESS CORRELATION INDEX</div>";
  html += "<div class='sci-val'>" + String(sci_score, 1) + "</div>";
  html += "<div class='sci-status'>" + status_label + "</div>";
  html += "<div class='sci-stress'>Dominant: " + dominant_stressor + "</div>";
  html += "<div class='bar-bg'><div class='bar-fill'></div></div>";
  html += "</div>";

  // Sensor Cards (3x2 grid)
  html += "<div class='grid'>";

  // Pulse
  html += "<div class='card'><div class='lbl'>PULSE</div>";
  if (current_pulse == 0)
    html += "<div class='val dim'>No Finger</div>";
  else
    html += "<div class='val'>" + String((int)current_pulse) +
            "<span style='font-size:11px'> BPM</span></div>";
  html += "</div>";

  // Temperature
  html += "<div class='card'><div class='lbl'>TEMP</div><div class='val'>" +
          String(current_temp, 1) + "<span style='font-size:11px'>C</span></div></div>";

  // Pressure
  html += "<div class='card'><div class='lbl'>PRESSURE</div><div class='val'>" +
          String(current_pressure, 0) + "<span style='font-size:11px'> hPa</span></div></div>";

  // Tremor
  float tr_disp = current_tremor * 100;
  String tr_class = (tr_disp > 30) ? ((tr_disp > 50) ? "val crit" : "val warn") : "val";
  html += "<div class='card'><div class='lbl'>TREMOR</div><div class='" +
          tr_class + "'>" + String(tr_disp, 1) + "</div></div>";

  // Light
  html += "<div class='card'><div class='lbl'>LIGHT</div><div class='val'>" +
          String(current_light, 0) + "</div></div>";

  // Uptime
  html += "<div class='card'><div class='lbl'>UPTIME</div><div class='val'>" +
          String((millis() - start_time) / 1000) +
          "<span style='font-size:11px'>s</span></div></div>";

  html += "</div>";
  html += "<p class='footer'>Auto-refresh 2s | http://192.168.4.1</p>";
  html += "</body></html>";
  return html;
}

// ═══════════════════════════════════════════════════════════════════════════
// ║                            SETUP ROUTINE                                ║
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  Wire.begin();
  start_time = millis();

  // Configure buzzer pin
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // ─── Initialize OLED ───
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED FAILED");
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("PULSEENV v1.0");
  display.println("Team Noventra");
  display.println("CVR College");
  display.println("Starting...");
  display.display();
  delay(1500);

  // ─── Sensor Check Screen ───
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Sensor Check:");
  display.display();

  // BMP180 — Pressure + Temperature
  if (!bmp.begin()) {
    display.println("BMP180:   FAIL");
    Serial.println("BMP180 FAILED");
  } else {
    display.println("BMP180:   OK");
    Serial.println("BMP180 OK");
  }
  display.display();
  delay(400);

  // APDS9960 — Light + Gesture (repurposed for PPG)
  if (!apds.init()) {
    display.println("APDS9960: FAIL");
    Serial.println("APDS9960 FAILED");
  } else {
    apds.enableLightSensor(true);
    delay(500);  // Critical: wait for sensor warmup
    display.println("APDS9960: OK");
    Serial.println("APDS9960 OK");
  }
  display.display();
  delay(400);

  // MPU6050 — Accelerometer + Gyroscope (for tremor detection)
  mpu.initialize();
  if (!mpu.testConnection()) {
    display.println("MPU6050:  FAIL");
    Serial.println("MPU6050 FAILED");
  } else {
    display.println("MPU6050:  OK");
    Serial.println("MPU6050 OK");
  }
  display.display();
  delay(400);

  // ─── Initialize SPIFFS (for data logging) ───
  if (!SPIFFS.begin(true))
    Serial.println("SPIFFS failed");

  // ─── Start WiFi Hotspot ───
  WiFi.softAP(ssid, password);
  Serial.print("Hotspot IP: ");
  Serial.println(WiFi.softAPIP());
  display.println("WiFi:     OK");
  display.println("192.168.4.1");
  display.display();
  delay(1500);

  // ─── Register Web Routes ───

  // Route: GET / → Dashboard HTML
  server.on("/", []() {
    server.send(200, "text/html", getDashboardHTML());
  });

  // Route: GET /data → Live JSON data feed
  server.on("/data", []() {
    String j = "{";
    j += "\"sci\":"        + String(sci_score, 1)           + ",";
    j += "\"pulse\":"      + String(current_pulse, 0)       + ",";
    j += "\"temp\":"       + String(current_temp, 1)        + ",";
    j += "\"pressure\":"   + String(current_pressure, 0)    + ",";
    j += "\"tremor\":"     + String(current_tremor * 100, 1)+ ",";
    j += "\"light\":"      + String(current_light, 0)       + ",";
    j += "\"status\":\""   + status_label                   + "\",";
    j += "\"stressor\":\"" + dominant_stressor              + "\"";
    j += "}";
    server.send(200, "application/json", j);
  });

  // Route: GET /log → Downloadable CSV data log
  server.on("/log", []() {
    server.send(200, "text/plain", log_data);
  });

  server.begin();

  // ─── Calibration Screen ───
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("== CALIBRATING ==");
  display.println("");
  display.println("Keep STILL");
  display.println("for 60 seconds");
  display.println("");
  display.println("Place finger on");
  display.println("APDS9960 sensor");
  display.display();

  Serial.println("══════════════════════");
  Serial.println("Calibrating 60 sec...");
  Serial.println("══════════════════════");
}

// ═══════════════════════════════════════════════════════════════════════════
// ║                           MAIN LOOP                                     ║
// ═══════════════════════════════════════════════════════════════════════════

void loop() {
  // Handle incoming HTTP requests
  server.handleClient();

  // Sample sensors every 1 second
  if (millis() - last_sample >= 1000) {
    last_sample = millis();

    // ─── Read BMP180 (Pressure + Temperature) ───
    sensors_event_t event;
    bmp.getEvent(&event);
    if (event.pressure) {
      current_pressure = event.pressure;
      float temp;
      bmp.getTemperature(&temp);
      current_temp = temp;
    }

    // ─── Read All Other Sensors ───
    current_pulse  = readPPG();
    current_tremor = readTremor();
    current_light  = readLight();

    // ═════════════════════════════════════════════════════════════════════════
    // CALIBRATION PHASE — First 60 seconds
    // ═════════════════════════════════════════════════════════════════════════

    if (!calibrated && calibration_count < 60) {

      // Accumulate baseline readings
      if (current_pulse > 0) {
        baseline_pulse += current_pulse;
        pulse_cal_count++;
      }
      baseline_temp     += current_temp;
      baseline_pressure += current_pressure;
      baseline_tremor   += current_tremor;
      calibration_count++;

      // Update OLED with countdown
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("== CALIBRATING ==");
      display.print("Time: ");
      display.print(60 - calibration_count);
      display.println("s left");
      display.print("T:"); display.print(current_temp, 1);
      display.print(" P:"); display.println(current_pressure, 0);
      display.print("L:"); display.println(current_light, 0);
      if (current_pulse == 0)
        display.println("BPM: place finger!");
      else {
        display.print("BPM: ");
        display.println(current_pulse, 0);
      }
      display.display();

      Serial.print("Cal ");
      Serial.print(60 - calibration_count);
      Serial.print("s | L:");
      Serial.println(current_light);

    // ═════════════════════════════════════════════════════════════════════════
    // CALIBRATION COMPLETE — Average baselines
    // ═════════════════════════════════════════════════════════════════════════

    } else if (!calibrated) {

      baseline_temp     /= 60;
      baseline_pressure /= 60;
      baseline_tremor   /= 60;
      baseline_pulse = (pulse_cal_count > 0)
                       ? baseline_pulse / pulse_cal_count
                       : 72;
      calibrated = true;

      // Print calibration summary to Serial
      Serial.println("✅ CALIBRATED!");
      Serial.print("Base BPM: "); Serial.println(baseline_pulse);
      Serial.print("Base T:   "); Serial.println(baseline_temp);
      Serial.print("Base P:   "); Serial.println(baseline_pressure);
      Serial.print("Base TR:  "); Serial.println(baseline_tremor);

      // Two beeps to signal calibration complete
      digitalWrite(BUZZER_PIN, HIGH); delay(200);
      digitalWrite(BUZZER_PIN, LOW);  delay(150);
      digitalWrite(BUZZER_PIN, HIGH); delay(200);
      digitalWrite(BUZZER_PIN, LOW);

      // Show completion on OLED
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("CALIBRATED!");
      display.println("");
      display.print("Base T:  "); display.println(baseline_temp, 1);
      display.print("Base P:  "); display.println(baseline_pressure, 0);
      display.print("Base TR: "); display.println(baseline_tremor * 100, 2);
      display.println("");
      display.println("Monitoring...");
      display.display();
      delay(2000);
    }

    // ═════════════════════════════════════════════════════════════════════════
    // LIVE MONITORING PHASE
    // ═════════════════════════════════════════════════════════════════════════

    if (calibrated) {

      // Compute stress index
      sci_score = computeSCI();
      updateStatus();
      updateOLED();
      buzzAlert(status_label);

      // Log data to string (for /log endpoint)
      String entry = String((millis() - start_time) / 1000) + "," +
                     String(current_pulse, 1)    + "," +
                     String(current_temp, 1)     + "," +
                     String(current_pressure, 1) + "," +
                     String(current_tremor, 4)   + "," +
                     String(current_light, 0)    + "," +
                     String(sci_score, 1)        + "\n";
      log_data += entry;

      // Keep log size manageable (prevent memory overflow)
      if (log_data.length() > 8000) {
        int cut = log_data.indexOf('\n', 100);
        log_data = log_data.substring(cut + 1);
      }

      // Print to Serial Monitor
      Serial.print("SCI:");    Serial.print(sci_score, 1);
      Serial.print(" | BPM:");
      if (current_pulse == 0) Serial.print("--");
      else Serial.print(current_pulse, 0);
      Serial.print(" | T:");   Serial.print(current_temp, 1);
      Serial.print(" | P:");   Serial.print(current_pressure, 0);
      Serial.print(" | TR:");  Serial.print(current_tremor * 100, 1);
      Serial.print(" | L:");   Serial.print(current_light, 0);
      Serial.print(" | ");     Serial.print(status_label);
      Serial.print(" | ");     Serial.println(dominant_stressor);
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// ║                          END OF FIRMWARE                               ║
// ═══════════════════════════════════════════════════════════════════════════
