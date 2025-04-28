#include <WiFi.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <PubSubClient.h>

// Definición de pines
#define ONE_WIRE_BUS 13
#define BUZZER_PIN 12
#define FLAME_SENSOR_PIN 32
#define GAS_SENSOR_PIN 34
#define RGB_RED_PIN 33
#define RGB_GREEN_PIN 25
#define RGB_BLUE_PIN 26


// Configuración de sensores
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Datos WiFi y servidor
const char* ssid = "Esteban-A55";
const char* password = "Esteban1234";
WiFiServer server(80);

// MQTT Configuration
const char* mqtt_server = "192.168.196.122";
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Estructura para datos compartidos protegidos por semáforo
typedef struct {
  float tempC;
  int gasValue;
  bool flameDetected;
  bool alertaMedia;
  bool alertaMediaAlta;
  bool incendio;
  String razonAlerta;
  float tempInicial;
  int gasInicial;
  bool lcdOn;
  bool buzzerGlobalOn;
  bool rgbOn;
} SharedData;

SharedData sharedData;
SemaphoreHandle_t xSemaphore;

// Registro de los últimos 10 datos
#define MAX_REGISTROS 10
String registros[MAX_REGISTROS];
int registroIndex = 0;

// Variables para patrón del buzzer
unsigned long lastBuzzerToggle = 0;
bool buzzerOnState = false;

// Variables para el temporizador de alertas
unsigned long tempAlertaStartTime = 0;
unsigned long gasAlertaStartTime = 0;
unsigned long flameAlertaStartTime = 0;
const unsigned long ALERTA_DURACION_INCENDIO = 5000;

// MQTT Callback function
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Mensaje recibido en el tópico: ");
  Serial.println(topic);

  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("Contenido: ");
  Serial.println(message);

  if (String(topic) == "esp32/alarma") {
    if (message == "ON") {
      if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
        sharedData.buzzerGlobalOn = true;
        xSemaphoreGive(xSemaphore);
      }
      Serial.println("Alarma activada por MQTT");
    } else if (message == "OFF") {
      if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
        sharedData.buzzerGlobalOn = false;
        xSemaphoreGive(xSemaphore);
      }
      Serial.println("Alarma desactivada por MQTT");
    }
  }

  if (String(topic) == "esp32/led") {
    if (message == "ON") {
      if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
        sharedData.rgbOn = true;
        xSemaphoreGive(xSemaphore);
        Serial.println("LED activada por MQTT");
      }
    } else if (message == "OFF") {
      if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
        sharedData.rgbOn = false;
        xSemaphoreGive(xSemaphore);
        Serial.println("LED desactivado por MQTT");

      }    
    }
  }
}

// MQTT Reconnect function
void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Intentando conexión MQTT...");
    if (mqttClient.connect("ESP32Publisher")) {
      Serial.println("Conectado al broker MQTT");
      mqttClient.subscribe("esp32/alarma");
      mqttClient.subscribe("esp32/led");
    } else {
      Serial.print("Fallo, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" Reintentando en 5s...");
      delay(5000);
    }
  }
}

