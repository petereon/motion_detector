#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <GranularThrottle.cpp>
#include <PubSubClient.h>

struct Notification {
  char topic[32];
  char payload[32];
};

struct DeviceState {
  char ssid[32];
  char password[32];
  char mqttServer[32];
  int  mqttPort;
};

WiFiClient   WIFI_CLIENT;
PubSubClient MQTT_CLIENT;

ESP8266WebServer server(80);
DeviceState      deviceState;

GranularPinThrottle throttle = GranularPinThrottle();

bool isDeviceInitialized(DeviceState deviceState) {
  return strlen(deviceState.ssid) > 0 && strlen(deviceState.password) > 0 && strlen(deviceState.mqttServer) > 0 &&
         deviceState.mqttPort > 0;
}

void (*resetFunc)(void) = 0;

void populateDeviceState(DeviceState &deviceState, ESP8266WebServer &server) {
  strncpy(deviceState.ssid, server.arg("ssid").c_str(), sizeof(deviceState.ssid) - 1);
  deviceState.ssid[sizeof(deviceState.ssid) - 1] = '\0'; // Ensure null-termination
  strncpy(deviceState.password, server.arg("password").c_str(), sizeof(deviceState.password) - 1);
  deviceState.password[sizeof(deviceState.password) - 1] = '\0'; // Ensure null-termination
  strncpy(deviceState.mqttServer, server.arg("mqttServer").c_str(), sizeof(deviceState.mqttServer) - 1);
  deviceState.mqttServer[sizeof(deviceState.mqttServer) - 1] = '\0'; // Ensure null-termination
  deviceState.mqttPort = server.arg("mqttPort").toInt();
}

void resetDeviceState() {
  deviceState = DeviceState(); // Reset device state
  EEPROM.put(0, deviceState);
  EEPROM.commit();
}

void initializeHttpServer() {
  Serial.println("Server - starting");
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html",
                "<form action='/save' method='post'><label>SSID: </label><input "
                "type='text' name='ssid' required><br><label>Password: </label><input "
                "type='password' name='password' required><br><label>MQTT Server: "
                "</label><input type='text' name='mqttServer' required><br><label>MQTT "
                "Port: </label><input type='number' name='mqttPort' "
                "required><br><button type='submit'>Save</button></form>");
  });

  server.on("/save", HTTP_POST, []() {
    populateDeviceState(deviceState, server);
    EEPROM.put(0, deviceState);
    EEPROM.commit();
    server.sendHeader("Location", String("/success"), true);
    server.send(303, "text/plain", "");
  });

  server.on("/success", HTTP_GET, []() {
    server.send(200, "text/html",
                "<h1>Success</h1><p>Device successfully initialized. Resetting "
                "in 3 seconds...</p>");
    delay(3000);
    resetFunc();
  });

  Serial.println("Server - begin");
  server.begin();
}

void initializePinThrottle() {
  pinMode(D0, INPUT);
  throttle.registerPinCallback(D0, DIGITAL, ON_CHANGE, 0,
                               [](int value) { MQTT_CLIENT.publish("interior/motion", value == HIGH ? "1" : "0"); });
}

bool connectWifiWithRetries(const char *ssid, const char *password, int retries, int delayMs = 500) {
  Serial.print("WiFi - Connecting ");

  WiFi.begin(ssid, password);

  int retryCount = 0;
  while (WiFi.status() != WL_CONNECTED && retryCount < retries) {
    delay(delayMs);
    Serial.print(".");
    retryCount++;
  }

  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

bool connectMqttWithRetries(const char *mqttServer, int mqttPort, const char *mqttClientName, int retries,
                            int delayMs = 1000) {
  Serial.print("MQTT - connecting");

  MQTT_CLIENT.setServer(mqttServer, mqttPort);
  MQTT_CLIENT.setClient(WIFI_CLIENT);

  int retryCount = 0;
  while (!MQTT_CLIENT.connected() && retryCount < retries) {
    delay(delayMs);
    MQTT_CLIENT.connect(mqttClientName);
    Serial.print(".");
    retryCount++;
  }

  Serial.println();
  return MQTT_CLIENT.connected();
}

void verifyConnection(DeviceState &deviceState) {
  if (WiFi.status() != WL_CONNECTED) {
    bool connected = connectWifiWithRetries(deviceState.ssid, deviceState.password, 40);
    if (!connected) {
      Serial.println("WiFi - Failed to connect. Returning to uninitialized mode.");
      resetDeviceState();
      resetFunc();
    }
  }
  if (!MQTT_CLIENT.connected()) {
    bool connected = connectMqttWithRetries(deviceState.mqttServer, deviceState.mqttPort, "esp32-motion-sensor", 40);
    if (!connected) {
      Serial.println("MQTT - Failed to connect. Returning to uninitialized mode.");
      resetDeviceState();
      resetFunc();
    }
  }
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(sizeof deviceState);
  EEPROM.get(0, deviceState);

  delay(1000);

  Serial.printf("Device State: \n SSID: %s\n  Password: %s\n  MQTT Server: %s\n  MQTT Port: %d\n", deviceState.ssid,
                deviceState.password, deviceState.mqttServer, deviceState.mqttPort);

  if (isDeviceInitialized(deviceState)) {
    Serial.println("WiFi - Station Mode");
    Serial.printf("WiFi - Network: %s\n", deviceState.ssid);
    verifyConnection(deviceState);
  } else {
    Serial.println("WiFi - AP Mode");
    WiFi.softAP("ESP32-AP", "toto-je-silne-heslo");
  }
  Serial.print("WiFi - IP Address: ");
  Serial.println(WiFi.localIP());
  initializeHttpServer();
  initializePinThrottle();
}

void loop() {
  server.handleClient();
  if (isDeviceInitialized(deviceState)) {
    verifyConnection(deviceState);
    throttle.processPins();
  }
}
