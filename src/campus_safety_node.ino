/**********************************************************************************
 * Project: Campus Safety & Foot-Traffic Telemetry Node (Gym 7)
 * Edition: Smart Grid Edition (Mains 5V DC - No Solar Battery Complexity)
 * Target Board: ESP32 Dev Module (Dual-Core FreeRTOS Architecture)
 * Hardware Connections:
 *   - PIR Motion Sensor (HC-SR501) -> GPIO 27 (Digital IN)
 *   - LDR Module (Ambient Light)   -> GPIO 34 (Digital IN ขา DO)
 *   - MOSFET Module / LED          -> GPIO 18 (PWM 5kHz)
 *   - Onboard Status LED           -> GPIO 2  (Wi-Fi Online/Offline Indicator)
 *   - Power Supply: 5V DC Adapter / USB
 **********************************************************************************/

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ==========================================
// 1. ตั้งค่า Wi-Fi และ Google Apps Script
// ==========================================
const char* WIFI_SSID         = "acaii";
const char* WIFI_PASSWORD     = "11111111";
const char* GOOGLE_SCRIPT_URL = "https://script.google.com/macros/s/AKfycbzncy-C_VV7vz-ZxKnKT_dgyhzXmPWLef3YGY6YuxaTJczWhCDAvPu5xad_2v3xgNjT/exec";
const char* NODE_ID           = "NODE-GYM7-01";

// ==========================================
// 2. กำหนดขาพิน (Hardware Pinout)
// ==========================================
const int PIN_PIR        = 27;  // Digital In: PIR Motion Sensor (HC-SR501 OUT)
const int PIN_LDR        = 34;  // Digital In: ขา DO จากโมดูลเซนเซอร์วัดแสง (LM393)
const int PIN_MOSFET     = 18;  // PWM Out: ควบคุมการหรี่ไฟ LED ทางเดิน
const int PIN_STATUS_LED = 2;   // Digital Out: ไฟสถานะ Wi-Fi บนบอร์ด ESP32 (Onboard Blue LED)

// ==========================================
// 3. ตั้งค่า PWM สำหรับ LEDC
// ==========================================
const int PWM_CHANNEL    = 0;
const int PWM_FREQ       = 5000; // 5 kHz
const int PWM_RESOLUTION = 8;    // 8-bit (0 - 255)

const int DUTY_OFF   = 0;    // 0% (เวลากลางวัน ดับไฟประหยัดพลังงาน)
const int DUTY_DIM   = 77;   // 30% Eco-Ambient Glow (กลางคืนไม่มีคน)
const int DUTY_FULL  = 255;  // 100% Emergency / Walkway Illumination (คนเดินผ่าน)

// ==========================================
// 4. ค่า Threshold และ Timing
// ==========================================
const int LDR_NIGHT_STATE           = HIGH;   // ขา DO: มืด = HIGH, สว่าง = LOW
const unsigned long LIGHT_HOLD_TIME = 15000;  // เร่งไฟ 100% ค้างไว้นาน 15 วินาที
const unsigned long LOITER_TIME     = 25000;  // ยืนแช่เกิน 25 วินาที = แจ้งเตือน Loitering

// ==========================================
// 5. ตัวแปรสถานะระบบ
// ==========================================
int   trafficCount        = 0;
bool  prevPirState        = LOW;
bool  isNight             = false;
bool  motionActive        = false;
bool  isLoitering         = false;
int   currentDutyCycle    = DUTY_OFF;
float currentPowerWatts   = 0.6;

unsigned long motionStartTime   = 0;
unsigned long pirHighStartTime  = 0;

// FreeRTOS Task Handle สำหรับสื่อสารข้ามแกน (Core 1 -> Core 0)
TaskHandle_t telemetryTaskHandle = NULL;

void sendTelemetry(String nodeId, int count, String lightState, float watts, String alertState);
void telemetryTask(void *pvParameters);
void setLightDuty(int duty);

void setLightDuty(int duty) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcWrite(PIN_MOSFET, duty);
#else
  ledcWrite(PWM_CHANNEL, duty);