void actualizarEstado() {
  sensors.requestTemperatures();
  float currentTemp = sensors.getTempCByIndex(0);
  int currentGas = analogRead(GAS_SENSOR_PIN);
  bool currentFlame = (digitalRead(FLAME_SENSOR_PIN) == LOW);

  bool tempAltaAhora = (currentTemp >= sharedData.tempInicial + 5);
  bool gasAltoAhora = (currentGas >= sharedData.gasInicial + 100);
  bool llamaAhora = currentFlame;

  String currentRazon = "Normal";
  bool currentAlertaMedia = false;
  bool currentAlertaMediaAlta = false;
  bool currentIncendio = false;

  // Verificar condiciones sostenidas
  if (tempAltaAhora) {
    if (tempAlertaStartTime == 0) {
      tempAlertaStartTime = millis();
    } else if (millis() - tempAlertaStartTime >= ALERTA_DURACION_INCENDIO) {
      currentIncendio = true;
      currentRazon = "INCENDIO: Temp alta sostenida";
    }
  } else {
    tempAlertaStartTime = 0;
  }

  if (gasAltoAhora) {
    if (gasAlertaStartTime == 0) {
      gasAlertaStartTime = millis();
    } else if (millis() - gasAlertaStartTime >= ALERTA_DURACION_INCENDIO) {
      currentIncendio = true;
      currentRazon = "INCENDIO: Gas alto sostenido";
    }
  } else {
    gasAlertaStartTime = 0;
  }

  if (llamaAhora) {
    if (flameAlertaStartTime == 0) {
      flameAlertaStartTime = millis();
    } else if (millis() - flameAlertaStartTime >= ALERTA_DURACION_INCENDIO) {
      currentIncendio = true;
      currentRazon = "INCENDIO: Llama detectada sostenida";
    }
  } else {
    flameAlertaStartTime = 0;
  }

  // Si no hay incendio, evaluar alertas normales
  if (!currentIncendio) {
    if (tempAltaAhora || gasAltoAhora || llamaAhora) {
      currentAlertaMediaAlta = true;
      currentRazon = "Alerta alta: ";
      if (tempAltaAhora) currentRazon += "Temp alta ";
      if (gasAltoAhora) currentRazon += "Gas alto ";
      if (llamaAhora) currentRazon += "Llama detectada";
    } else if (currentTemp >= sharedData.tempInicial + 3 || currentGas >= sharedData.gasInicial + 50) {
      currentAlertaMedia = true;
      currentRazon = "Alerta media: ";
      if (currentTemp >= sharedData.tempInicial + 3) currentRazon += "Temp moderada ";
      if (currentGas >= sharedData.gasInicial + 50) currentRazon += "Gas moderado";
    }
  }

  // Actualizar datos compartidos de forma segura
  if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
    sharedData.tempC = currentTemp;
    sharedData.gasValue = currentGas;
    sharedData.flameDetected = currentFlame;
    sharedData.alertaMedia = currentAlertaMedia;
    sharedData.alertaMediaAlta = currentAlertaMediaAlta;
    sharedData.incendio = currentIncendio;
    sharedData.razonAlerta = currentRazon;
    xSemaphoreGive(xSemaphore);
  }

  // Almacenar registro (no necesita semáforo porque solo escribe una tarea)
  String reg = "Temp: " + String(currentTemp) + "C, Gas: " + String(currentGas) + 
               ", Llama: " + (currentFlame ? "SI" : "NO") + ", Estado: " + currentRazon;
  registros[registroIndex] = reg;
  registroIndex = (registroIndex + 1) % MAX_REGISTROS;

  // Publicar datos por MQTT
  String payload = "{\"temperatura\":";
  payload += String(currentTemp, 1);
  payload += ",\"gas\":";
  payload += String(currentGas);
  payload += ",\"llama\":";
  payload += (currentFlame ? "true" : "false");
  payload += ",\"estado\":\"";
  payload += currentRazon;
  payload += "\"}";

  mqttClient.publish("fire/data", (char*) payload.c_str());
}


void actualizarRGB() {
  bool localRgbOn, localIncendio, localAlertaMediaAlta, localAlertaMedia;
  
  if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
    localRgbOn = sharedData.rgbOn;
    localIncendio = sharedData.incendio;
    localAlertaMediaAlta = sharedData.alertaMediaAlta;
    localAlertaMedia = sharedData.alertaMedia;
    xSemaphoreGive(xSemaphore);
  }

  if (localRgbOn) {
    if (localIncendio) {
      analogWrite(RGB_RED_PIN, 0);
      analogWrite(RGB_GREEN_PIN, 255);
      analogWrite(RGB_BLUE_PIN, 255);
    } else if (localAlertaMediaAlta) {
      analogWrite(RGB_RED_PIN, 0);
      analogWrite(RGB_GREEN_PIN, 90);
      analogWrite(RGB_BLUE_PIN, 255);
    } else if (localAlertaMedia) {
      analogWrite(RGB_RED_PIN, 0);
      analogWrite(RGB_GREEN_PIN, 0);
      analogWrite(RGB_BLUE_PIN, 255);
    } else {
      analogWrite(RGB_RED_PIN, 255);
      analogWrite(RGB_GREEN_PIN, 0);
      analogWrite(RGB_BLUE_PIN, 255);
    }
  } else {
    analogWrite(RGB_RED_PIN, 0);
    analogWrite(RGB_GREEN_PIN, 0);
    analogWrite(RGB_BLUE_PIN, 0);
  }
}

