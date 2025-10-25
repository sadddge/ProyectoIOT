#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>

const char* ssid = "Songrim";
const char* password = "AABBCCDD";
const char* mqtt_server = "192.168.1.87";

WiFiClient espClient;
PubSubClient client(espClient);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- Pines ---
#define PIR_PIN 32       // Pin del sensor PIR
#define TRIG_PIN 26
#define ECHO_PIN 27
#define BUZZER_PIN 19

// --- Tópicos MQTT (Solo de SALIDA de datos crudos) ---
#define TOPIC_SENSOR_PIR "pomodoro/sensor/pir"
#define TOPIC_SENSOR_DIST "pomodoro/sensor/distance"
  
// --- Tópicos MQTT (De ENTRADA de comandos) ---
#define TOPIC_CMD_LCD "pomodoro/actuador/lcd"
#define TOPIC_CMD_BUZZER "pomodoro/actuador/buzzer"

long lastSensorRead = 0;
const long sensorInterval = 2000; // Lee sensores cada 2 segundos

// Esta es la función que recibe mensajes de Node-RED
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  StaticJsonDocument<256> doc;
  deserializeJson(doc, message);

  if (strcmp(topic, TOPIC_CMD_LCD) == 0) {
    const char* l1 = doc["l1"] | "                ";
    const char* l2 = doc["l2"] | "                ";
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(l1);
    lcd.setCursor(0, 1);
    lcd.print(l2);
  } 
  else if (strcmp(topic, TOPIC_CMD_BUZZER) == 0) {
    int on_ms = doc["on_ms"] | 200;
    int repeat = doc["repeat"] | 1;
    // (Aquí va tu lógica de buzzer, como la tenías en el 'arduino.ino' original)
    for(int i=0; i < repeat; i++){
      digitalWrite(BUZZER_PIN, HIGH);
      delay(on_ms);
      digitalWrite(BUZZER_PIN, LOW);
      delay(doc["off_ms"] | on_ms);
    }
  }
}

// ... (Aquí van las funciones setup() y reconnect() de la respuesta anterior) ...
// ... Ellas no cambian, pero recuerda suscribirte a:
// client.subscribe(TOPIC_CMD_LCD);
// client.subscribe(TOPIC_CMD_BUZZER);
// ...

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  lcd.init();
  lcd.backlight();
  lcd.print("Conectando WiFi...");


  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  lcd.clear();
  lcd.print("Conectado!");

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

long readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // 30ms timeout
  if (duration == 0) return 999; // Fuera de rango
  
  // Calcular distancia en cm
  long distance_cm = duration * 0.034 / 2;
  return distance_cm;
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Conectando a MQTT...");
    if (client.connect("ESP32_Pomodoro_Client")) {
      Serial.println("conectado");
      // Suscribirse a los tópicos de actuadores
      client.subscribe(TOPIC_CMD_LCD);
      client.subscribe(TOPIC_CMD_BUZZER);
    } else {
      Serial.print("falló, rc=");
      Serial.print(client.state());
      Serial.println(" reintentando en 5 seg");
      delay(5000);
    }
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop(); // Mantener viva la conexión MQTT

  long now = millis();
  if (now - lastSensorRead > sensorInterval) {
    lastSensorRead = now;

    // 1. Leer PIR
    bool pirState = digitalRead(PIR_PIN); // true o false

    // 2. Leer Distancia (Ultrasónico)
    float distance_cm = readDistance();

    // Filtro de rango
    if (distance_cm > 80 || distance_cm < 2) {
        distance_cm = 80.0;
    }
    
    // ----------------------------------------------------
    // IMPORTANTE: El sensor no es fiable fuera de su rango (10cm - 80cm)
    // Si el voltaje es muy alto (muy cerca) o muy bajo (muy lejos), 
    // la fórmula da valores extraños. Los filtramos.
    if (distance_cm < 10) {
        distance_cm = 10;
    }
    if (distance_cm > 80) {
        distance_cm = 80;
    }

    // 3. Publicar valores CRUDOS en tópicos separados
    // Convertimos a String para publicar
    char distString[8];
    dtostrf(distance_cm, 4, 2, distString); // float a string

    client.publish(TOPIC_SENSOR_PIR, pirState ? "true" : "false");
    client.publish(TOPIC_SENSOR_DIST, distString);

    Serial.print("PIR: "); Serial.print(pirState);
    Serial.print(" | Distance: "); Serial.println(distString);
  }
}
