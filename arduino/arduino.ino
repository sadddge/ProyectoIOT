#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// === Configuración de pines ===
#define TRIG_PIN 3
#define ECHO_PIN 2
#define BUZZER_PIN 6

// LCD I2C (dirección común 0x27 o 0x3F, depende del módulo)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Variables de control
unsigned long lastHeartbeat = 0;
unsigned long lastSensorRead = 0;
const unsigned long heartbeatInterval = 5000;
const unsigned long sensorInterval = 1000;

struct BuzzerState {
  bool active = false;        // ¿hay patrón en curso?
  bool level  = false;        // estado actual del pin (ON/OFF)
  unsigned long on_ms  = 200;
  unsigned long off_ms = 200;
  long repeat = 0;            // >0 veces restantes; -1 == infinito
  unsigned long nextToggleAt = 0;
} buzzer;

void buzzerStart(unsigned long onMs, unsigned long offMs, long repeat) {
  buzzer.on_ms  = onMs;
  buzzer.off_ms = offMs;
  buzzer.repeat = (repeat == 0) ? -1 : repeat; // extensión: repeat=0 => infinito
  buzzer.active = true;
  buzzer.level  = true;                 // comienza encendido
  digitalWrite(BUZZER_PIN, HIGH);
  buzzer.nextToggleAt = millis() + buzzer.on_ms;
}

void buzzerStop() {
  buzzer.active = false;
  buzzer.level  = false;
  digitalWrite(BUZZER_PIN, LOW);
}

void buzzerUpdate() {
  if (!buzzer.active) return;
  unsigned long now = millis();
  if (now < buzzer.nextToggleAt) return;

  // Toggle
  buzzer.level = !buzzer.level;
  digitalWrite(BUZZER_PIN, buzzer.level ? HIGH : LOW);

  if (buzzer.level) {
    // Acabamos de pasar a ON
    buzzer.nextToggleAt = now + buzzer.on_ms;
  } else {
    // Acabamos de pasar a OFF (terminó un ciclo ON)
    if (buzzer.repeat > 0) buzzer.repeat--;
    if (buzzer.repeat == 0) {
      // terminó
      buzzerStop();
      return;
    }
    buzzer.nextToggleAt = now + buzzer.off_ms;
  }
}

// ===== Funciones de envío =====
void sendReady() {
  StaticJsonDocument<200> doc;
  doc["src"] = "A";
  doc["type"] = "READY";
  serializeJson(doc, Serial);
  Serial.println();
}

void sendHeartbeat() {
  StaticJsonDocument<200> doc;
  doc["src"] = "A";
  doc["type"] = "HEARTBEAT";
  doc["t_ms"] = millis();
  serializeJson(doc, Serial);
  Serial.println();
}

void sendSensorReading(long d_mm, bool ok) {
  StaticJsonDocument<200> doc;
  doc["src"] = "A";
  doc["type"] = "SENSOR_READING";
  doc["d_mm"] = d_mm;
  doc["ok"] = ok;
  doc["t_ms"] = millis();
  serializeJson(doc, Serial);
  Serial.println();
}

void sendError(const char* code) {
  StaticJsonDocument<200> doc;
  doc["src"] = "A";
  doc["type"] = "ERROR";
  doc["code"] = code;
  serializeJson(doc, Serial);
  Serial.println();
}

void sendAck(const char* cmd) {
  StaticJsonDocument<128> doc;
  doc["src"]  = "A";
  doc["type"] = "ACK";
  doc["cmd"]  = cmd;
  serializeJson(doc, Serial); Serial.println();
}

// ===== Lectura de sensor ultrasónico =====
long readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 60000); // timeout 60ms
  if (duration == 0) return -1; // fallo
  long distance = duration / 2 / 2.91; // en mm aprox (343m/s)
  return distance;
}

// ===== Procesar comandos desde Processing =====
void processCommand(String input) {
  StaticJsonDocument<512> doc;  // subimos de 256 a 512
  DeserializationError err = deserializeJson(doc, input);
  if (err) {
    Serial.print(F("{\"src\":\"A\",\"type\":\"PARSE_ERR\",\"msg\":\""));
    Serial.print(err.c_str());
    Serial.println(F("\"}"));
    return;
  }

  const char* type = doc["type"];
  if (!type) return;

  if (strcmp(type, "LCD_TEXT") == 0) {
    String l1 = doc["l1"] | "";
    String l2 = doc["l2"] | "";

    // Sanitizar a 16 chars ASCII básicos (evitar acentos/emoji)
    l1 = sanitizeForLCD(l1, 16);
    l2 = sanitizeForLCD(l2, 16);

    lcd.clear();
    lcd.setCursor(0,0); lcd.print(l1);
    lcd.setCursor(0,1); lcd.print(l2);

    // ACK para depurar
    sendAck("LCD_TEXT");
  }
  else if (strcmp(type, "BUZZER_DRIVE") == 0) {
    const char* mode = doc["mode"] | "ON_OFF";
    int on_ms = doc["on_ms"] | 200;
    int off_ms = doc["off_ms"] | 200;
    int repeat = doc["repeat"] | 1;

    buzzerStart(on_ms, off_ms, repeat);
    sendAck("BUZZER_DRIVE");
  }
  else if (strcmp(type, "RESET") == 0) {
    lcd.clear();
    buzzerStop();
    sendAck("RESET");
  }
}

// Sanitiza: quita no-ASCII y corta a 16
String sanitizeForLCD(String s, int maxLen) {
  String out = "";
  for (size_t i=0; i<s.length(); i++) {
    char c = s[i];
    if (c >= 32 && c <= 126) { // ASCII imprimible
      out += c;
      if (out.length() >= maxLen) break;
    }
  }
  return out;
}


// ===== Setup/Loop =====

void setup() {
  Serial.begin(9600);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  lcd.init();
  lcd.backlight();

  sendReady();
}


void loop() {
  // 1. Lectura periódica del sensor
  if (millis() - lastSensorRead > sensorInterval) {
    lastSensorRead = millis();
    long dist = readDistance();
    if (dist > 0) sendSensorReading(dist, true);
    else sendError("ECHO_TIMEOUT");
  }

  // 2. Heartbeat
  if (millis() - lastHeartbeat > heartbeatInterval) {
    lastHeartbeat = millis();
    sendHeartbeat();
  }

  buzzerUpdate();

  // 3. Recepción de mensajes
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    processCommand(input);
  }
}