void actualizarBuzzer() {
  bool localBuzzerOn, localIncendio, localAlertaMediaAlta, localAlertaMedia;
  
  if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
    localBuzzerOn = sharedData.buzzerGlobalOn;
    localIncendio = sharedData.incendio;
    localAlertaMediaAlta = sharedData.alertaMediaAlta;
    localAlertaMedia = sharedData.alertaMedia;
    xSemaphoreGive(xSemaphore);
  }

  unsigned long currentMillis = millis();
  
  if (!localBuzzerOn || !(localIncendio || localAlertaMediaAlta || localAlertaMedia)) {
    digitalWrite(BUZZER_PIN, HIGH);
    return;
  }

  if (localIncendio) {
    digitalWrite(BUZZER_PIN, LOW);
  } 
  else if (localAlertaMediaAlta) {
    if (currentMillis - lastBuzzerToggle >= 2000UL) {
      lastBuzzerToggle = currentMillis;
      buzzerOnState = !buzzerOnState;
    }
    digitalWrite(BUZZER_PIN, buzzerOnState ? LOW : HIGH);
  } 
  else if (localAlertaMedia) {
    if (currentMillis - lastBuzzerToggle >= 4000UL) {
      lastBuzzerToggle = currentMillis;
      buzzerOnState = !buzzerOnState;
    }
    digitalWrite(BUZZER_PIN, buzzerOnState ? LOW : HIGH);
  }
}

void sensorTask(void* pvParameters) {
  // Tomar semáforo para inicializar valores
  if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
    sensors.requestTemperatures();
    sharedData.tempInicial = sensors.getTempCByIndex(0);
    sharedData.gasInicial = analogRead(GAS_SENSOR_PIN);
    xSemaphoreGive(xSemaphore);
  }

  while (1) {
    // Handle MQTT connection
    if (!mqttClient.connected()) {
      reconnectMQTT();
    }
    mqttClient.loop();

    actualizarEstado();
    actualizarRGB();
    actualizarBuzzer();

    // Imprimir en consola (solo lectura, no necesita semáforo)
    Serial.print("Temp: ");
    Serial.print(sharedData.tempC);
    Serial.print("C, Gas: ");
    Serial.print(sharedData.gasValue);
    Serial.print(", Llama: ");
    Serial.print(sharedData.flameDetected ? "SI" : "NO");
    Serial.print(", Estado: ");
    Serial.println(sharedData.razonAlerta);

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);


  delay(2000);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RGB_RED_PIN, OUTPUT);
  pinMode(RGB_GREEN_PIN, OUTPUT);
  pinMode(RGB_BLUE_PIN, OUTPUT);
  pinMode(FLAME_SENSOR_PIN, INPUT);

  digitalWrite(BUZZER_PIN, HIGH);

  // Inicializar semáforo
  xSemaphore = xSemaphoreCreateMutex();
  if (xSemaphore == NULL) {
    Serial.println("Error al crear el semáforo");
    while(1);
  }

  // Inicializar datos compartidos
  sharedData.lcdOn = true;
  sharedData.buzzerGlobalOn = true;
  sharedData.rgbOn = true;

  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.print(WiFi.localIP());
  server.begin();
  sensors.begin();

  // Configure MQTT
  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setCallback(callback);

  xTaskCreate(sensorTask, "SensorTask", 4096, NULL, 1, NULL);
}

