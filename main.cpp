#include <Arduino.h>

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include <PZEM004Tv30.h>
#include <HardwareSerial.h>

#define LED 2
#define RELAY_ONE 25
#define RELAY_TWO 27

#define RX1 14
#define TX1 12

#define RX2 16
#define TX2 17

#define LOG_DEBUG 0
#define LOG_INFO 1
#define LOG_ERROR 2

String ESP_ID = "1";

PZEM004Tv30 pzemRegular(Serial1, RX1, TX1);
PZEM004Tv30 pzemScheduled(Serial2, RX2, TX2);

const char *ssid = "ASUS_X00TD";
const char *password = "9487774825@Hs";

String PROPERTIES_JSON_URL = "https://raw.githubusercontent.com/arv2k1/sls-properties/main/sls.json";


String domain;
int intervalBetweenRequests = 5000;
int logLevel = 1;

// ====== MODELS ======
typedef struct
{
  float voltage;
  float current;
  float frequency;
  float powerFactor;
  float power;
  float energy;
} MeterReading ;

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

void log(int level, String msg)
{
  if (domain && msg && level >= logLevel) {
    HTTPClient http;
    http.begin(domain + "/log");
    http.addHeader("Content-Type", "text/plain");
    http.POST("ESP_" + ESP_ID + " :: " + msg);
    http.end();
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
  delay(5000);
  log(LOG_INFO, "Wifi connected");
}


// ====== SERVICE METHODS ======
void getPropertiesAndSetAsGlobalVariable()
{
  // === Send request to get properties ===
  HTTPClient http;
  http.begin(PROPERTIES_JSON_URL);
  int responseStatusCode = http.GET();
  log(LOG_DEBUG, "Properties fetch response code = " + String(responseStatusCode));
  if (responseStatusCode <= 0)
  {
    delay(10000);
    blinkLed(10);
    getPropertiesAndSetAsGlobalVariable();
    return;
  }

  String propertiesJsonStr = http.getString();
  http.end();
  log(LOG_DEBUG, propertiesJsonStr);

  // === Parse properties json str as a json doc ===
  DynamicJsonDocument propertiesJson(300);
  deserializeJson(propertiesJson, propertiesJsonStr);

  // === Set as global variables ===
  const char *d = propertiesJson["domain"];
  domain = String(d);
  log(LOG_INFO, "domain set => " + domain);

  const char *delayInterval = propertiesJson["post_meter_readings_interval"];
  int di = String(delayInterval).toInt();
  intervalBetweenRequests = di >= 100 ? di : 5000;
  log(LOG_INFO, "Request delay interval set => " + String(intervalBetweenRequests));
}

void setRelayState(int loadNumber, int state)
{
  if (loadNumber < 1 || loadNumber > 2 || state < 0 || state > 1)
  {
    log(LOG_ERROR, "Invalid load number or state, aborting setRelayState, Loadnumber => " + String(loadNumber) + ", state => " + String(state));
    return;
  }
  int relayPin = loadNumber == 1 ? RELAY_ONE : RELAY_TWO;
  digitalWrite(relayPin, state);
  log(LOG_DEBUG, "Pin " + String(relayPin) + " set to => " + String(state));
}

void getRelayStatusAndSetState()
{
   // === Send request to get relay status ===
  HTTPClient http;
  http.begin(domain + "/" + ESP_ID + "/relay-status");
  int responseStatusCode = http.GET();
  log(LOG_DEBUG, "Relay status fetch response code = " + String(responseStatusCode));
  if (responseStatusCode <= 0)
    return;
  String relayStatusJsonStr = http.getString();
  http.end();
  log(LOG_DEBUG, relayStatusJsonStr);

  // === Parse json ===
  DynamicJsonDocument relayStatusJson(300);
  deserializeJson(relayStatusJson, relayStatusJsonStr);

  // === Set relay state ===
  for(int i=1; i<=2; i++) {
    int curRelayState = relayStatusJson["relay-status"][i-1];
    log(LOG_DEBUG, "Setting relay state for load => " + String(i) + ", state => " + String(curRelayState));
    setRelayState(i, curRelayState);
  }
}

bool isValidReading(MeterReading meterReading)
{
  log(LOG_INFO, "Inside validate method");
  log(LOG_INFO, String(meterReading.voltage));
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
  MeterReading regularMeterReading = {
    pzemRegular.voltage(),
    pzemRegular.current(),
    pzemRegular.frequency(),
    pzemRegular.pf(),
    pzemRegular.power(),
    pzemRegular.energy()
  };

  MeterReading scheduledMeterReading = {
    pzemScheduled.voltage(),
    pzemScheduled.current(),
    pzemScheduled.frequency(),
    pzemScheduled.pf(),
    pzemScheduled.power(),
    pzemScheduled.energy()
  };

  if(!isValidReading(regularMeterReading) || !isValidReading(scheduledMeterReading))
  {
    log(LOG_INFO, "Invalid meter reading, aborting POST");
    return;
  }

  DynamicJsonDocument meterReadingsJson(1000);

  meterReadingsJson["regular_meter"]["voltage"] = regularMeterReading.voltage;
  meterReadingsJson["regular_meter"]["current"] = regularMeterReading.current;
  meterReadingsJson["regular_meter"]["frequency"] = regularMeterReading.frequency;
  meterReadingsJson["regular_meter"]["pf"] = regularMeterReading.powerFactor;
  meterReadingsJson["regular_meter"]["power"] = regularMeterReading.power;
  meterReadingsJson["regular_meter"]["energy"] = regularMeterReading.energy;

  meterReadingsJson["scheduled_meter"]["voltage"] = scheduledMeterReading.voltage;
  meterReadingsJson["scheduled_meter"]["current"] = scheduledMeterReading.current;
  meterReadingsJson["scheduled_meter"]["frequency"] = scheduledMeterReading.frequency;
  meterReadingsJson["scheduled_meter"]["pf"] = scheduledMeterReading.powerFactor;
  meterReadingsJson["scheduled_meter"]["power"] = scheduledMeterReading.power;
  meterReadingsJson["scheduled_meter"]["energy"] = scheduledMeterReading.energy;

  String meterReadingsJsonStr;
  serializeJson(meterReadingsJson, meterReadingsJsonStr);
  log(LOG_INFO, "Serialized json to str");
  log(LOG_INFO, meterReadingsJsonStr);

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

  Serial.begin(115200);

  //  Serial1.begin(9600, SERIAL_8N1, TX1, RX1);
  //  Serial2.begin(9600, SERIAL_8N1, TX2, RX2);

  connectToWifi();
  
  getPropertiesAndSetAsGlobalVariable();
}

void loop() {

  log(LOG_INFO, "Looping...");

  getRelayStatusAndSetState();
  
  readAndSendMeterReadings();
  
  blinkLed(3);
  delay(intervalBetweenRequests);
}
