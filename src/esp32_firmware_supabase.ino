/**********************************************************************************
 * Project: Campus Safety & Foot-Traffic Telemetry Node (Gym 7)
 * Cloud Architecture: Supabase Cloud (PostgreSQL REST API + Realtime WebSockets)
 * Hardware Connections:
 *   - PIR Motion Sensor (HC-SR501) -> GPIO 27
 *   - LDR Module (Ambient Light)   -> GPIO 34 (ADC1)
 *   - MOSFET Module (LED PWM)      -> GPIO 18
 *   - Power Supply: 5V DC (Common Ground)
 **********************************************************************************/

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ==========================================
// 1. ตั้งค่า Wi-Fi และ Supabase Cloud Credentials
// ==========================================
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// นำ Project URL และ Anon Key ของโปรเจกต์คุณใส่ไว้ให้เรียบร้อยแล้ว
const char* SUPABASE_URL      = "https://thetawwspattkkswawjt.supabase.co"; 
const char* SUPABASE_ANON_KEY = "sb_publishable_38bodlCsO-bC4rfpHWN3sg_IPuEL1y-";
const char* NODE_ID           = "NODE-GYM7-01";

// ==========================================
// 2. กำหนดขาพิน (Simplified 3 Pins)
// ==========================================
const int PIN_PIR    = 27;  // Digital In: PIR Motion Sensor
const int PIN_LDR    = 34;  // Analog In: ตรวจวัดแสงสว่างรอบข้าง (ADC1)
const int PIN_MOSFET = 18;  // PWM Out: ควบคุมการหรี่ไฟ LED ผ่าน MOSFET

// ==========================================
// 3. ตั้งค่า PWM สำหรับ LEDC
// ==========================================
const int PWM_CHANNEL    = 0;
const int PWM_FREQ       = 5000; // 5 kHz
const int PWM_RESOLUTION = 8;    // 8-bit (0 - 255)

const int DUTY_OFF   = 0;    // ดับไฟ (กลางวัน)
const int DUTY_DIM   = 77;   // หรี่ไฟ 30% (กลางคืนไม่มีคน)
const int DUTY_FULL  = 255;  // เร่งไฟ 100% (ตรวจพบคนเดินผ่าน)

// ==========================================
// 4. ค่า Threshold และ Timing
// ==========================================
const int LDR_THRESHOLD             = 1800;   // ค่า ADC เกินนี้ถือเป็นเวลากลางคืน
const unsigned long LIGHT_HOLD_TIME = 15000;  // เร่งไฟ 100% ค้างไว้นาน 15 วินาที
const unsigned long LOITER_TIME     = 25000;  // แช่นานเกิน 25 วินาที = แจ้งเตือน Loitering
const unsigned long TELEMETRY_RATE  = 10000;  // ส่งข้อมูลขึ้น Supabase ทุกๆ 10 วินาที

// ==========================================
// 5. ตัวแปรสถานะระบบ
// ==========================================
int  trafficCount        = 0;
bool prevPirState        = LOW;
bool isNight             = false;
bool motionActive        = false;
bool isLoitering         = false;
int  currentDutyCycle    = DUTY_OFF;

unsigned long motionStartTime   = 0;
unsigned long pirHighStartTime  = 0;
unsigned long lastTelemetryTime = 0;

void sendTelemetryToSupabase(String nodeId, int count, String lightState, float watts, String alertState);

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n==================================================");
  Serial.println("  CAMPUS SAFETY & FOOT-TRAFFIC TELEMETRY NODE");
  Serial.println("  Cloud Architecture: Supabase Cloud (PostgreSQL)");
  Serial.println("==================================================");

  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_LDR, INPUT);

  // ตั้งค่า PWM สำหรับ MOSFET
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(PIN_MOSFET, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, DUTY_OFF);

  // เชื่อมต่อ Wi-Fi
  Serial.print("[WiFi] Connecting to: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Online! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n[WiFi] Offline Mode Running.");
  }
}

