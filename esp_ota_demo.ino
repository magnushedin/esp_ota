

/* OTA and knoleary MQTT combined
 *  OTA via Arduino IDE.
 */


#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>


// I/O Pins used
#define PUMP        2  // GPIO2 (available)
#define NOT_MEASURE 0  // Shared with DTR
#define RESULT      3  // Shared with RxD - remember to disconnect RxD
// TxD used for one way logging/debugging


// Update these with values suitable for your network.
const char* ssid     = "Hedin";
const char* password = "porscheboxster";
const char* OTApass  = "ota_secret";

const char* mqtt_server = "192.168.68.59";
const char* mqttuser    = "mqtt_user";
const char* mqttpass    = "mqtt_pass";

const char* application = "esp_ota_demo";
const char* version     = "0.0.1";

int led_status = LOW;


// MQTT variables
WiFiClient espClient;
PubSubClient client(espClient);


// Application variables and defines
#define give_water           1
#define measure_conductivity 2
#define set_clock            4
#define intro                8
int actions = set_clock | intro;
int pump_time = 60; // sec
int count_samples = 0;
int count_wet     = 0;
//Clock24h next_event; // Set to now + 30 min when data is measured and/or flower watered. 


// Temporary string
char msg[50];

// Readable string equality test
#define IsEqual(a,b) (strcmp(a, b) == 0)


// ------------------------------------------------------------
void setup() 
{
  Serial.begin(115200);
  setup_application(); // ASAP to stop pump
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  setup_ota();
  Serial.print("IP address: "); Serial.println(WiFi.localIP());
}


// ------------------------------------------------------------
void setup_wifi() 
{
  pinMode(LED_BUILTIN, OUTPUT);
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to "); Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}


// ------------------------------------------------------------
void setup_ota()
{
  // OTA authentication
  ArduinoOTA.setPassword(OTApass);

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    client.disconnect();
    digitalWrite(PUMP, LOW); // Pump off
    digitalWrite(NOT_MEASURE, HIGH); // Sensor power off
    Serial.println("Start updating " + type);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  
  ArduinoOTA.begin();
  Serial.println("OTA Ready");
}


// ------------------------------------------------------------
void setup_application()
{
  pinMode(PUMP, OUTPUT);
  pinMode(NOT_MEASURE, OUTPUT);
  pinMode(RESULT, INPUT);
  digitalWrite(PUMP, LOW); // Pump off
  digitalWrite(NOT_MEASURE, HIGH); // Sensor power off
}



// ------------------------------------------------------------
void callback(char* topic, byte* payload, unsigned int length) 
{
  for (int i=0; i<length; i++) {
    msg[i] = (char)payload[0];
  }
  led_status = !led_status;
}


// ------------------------------------------------------------
void reconnect() 
{
  // Loop until we're reconnected
  while ( ! client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(application, mqttuser, mqttpass)) {
      Serial.println("connected");
      snprintf (msg, 50, "dev/esp8266-%06x/app", ESP.getChipId());
      client.publish(msg, application);
      snprintf (msg, 50, "dev/esp8266-%06x/ver", ESP.getChipId());
      client.publish(msg, version);
      client.subscribe("led/#");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}



// ------------------------------------------------------------
void loop() 
{
  if (!client.connected()) {
    reconnect();
  }
  
  client.loop();

  ArduinoOTA.handle();

  client.publish("dev/null", led_status ? "led OFF" : "led ON");
  digitalWrite(LED_BUILTIN, led_status);
  delay(1000);
  
  if (actions & give_water) {
    actions &= ~ give_water;
    Serial.println("Water the flower");

    // Optimize the watering time
    if (count_samples == 48) {
      if (count_wet >= 4) {
        pump_time = 9 * pump_time / 10;
      }
      else {
        pump_time = 11 * pump_time / 10;
      }
    }

    // Check it isn't all wet and then do the pumping
    if (count_wet == count_samples) {
      client.publish("Flower/pump_sec", "0");
      Serial.println("All wet -> skip watering");
    }
    else {
      snprintf (msg, 10, "%ld", pump_time);
      client.publish("Flower/pump_sec", msg);
      digitalWrite(PUMP, 1);
      delay (pump_time * 1000);
      digitalWrite(PUMP, 0);
    }

    // Reset counters for next day
    count_samples = 0;
    count_wet     = 0;
  }
  
  if (actions & measure_conductivity) {
    actions &= ~ measure_conductivity;
    Serial.print("Measure conductivity: ");
    digitalWrite(NOT_MEASURE, 0);
    delay (100);
    int is_dry = digitalRead(RESULT);
    digitalWrite(NOT_MEASURE, 1);
    Serial.println(is_dry ? "dry" : "wet");
    client.publish("Flower/state", is_dry ? "dry" : "wet");
    count_samples += 1;
    count_wet     += ! is_dry;
  }

  if (actions & intro) {
    actions &= ~ intro;
    Serial.print("Power on test...");
    digitalWrite(NOT_MEASURE, 0);
    digitalWrite(PUMP, 1);
    delay(100);
    digitalWrite(PUMP, 0);
    digitalWrite(NOT_MEASURE, 1);
    Serial.println(" done.");
 }
}