#endif
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n==================================================");
  Serial.println("  CAMPUS SAFETY & FOOT-TRAFFIC TELEMETRY NODE");
  Serial.println("  Real-time Event-Driven Grid Edition (Gym 7)");
  Serial.println("==================================================");

  pinMode(PIN_PIR, INPUT_PULLDOWN);
  pinMode(PIN_LDR, INPUT);
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);

  // ตั้งค่า PWM ขับหลอดไฟ LED
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcAttach(PIN_MOSFET, PWM_FREQ, PWM_RESOLUTION);
  setLightDuty(DUTY_OFF);
#else
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(PIN_MOSFET, PWM_CHANNEL);
  setLightDuty(DUTY_OFF);
#endif

  // 1. เชื่อมต่อ Wi-Fi พร้อมแจ้งเตือนผ่านไฟสถานะ
  Serial.print("[WiFi] Connecting to: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 30) {
    // กะพริบไฟสถานะบนบอร์ดถี่ๆ แสดงว่ากำลังพยายามเชื่อมต่อ
    digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
    delay(250);
    Serial.print(".");
    retries++;
  }

  // 2. การแจ้งเตือนที่ฝั่งฮาร์ดแวร์เมื่อต่อเน็ตสำเร็จ (Hardware Internet Connected Alerts)
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Online! IP: " + WiFi.localIP().toString());
    
    // แจ้งเตือน 1: ไฟสถานะบนบอร์ด (GPIO 2) ติดสว่างค้าง 100% บ่งบอกว่า "Internet Online"
    digitalWrite(PIN_STATUS_LED, HIGH);

    // แจ้งเตือน 2: ไฟโคมทางเดิน (GPIO 18) กะพริบยืนยันต้อนรับ 3 ครั้ง (Triple Welcome Flash)
    for (int i = 0; i < 3; i++) {
      setLightDuty(DUTY_FULL);
      delay(120);
      setLightDuty(DUTY_OFF);
      delay(120);
    }
  } else {
    Serial.println("\n[WiFi] Offline Fallback - Autonomous Mode Running.");
    digitalWrite(PIN_STATUS_LED, LOW);
  }

  // 3. แยกงานส่ง Telemetry ขึ้น Cloud ไปรันเบื้องหลังบน Core 0
  // ให้ Core 1 ทำหน้าที่ควบคุมแสงไฟ Real-Time 100% ใน 0.0001 วินาที
  xTaskCreatePinnedToCore(
    telemetryTask,
    "telemetryTask",
    8192,
    NULL,
    1,
    &telemetryTaskHandle,
    0
  );
}

void loop() {
  unsigned long now = millis();

  int ldrState = digitalRead(PIN_LDR);
  int pirState = digitalRead(PIN_PIR);
  isNight = (ldrState == LDR_NIGHT_STATE);

  // 1. ตรวจจับคนเดินผ่าน (Rising Edge) -> เร่งไฟทันที 0.001s + ปลุก Core 0 ส่ง Cloud ทันที!
  if (pirState == HIGH && prevPirState == LOW) {
    trafficCount++;
    motionActive = true;
    motionStartTime = now;
    pirHighStartTime = now;
    Serial.printf("[Event] Pedestrian Passed! Total Count: %d\n", trafficCount);

    // ⚡ REAL-TIME EVENT-DRIVEN TRIGGER:
    // ส่งสัญญาณปลุก Core 0 ให้ยิงข้อมูลขึ้นแดชบอร์ดทันที โดยไม่ต้องรอรอบเวลา!
    if (telemetryTaskHandle != NULL) {
      xTaskNotifyGive(telemetryTaskHandle);
    }
  }

  // 2. ตรวจจับพฤติกรรมป้วนเปี้ยนผิดปกติ (Loitering Watchdog)
  if (pirState == HIGH) {
    if (now - pirHighStartTime > LOITER_TIME) {
      if (!isLoitering) {
        isLoitering = true;
        Serial.println("[SECURITY] >>> LOITERING DETECTED AT GYM 7 <<<");
        if (telemetryTaskHandle != NULL) {
          xTaskNotifyGive(telemetryTaskHandle);
        }
      }
    }
  } else {
    isLoitering = false;
  }
  prevPirState = pirState;

  // 3. ควบคุมแสงไฟตาม State Machine (Real-time ใน 0.0001 วินาที)
  if (!isNight) {
    currentDutyCycle = DUTY_OFF;
    currentPowerWatts = 0.6;
  } else {
    if (motionActive) {
      currentDutyCycle = DUTY_FULL;
      currentPowerWatts = 8.5; // โหลดไฟ 100%
      if (now - motionStartTime > LIGHT_HOLD_TIME) {
        motionActive = false;
        // เมื่อไฟดับลงโหมดหรี่ ส่งข้อมูลอัปเดตสถานะแดชบอร์ดทันที
        if (telemetryTaskHandle != NULL) {
          xTaskNotifyGive(telemetryTaskHandle);
        }
      }
    } else {
      currentDutyCycle = DUTY_DIM;
      currentPowerWatts = 2.8; // โหมดหรี่ไฟประหยัดพลังงาน 30%
    }
  }

  setLightDuty(currentDutyCycle);
}

