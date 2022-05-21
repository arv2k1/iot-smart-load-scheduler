#include <Arduino.h>

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include <PZEM004Tv30.h>
#include <HardwareSerial.h>

#define LED 2
#define RELAY_ONE 25
#define RELAY_TWO 27
#define DIP 26

#define RX1 14
#define TX1 12

#define RX2 16
#define TX2 17

String ESP_ID = "1";

PZEM004Tv30 pzemRegular(Serial1, RX1, TX1);
PZEM004Tv30 pzemScheduled(Serial2, RX2, TX2);

const char *ssid = "ASUS_X00TD";
const char *password = "9487774825@Hs";

String PROPERTIES_JSON_URL = "https://raw.githubusercontent.com/arv2k1/sls-properties/main/sls.json";


String domain;
int intervalBetweenRequests = 5000;

// ====== MODELS ======
struct MeterReading
{
  float voltage;
  float current;
  float frequency;
  float powerFactor;
  float power;
  float energy;
};

// ====== UTIL METHODS ======
void blinkLed(int count)
{
  for (int i = 0; i < count; i++)
  {
    digitalWrite(LED, HIGH);
    delay(100);
    digitalWrite(LED, LOW);
    delay(100);
  }
}

void connectToWifi()
{
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    blinkLed(6);
    delay(1000);
  }
  log("Wifi connected");
}

void log(String msg)
{
  if (domain && msg) {
    HTTPClient http;
    http.begin(domain + "/log");
    http.addHeader("Content-Type", "text/plain");
    http.POST("ESP_" + ESP_ID + " :: " + msg);
    http.end();
  }
}

// ====== SERVICE METHODS ======
void getPropertiesAndSetAsGlobalVariable()
{
  // === Send request to get properties ===
  HTTPClient http;
  http.begin(PROPERTIES_JSON_URL);
  int responseStatusCode = http.GET();
  log("Properties fetch response code = " + String(responseStatusCode));
  if (responseStatusCode <= 0)
  {
    delay(10000);
    blinkLed(10);
    getPropertiesAndSetAsGlobalVariable();
    return;
  }

  String propertiesJsonStr = http.getString();
  http.end();
  log(propertiesJsonStr);

  // === Parse properties json str as a json doc ===
  DynamicJsonDocument propertiesJson(300);
  deserializeJson(propertiesJson, propertiesJsonStr);

  // === Set as global variables ===
  const char *d = propertiesJson["domain"];
  domain = String(d);
  log("domain set => " + domain);

  const char *delayInterval = propertiesJson["post_meter_readings_interval"];
  int di = String(delayInterval).toInt();
  intervalBetweenRequests = di >= 100 ? di : 5000;
  log("Request delay interval set => " + String(intervalBetweenRequests));
}

void setRelayState(int loadNumber, int state)
{
  if (loadNumber < 1 || loadNumber > 2 || state < 0 || state > 1)
    return;
  int relayPin = loadNumber == 1 ? RELAY_ONE : RELAY_TWO;
  digitalWrite(relayPin, state);
  log("Pin " + relayPin + " set to => " + state);
}

void getRelayStatusAndSetState()
{
   // === Send request to get relay status ===
  HTTPClient http;
  http.begin(domain + "/" + ESP_ID + "/relay-status");
  int responseStatusCode = http.GET();
  log("Relay status fetch response code = " + String(responseStatusCode));
  if (responseStatusCode <= 0)
    return;
  String relayStatusJsonStr = http.getString();
  http.end();
  log(relayStatusJsonStr);

  // === Parse json ===
  DynamicJsonDocument relayStatusJson(300);
  deserializeJson(relayStatusJson, relayStatusJsonStr);

  // === Set relay state ===
  for(int i=0; i<2; i++) {
    setRelayState(i, relayStatusJson["relay-status"][i]);
  }
}

void readMeterReading(PZEM004Tv30 pzem, MeterReading meterReading)
{
  meterReading.voltage = pzem.voltage();
  meterReading.current = pzem.current();
  meterReading.frequency = pzem.frequency();
  meterReading.powerFactor = pzem.pf();
  meterReading.power = pzem.power();
  meterReading.energy = pzem.energy();
}

bool isValidReading(MeterReading meterReading)
{
  return
    !isnan(meterReading.voltage)     && 
    !isnan(meterReading.current)     && 
    !isnan(meterReading.frequency)   && 
    !isnan(meterReading.powerFactor) && 
    !isnan(meterReading.power)       && 
    !isnan(meterReading.energy); 
}

void setMeterReadingsInJson(String meterType, MeterReading mr, DynamicJsonDocument json)
{
  json[meterType]["voltage"] = mr.voltage;
  json[meterType]["current"] = mr.current;
  json[meterType]["frequency"] = mr.frequency;
  json[meterType]["pf"] = mr.powerFactor;
  json[meterType]["power"] = mr.power;
  json[meterType]["energy"] = mr.energy;
}

void readAndSendMeterReadings()
{
  // === Read meter readings and construct json
  MeterReading regularMeterReading;
  MeterReading scheduledMeterReading;

  readMeterReading(pzemRegular, regularMeterReading);
  readMeterReading(pzemScheduled, scheduledMeterReading);

  DynamicJsonDocument meterReadingsJson(1000);
  setMeterReadingsInJson("regular_meter", regularMeterReading, meterReadingsJson);
  setMeterReadingsInJson("scheduled_meter", scheduledMeterReading, meterReadingsJson);

  String meterReadingsJsonStr;
  serializeJson(meterReadingsJson, meterReadingsJsonStr);
  log(meterReadingsJsonStr);

  // === Post to server ===
  HTTPClient http;
  http.begin(domain + "/" + ESP_ID + "/meter-readings");
  http.addHeader("Content-Type", "application/json");
  http.POST(meterReadingsJsonStr);
  http.end();
}

// ====== SETUP AND LOOP METHODS =====
void setup() {
  pinMode(LED, OUTPUT);
  pinMode(RELAY_ONE, OUTPUT);
  pinMode(RELAY_TWO, OUTPUT);
  pinMode(DIP, INPUT);

  Serial.begin(115200);

  //  Serial1.begin(9600, SERIAL_8N1, TX1, RX1);
  //  Serial2.begin(9600, SERIAL_8N1, TX2, RX2);

  connectToWifi();
  
  getPropertiesAndSetAsGlobalVariable();
}

void loop() {

  log("Looping...");

  getRelayStatusAndSetState();
  
  readAndSendMeterReadings();
  
  blinkLed(3);
  delay(intervalBetweenRequests);
}
