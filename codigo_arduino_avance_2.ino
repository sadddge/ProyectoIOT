#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "Adafruit_VL53L0X.h"
#include <ArduinoJson.h>

const char* ssid = "Songrim";
const char* password = "AABBCCDD";
const char* mqtt_server = "iot.ceisufro.cl"; 
const int mqtt_port = 1883; 
const char* device_token = "354ee7omsirwgui3zdzx";

#define BUZZER_PIN 15

LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_VL53L0X sensor = Adafruit_VL53L0X();
WiFiClient espClient;
PubSubClient client(espClient);

String currentStatus = "IDLE"; // IDLE, RUNNING, WARNING, PAUSED
long targetTimeSec = 0;        
long startTimeMillis = 0;      
long pausedAtSec = 0;          

long lastTelem = 0;
long lastBuzzer = 0;
bool buzzerState = false;

void updateLCD() {
  lcd.setCursor(0, 0);
  
  if (currentStatus == "IDLE") {
    lcd.print("TrueFocus v2.0  ");
    lcd.setCursor(0, 1);
    lcd.print("Esperando App...");
    return;
  }

  long elapsed = (millis() - startTimeMillis) / 1000;
  if (currentStatus == "PAUSED") elapsed = pausedAtSec; 
  
  long remaining = targetTimeSec - elapsed;
  if (remaining < 0) remaining = 0;

  int mins = remaining / 60;
  int secs = remaining % 60;
  
  String timeStr = (mins < 10 ? "0" : "") + String(mins) + ":" + (secs < 10 ? "0" : "") + String(secs);
  
  lcd.print("Modo: " + currentStatus + "   "); 
  lcd.setCursor(0, 1);
  lcd.print("Tiempo: " + timeStr + "    ");
}

void handleBuzzer() {
  if (currentStatus == "WARNING") {
    if (millis() - lastBuzzer > 200) {
      lastBuzzer = millis();
      buzzerState = !buzzerState;
      digitalWrite(BUZZER_PIN, buzzerState);
    }
  } else if (currentStatus == "PAUSED") {
    if (millis() - lastBuzzer > 1000) {
      lastBuzzer = millis();
      buzzerState = !buzzerState;
      digitalWrite(BUZZER_PIN, buzzerState);
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }
}

// COMUNICACION RPC

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) message += (char)payload[i];
  
  DynamicJsonDocument doc(512);
  deserializeJson(doc, message);
  String method = doc["method"];
  JsonObject params = doc["params"];

  if (method == "setSessionState") {
    String newStatus = params["status"];
    int duration = params["duration_sec"];
    
    if (currentStatus != "RUNNING" && newStatus == "RUNNING") {
        startTimeMillis = millis(); 
        targetTimeSec = duration;
    }
    if (newStatus == "PAUSED" && currentStatus == "RUNNING") {
       pausedAtSec = (millis() - startTimeMillis) / 1000;
    }
    if (newStatus == "RUNNING" && currentStatus == "PAUSED") {
       startTimeMillis = millis() - (pausedAtSec * 1000);
    }

    currentStatus = newStatus;
    updateLCD();
  }
}

void setup_wifi() {
  delay(10);
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("Conectando WiFi");
  lcd.setCursor(0,1); lcd.print(ssid);

  WiFi.begin(ssid, password);

  int dots = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    lcd.setCursor(dots, 1); 
    // Animación simple de puntos
    if (dots > 15) { lcd.setCursor(0,1); lcd.print("                "); dots=0; }
    lcd.print(".");
    dots++;
  }

  lcd.clear();
  lcd.setCursor(0,0); lcd.print("WiFi OK!");
  lcd.setCursor(0,1); lcd.print(WiFi.localIP());
  delay(2000); // Pausa para verip
}

void reconnect() {
  while (!client.connected()) {
    lcd.clear();
    lcd.setCursor(0,0); lcd.print("Conectando TB...");
    
    if (client.connect("ESP32_TF", device_token, NULL)) {
      lcd.setCursor(0,1); lcd.print("Exito! Token OK");
      delay(1000);
      
      lcd.clear();
      lcd.setCursor(0,0); lcd.print("Suscripcion RPC");
      
      if(client.subscribe("v1/devices/me/rpc/request/+")) {
         lcd.setCursor(0,1); lcd.print("RPC: OK");
      } else {
         lcd.setCursor(0,1); lcd.print("RPC: FALLO");
      }
      delay(1000);
      
    } else {
      lcd.setCursor(0,1); 
      lcd.print("Error rc="); lcd.print(client.state());
      delay(5000);
    }
  }
  lcd.clear();
}


void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  Wire.begin();
  
  lcd.init(); 
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("Iniciando...");

  lcd.setCursor(0,1);
  lcd.print("Sensor ToF...");
  if (!sensor.begin()) {
    lcd.clear();
    lcd.setCursor(0,0); lcd.print("ERROR CRITICO:");
    lcd.setCursor(0,1); lcd.print("Fallo VL53L0X");
    while(1);
  }
  lcd.print("OK");
  delay(1000);

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}


void loop() {
  if (!client.connected()) {
      reconnect();
  }
  client.loop();

  handleBuzzer();
  
  static long lastLCD = 0;
  if (millis() - lastLCD > 500) {
      lastLCD = millis();
      updateLCD();
  }

  // TELEMETRIA
  if (millis() - lastTelem > 200) {
    lastTelem = millis();
    
    VL53L0X_RangingMeasurementData_t measure;
    sensor.rangingTest(&measure, false);
    
    int distance = 0;
    bool presence = false;
    bool validReading = false;

    if (measure.RangeStatus != 4) {
      distance = measure.RangeMilliMeter;
      validReading = true;
      if (distance < 800 && distance > 30) {
          presence = true;
      }
    } else {
      distance = 1200; 
      validReading = false;
    }

    static bool lastPresence = false;
    static int lastDistance = 0;
    static long lastSent = 0;
    
    bool stateChange = (presence != lastPresence);
    bool significantChange = abs(distance - lastDistance) > 50; 
    bool heartbeat = (millis() - lastSent > 5000);

    if (stateChange || (significantChange && validReading) || heartbeat) {
        
        String payload = "{";
        payload += "\"presencia\":"; 
        payload += (presence ? "true" : "false");
        payload += ",";
        payload += "\"distancia\":"; 
        payload += distance;
        payload += "}";

        if (client.publish("v1/devices/me/telemetry", (char*) payload.c_str())) {
            Serial.println("Envio OK: " + payload);
        } else {
            Serial.println("Fallo envio MQTT");
            lcd.setCursor(0,1);
            lcd.print("ERR: ENVIO FALLO"); 
            delay(500); 
        }
        
        lastPresence = presence;
        lastDistance = distance;
        lastSent = millis();
    }
  }
}