// ====================================================================
// Background Telemetry Task (รันแยกบน Core 0 ไม่รบกวนการเปิด-ปิดไฟ)
// รองรับ Real-time Event-Driven Push ทันทีที่มีคนเดินผ่าน
// ====================================================================
void telemetryTask(void *pvParameters) {
  unsigned long lastSendTime = 0;
  const unsigned long MIN_SEND_INTERVAL  = 1200; // ป้องกันการส่งซ้ำถี่เกินไปไม่เกิน 1.2 วินาทีต่อครั้ง
  const unsigned long HEARTBEAT_INTERVAL = 5000; // ส่ง Heartbeat ทุก 5 วินาทีหากไม่มีความเคลื่อนไหว

  for (;;) {
    // รอรับสัญญาณ Notify เมื่อมีคนเดิน หรือรอจนครบ Heartbeat Interval
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HEARTBEAT_INTERVAL));

    unsigned long now = millis();
    if (now - lastSendTime < MIN_SEND_INTERVAL) {
      vTaskDelay(pdMS_TO_TICKS(MIN_SEND_INTERVAL - (now - lastSendTime)));
    }

    // ตรวจสอบและดูแลสถานะการเชื่อมต่อ Wi-Fi
    if (WiFi.status() != WL_CONNECTED) {
      digitalWrite(PIN_STATUS_LED, LOW); // ดับไฟแจ้งเตือนว่าเน็ตหลุด
      WiFi.reconnect();
      vTaskDelay(pdMS_TO_TICKS(800));
      if (WiFi.status() == WL_CONNECTED) {
        digitalWrite(PIN_STATUS_LED, HIGH); // ติดสว่างเมื่อเน็ตกลับมา
      } else {
        continue;
      }
    } else {
      digitalWrite(PIN_STATUS_LED, HIGH); // ติดสว่างค้าง 100%
    }

    int count = trafficCount;
    float watts = currentPowerWatts;
    String lightStatusStr = (!isNight) ? "DAY_OFF" : (motionActive ? "100%_ACTIVE" : "30%_AMBIENT");
    String alertStatusStr = isLoitering ? "LOITERING_ALERT" : "NORMAL";

    Serial.printf("[Telemetry] Light: %s | Power: %.1fW | Count: %d | Alert: %s\n",
                  lightStatusStr.c_str(), watts, count, alertStatusStr.c_str());

    sendTelemetry(NODE_ID, count, lightStatusStr, watts, alertStatusStr);
    lastSendTime = millis();
  }
}

void sendTelemetry(String nodeId, int count, String lightState, float watts, String alertState) {
  WiFiClientSecure client;
  client.setInsecure(); // ไม่ตรวจสอบ SSL เพื่อความลื่นไหลระดับ Real-time

  HTTPClient http;
  http.begin(client, GOOGLE_SCRIPT_URL);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.addHeader("Content-Type", "application/json");

  String jsonPayload = "{";
  jsonPayload += "\"node_id\":\"" + nodeId + "\",";
  jsonPayload += "\"traffic_count\":" + String(count) + ",";
  jsonPayload += "\"light_status\":\"" + lightState + "\",";
  jsonPayload += "\"power_watts\":" + String(watts, 1) + ",";
  jsonPayload += "\"alert_status\":\"" + alertState + "\"";
  jsonPayload += "}";

  int httpCode = http.POST(jsonPayload);
  if (httpCode == 200 || httpCode == 302 || httpCode == 301) {
    Serial.printf("[HTTP] Sync Success -> Google Sheet Updated! (Code: %d)\n", httpCode);
  } else if (httpCode > 0) {
    Serial.printf("[HTTP] Server Response (Code: %d)\n", httpCode);
  } else {
    Serial.printf("[HTTP] Sync Error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}