void loop() {
  unsigned long now = millis();

  // Reconnect Wi-Fi อัตโนมัติหากหลุด
  if (WiFi.status() != WL_CONNECTED && (now % 10000 < 50)) {
    WiFi.reconnect();
  }

  int ldrValue = analogRead(PIN_LDR);
  int pirState = digitalRead(PIN_PIR);
  isNight = (ldrValue > LDR_THRESHOLD);

  // 1. ตรวจจับการเดินผ่าน (Rising Edge) -> นับจำนวนคน
  if (pirState == HIGH && prevPirState == LOW) {
    trafficCount++;
    motionActive = true;
    motionStartTime = now;
    pirHighStartTime = now;
    Serial.printf("[Event] Pedestrian Passed! Count: %d\n", trafficCount);
  }

  // 2. ตรวจจับพฤติกรรมป้วนเปี้ยนผิดปกติ (Loitering Watchdog)
  if (pirState == HIGH) {
    if (now - pirHighStartTime > LOITER_TIME) {
      if (!isLoitering) {
        isLoitering = true;
        Serial.println("[SECURITY] >>> LOITERING DETECTED AT GYM 7 <<<");
      }
    }
  } else {
    isLoitering = false;
  }
  prevPirState = pirState;

  // 3. ควบคุมแสงไฟตาม State Machine
  float currentPowerWatts = 0.6; // กำลังไฟสแตนด์บายของ ESP32
  if (!isNight) {
    currentDutyCycle = DUTY_OFF;
    currentPowerWatts = 0.6;
  } else {
    if (motionActive) {
      currentDutyCycle = DUTY_FULL;
      currentPowerWatts = 8.5; // โหลดไฟ 100%
      if (now - motionStartTime > LIGHT_HOLD_TIME) {
        motionActive = false;
      }
    } else {
      currentDutyCycle = DUTY_DIM;
      currentPowerWatts = 2.8; // โหมดหรี่ไฟ 30%
    }
  }
  ledcWrite(PWM_CHANNEL, currentDutyCycle);

  // 4. ส่งข้อมูล Telemetry ขึ้น Supabase Cloud
  if (now - lastTelemetryTime >= TELEMETRY_RATE) {
    lastTelemetryTime = now;

    String lightStatusStr = (!isNight) ? "DAY_OFF" : (motionActive ? "100%_ACTIVE" : "30%_AMBIENT");
    String alertStatusStr = isLoitering ? "LOITERING_ALERT" : "NORMAL";

    Serial.printf("[Telemetry] Light: %s | Power: %.1fW | Count: %d | Alert: %s\n",
                  lightStatusStr.c_str(), currentPowerWatts, trafficCount, alertStatusStr.c_str());

    if (WiFi.status() == WL_CONNECTED) {
      sendTelemetryToSupabase(NODE_ID, trafficCount, lightStatusStr, currentPowerWatts, alertStatusStr);
    }
  }
}

// ยิงข้อมูลเข้า Supabase PostgreSQL ผ่าน REST API
void sendTelemetryToSupabase(String nodeId, int count, String lightState, float watts, String alertState) {
  WiFiClientSecure client;
  client.setInsecure(); // ไม่ต้องตรวจสอบ SSL เพื่อความเร็ว

  HTTPClient http;
  String endpoint = String(SUPABASE_URL) + "/rest/v1/telemetry_logs";
  http.begin(client, endpoint);

  // Headers สำหรับยืนยันตัวตนกับ Supabase
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPABASE_ANON_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
  http.addHeader("Prefer", "return=minimal"); // ประหยัด RAM ไม่ต้องให้คลาวด์ส่งข้อมูลเดิมกลับมา

  // Payload JSON
  String jsonPayload = "{";
  jsonPayload += "\"node_id\":\"" + nodeId + "\",";
  jsonPayload += "\"traffic_count\":" + String(count) + ",";
  jsonPayload += "\"light_status\":\"" + lightState + "\",";
  jsonPayload += "\"power_watts\":" + String(watts, 1) + ",";
  jsonPayload += "\"alert_status\":\"" + alertState + "\"";
  jsonPayload += "}";

  int httpCode = http.POST(jsonPayload);
  if (httpCode >= 200 && httpCode < 300) {
    Serial.printf("[Supabase] Insert Success! (HTTP %d)\n", httpCode);
  } else {
    Serial.printf("[Supabase] Insert Failed: %d (%s)\n", httpCode, http.errorToString(httpCode).c_str());
  }
  http.end();
}