void loop() {
  WiFiClient client = server.available();
  if (client) {
    String request = client.readStringUntil('\r');
    client.flush();

    // Procesar comandos con protección de semáforo
    if (request.indexOf("/offLCD") != -1) {
      if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
        sharedData.lcdOn = false;
        xSemaphoreGive(xSemaphore);
      }
    }
    if (request.indexOf("/onLCD") != -1) {
      if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
        sharedData.lcdOn = true;
        xSemaphoreGive(xSemaphore);
      }
    }
    if (request.indexOf("/offBuzzer") != -1) {
      if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
        sharedData.buzzerGlobalOn = false;
        xSemaphoreGive(xSemaphore);
      }
    }
    if (request.indexOf("/onBuzzer") != -1) {
      if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
        sharedData.buzzerGlobalOn = true;
        xSemaphoreGive(xSemaphore);
      }
    }
    if (request.indexOf("/offRGB") != -1) {
      if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
        sharedData.rgbOn = false;
        xSemaphoreGive(xSemaphore);
      }
    }
    if (request.indexOf("/onRGB") != -1) {
      if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
        sharedData.rgbOn = true;
        xSemaphoreGive(xSemaphore);
      }
    }

    // Leer datos compartidos para la respuesta HTTP
    float localTemp;
    int localGas;
    bool localFlame;
    String localRazon;
    
    if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
      localTemp = sharedData.tempC;
      localGas = sharedData.gasValue;
      localFlame = sharedData.flameDetected;
      localRazon = sharedData.razonAlerta;
      xSemaphoreGive(xSemaphore);
    }

    // Generar respuesta HTTP
    client.print(F("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"));
    client.print(F("<!DOCTYPE html><html lang='es'><head>"
                   "<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>"
                   "<title>ESP32 Sensor Data</title>"
                   "<style>"
                   "*{margin:0;padding:0;box-sizing:border-box;}"
                   "body{font-family:'Segoe UI',Arial,sans-serif;height:fit-content;width:100%;"
                   "background:linear-gradient(to bottom,#f8ffe8 0%,#e3f5ab 33%,#b7df2d 100%);color:white;text-align:center;}"
                   ".info-container{margin-top:20px;background:rgba(0,0,0,0.093);color:white;display:flex;"
                   "width:fit-content;flex-direction:column;justify-self:center;padding:10px;border-radius:10px;"
                   "text-shadow:1px 1px 1px rgba(0,0,0,0.192);}"
                   ".info-container h3{font-size:1.5em;margin:1px;}"
                   ".btn-container{display:flex;justify-content:center;margin-top:8px;}"
                   "h1{color:black;font-weight:800;}"
                   "h2{margin-top:20px;color:white;font-weight:800;text-shadow:1px 1px 1px rgba(0,0,0,0.274);}"
                   "p{font-size:1.2em;}"
                   ".elementOn{background:linear-gradient(to bottom,#b7deed 0%,#71ceef 50%,#21b4e2 51%,#b7deed 100%);border-radius:8px 0 0 8px;}"
                   ".elementOff{background:linear-gradient(to bottom,#f3c5bd 0%,#e86c57 50%,#ea2803 51%,#ff6600 75%,#c72200 100%);border-radius:0 8px 8px 0;}"
                   ".btn{flex:1;max-width:180px;border:none;padding:10px 20px;color:white;font-size:1em;cursor:pointer;transition:all 200ms;"
                   "text-shadow:1px 1px 1px rgba(0,0,0,0.692);}"
                   ".btn:hover{filter:brightness(1.1);} .btn:active{filter:brightness(0.9);}"
                   "table{margin:20px auto;border-collapse:collapse;width:80%;max-width:600px;background:rgba(255,255,255,0.1);border-radius:10px;overflow:hidden;}"
                   "th,td{padding:10px;border-bottom:1px solid rgba(255,255,255,0.275);text-shadow:1px 1px 1px rgba(0,0,0,0.338);}"
                   "th{background:linear-gradient(to bottom,#a9db80 0%,#96c56f 100%);color:white;text-shadow:1px 1px 1px rgba(0,0,0,0.692);}"
                   "</style></head><body>"));

    client.print(F("<h1>ESP32 Sensor Data</h1>"
                   "<article class='info-container'>"
                   "<h3>Datos Actuales</h3>"));
    client.print(F("<p><b>Temperatura:</b> "));
    client.print(localTemp);
    client.print(F(" C°</p>"));
    client.print(F("<p><b>Gas:</b> "));
    client.print(localGas);
    client.print(F("</p>"));
    client.print(F("<p><b>Llama:</b> "));
    client.print(localFlame ? "SI" : "NO");
    client.print(F("</p>"));
    client.print(F("<p><b>Estado: "));
    client.print(localRazon);
    client.print(F("</b></p></article>"));

    client.print(F("<div class='btn-container'>"
                   "<a href='/onLCD'><button class='elementOn btn'>Encender LCD</button></a>"
                   "<a href='/offLCD'><button class='btn elementOff'>Apagar LCD</button></a></div>"));

    client.print(F("<div class='btn-container'>"
                   "<a href='/onBuzzer'><button class='elementOn btn'>Encender Buzzer</button></a>"
                   "<a href='/offBuzzer'><button class='btn elementOff'>Apagar Buzzer</button></a></div>"));

    client.print(F("<div class='btn-container'>"
                   "<a href='/onRGB'><button class='elementOn btn'>Encender RGB</button></a>"
                   "<a href='/offRGB'><button class='btn elementOff'>Apagar RGB</button></a></div>"));

    client.print(F("<h2>Últimos Registros</h2><table><tr><th>Registro</th></tr>"));
    for (int i = 0; i < MAX_REGISTROS; i++) {
      int idx = (registroIndex - 1 - i + MAX_REGISTROS) % MAX_REGISTROS;
      if (registros[idx] != "") {
        client.print(F("<tr><td>"));
        client.print(registros[idx]);
        client.print(F("</td></tr>"));
      }
    }
    client.print(F("</table></body></html>"));
    client.print("<meta http-equiv='refresh' content='1'>");
    client.stop();
  }
}