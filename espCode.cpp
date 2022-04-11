#include <Arduino.h>

#include <WiFi.h>
#include <PZEM004Tv30.h>
#include <HTTPClient.h>

#include <HardwareSerial.h>

#define LED 2 // Builtin LED of ESP32
#define DEVICE_ONE 14
#define DEVICE_TWO 15

// Wifi Credentials
const char *ssid = "";
const char *password = "";

HardwareSerial hws1(1) ; // UART '1'
HardwareSerial hws2(2) ; // UART '2' 

PZEM004Tv30 pzemRegular(&hws1);
PZEM004Tv30 pzemScheduled(&hws2);

String baseUrl = "http://192.168.137.1:8080/iot-smart-load-scheduler/";
const int consumerId = 1;

void blinkLED(int count)
{
	for (int i = 0; i < count; i++)
	{
		digitalWrite(LED, HIGH);
		delay(100);
		digitalWrite(LED, LOW);
		delay(100);
	}
}

struct MeterReading
{
	float voltage;
	float current;
	float frequency;
	float powerFactor;
	float power;
	float energy;
};

MeterReading getMeterReading(PZEM004Tv30 pzem)
{
	MeterReading meterReading;

	meterReading.voltage = pzem.voltage();
	meterReading.current = pzem.current();
	meterReading.frequency = pzem.frequency();
	meterReading.powerFactor = pzem.pf();
	meterReading.power = pzem.power();
	meterReading.energy = pzem.energy();

	return meterReading;
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

String getMeterReadingAsJsonString(MeterReading meterReading)
{
	return 
	"{" +
		"\"voltage\":"   + meterReading.voltage     + "," +
		"\"current\":"   + meterReading.current     + "," +
		"\"frequency\":" + meterReading.frequency   + "," +
		"\"pf\":"        + meterReading.powerFactor + "," +
		"\"power\":"     + meterReading.power       + "," +
		"\"energy\":"    + meterReading.energy      + "," +
	"}";
}

void handleRelayOps(int pin, String status)
{
	bool on = status == "on";

	digitalWrite(pin, on ? LOW : HIGH);
}

void setup()
{
	pinMode(LED, OUTPUT);
	pinMode(DEVICE_ONE, OUTPUT);
	pinMode(DEVICE_TWO, OUTPUT);

	WiFi.begin(ssid, password);

	while (WiFi.status() != WL_CONNECTED)
	{
		blinkLED(6);
		delay(1000);
	}
}

void loop()
{
	// Ensure wifi connected
	if(WiFi.status() != WL_CONNECTED)
	{
		blinkLED(2) ;
		return ;
	}

	// === Construct Request Body ===
	MeterReading regularMeterReading = getMeterReading(pzemRegular);
	MeterReading scheduledMeterReading = getMeterReading(pzemScheduled);

	if(!isValidReading(regularMeterReading) || !isValidReading(scheduledMeterReading))
	{
		blinkLED(3);
		return;
	}

	String regularReadingJson = getMeterReadingAsJsonString(regularMeterReading);
	String scheduledReadingJson = getMeterReadingAsJsonString(scheduledMeterReading);

	String requestPayload = 
	"{" +
		"\"regular_meter_reading\""   + regularMeterReadingJson   + "," +
		"\"scheduled_meter_reading\"" + scheduledMeterReadingJson + "," +
	"}";

	// === Make request ===
	String url = baseUrl + "/consumers/" + consumerId;

	HTTPClient http; 

	http.begin(url);
	http.setHeader("Content-Type", "application/json");

	int httpResponseCode = http.POST(requestPayload);

	if(httpResponseCode != 200)
	{
		// Request failed
		blinkLED(4);
		return;
	}

	String responseJsonString = http.getString() ;

	http.end() ;

	// === Parse response ===
	responseJsonString.replace("\"", "");
	responseJsonString = responseJsonString.substring(1, responseJsonString.length()-1);

	int start = 0, end = -1;
	while((end = responseJsonString.indexOf(',', start)) != -1)
	{
		String line = responseJsonString.substring(start, end);

		int colonIx = line.indexOf(':');
		
		int pin = line.substring(0, colonIx).toInt();
		String status = line.substring(colonIx + 1);

		handleRelayOps(pin, status);

		start = end + 1;
	}


	blinkLED(1) ;
	delay(4000) ;
}
