#include <ESP8266mDNS.h>
#include <FS.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <ESP8266HTTPUpdateServer.h>

#include "gamma.h"
#include "hsb.h"

#define DEBUG

#define DEVBOARD
//#define RESET_SPIFFS

const char *VERSION = "1.0.0";

// pin configurations
const unsigned int PIN_RGB_RED = 15;
const unsigned int PIN_RGB_GREEN = 13;
const unsigned int PIN_RGB_BLUE = 12;
const unsigned int PIN_WHITE1 = 14;
const unsigned int PIN_WHITE2 = 4;

const unsigned int PIN_LED_GREEN = 1;
const unsigned int PIN_LED_RED = 5;

// username and password for web ota update
const char *username = "admin";
const char *password = "h801update";


WiFiManager wifiManager;
WiFiClient wifiClient;
PubSubClient client(wifiClient);
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

extern const unsigned int PROGMEM gamma16[];
char mqttServer[60];
char mqttPort[6] = "1883";
bool shouldSaveConfig = false;

String chipId;
String nodeName;
String baseTopic;
String dnsName;

const char *topicWhite1 = "white1";
const char *topicWhite2 = "white2";
const char *topicHsb = "hsb";
const char *topicSpeed = "speed";
const char *topicStatus = "status";
const char *topicStatusHsb = "status/hsb";
const char *topicStatusWhite1 = "status/white1";
const char *topicStatusWhite2 = "status/white2";
const char *topicStatusSpeed = "status/speed";

unsigned long lastRunColors = 0L;

Hsb hsb;
unsigned int currentRed = 0;
unsigned int currentGreen = 0;
unsigned int currentBlue = 0;
unsigned int currentWhite1 = 0;
unsigned int currentWhite2 = 0;

unsigned int targetRed = 0;
unsigned int targetGreen = 0;
unsigned int targetBlue = 0;
unsigned int targetWhite1 = 0;
unsigned int targetWhite2 = 0;
unsigned int rawTargetWhite1 = 0;
unsigned int rawTargetWhite2 = 0;

// speed = 0 - 100: 0 -> 50000µs delay, 100 -> 100µs delay
unsigned long delayValue = 10000;

char *serverIndex = "<style>body { font-family: Arial; } a { color: blue }</style><h1>h801 webupdate</h1>current version: %VERSION%<br />chipid: %CHIPID%<br /><br /><a href='/update'>web update</a>";

void saveConfigCallback () {
#ifdef DEBUG
	Serial.println(F("save config callback"));
#endif
	shouldSaveConfig = true;
}

void setup() {
#ifdef DEBUG
#ifndef DEVBOARD
	Serial.set_tx(2);	
#endif
	Serial.begin(115200);
	Serial.println("");
	Serial.println(F("booting"));
#endif

	chipId = String(ESP.getChipId());
	nodeName = "h801-" + chipId;
	baseTopic = "/h801/" + chipId + "/";
	dnsName = "h801ota-" + chipId;

#ifdef DEBUG
	Serial.println(F("setting up ports"));
#endif
	pinMode(PIN_RGB_RED, OUTPUT);
	pinMode(PIN_RGB_GREEN, OUTPUT);
	pinMode(PIN_RGB_BLUE, OUTPUT);
	pinMode(PIN_WHITE1, OUTPUT);
	pinMode(PIN_WHITE2, OUTPUT);
#ifndef DEVBOARD
	pinMode(PIN_LED_GREEN, OUTPUT);
#endif
	pinMode(PIN_LED_RED, OUTPUT);

	analogWrite(PIN_RGB_RED, 0);
	analogWrite(PIN_RGB_GREEN, 0);
	analogWrite(PIN_RGB_BLUE, 0);
	analogWrite(PIN_WHITE1, 0);
	analogWrite(PIN_WHITE2, 0);

	analogWrite(PIN_RGB_RED, 1023);
	delay(200);
	analogWrite(PIN_RGB_RED, 0);
	analogWrite(PIN_RGB_GREEN, 1023);
	delay(200);
	analogWrite(PIN_RGB_GREEN, 0);
	analogWrite(PIN_RGB_BLUE, 1023);
	delay(200);
	analogWrite(PIN_RGB_BLUE, 0);
	analogWrite(PIN_WHITE1, 1023);
	delay(200);
	analogWrite(PIN_WHITE1, 0);
	analogWrite(PIN_WHITE2, 1023);
	delay(200);
	analogWrite(PIN_WHITE2, 0);

#ifdef DEBUG
	Serial.println(F("start spiffs"));
#endif

	// only needed once during initial setup
#ifdef RESET_SPIFFS
#ifdef DEBUG
 	Serial.println(F("format spiffs"));
#endif
	SPIFFS.format();
	wifiManager.resetSettings();
#endif

	if (SPIFFS.begin()) {
		if (SPIFFS.exists("/config.json")) {
#ifdef DEBUG
			Serial.println(F("config file found"));
#endif
			File configFile = SPIFFS.open("/config.json", "r");
			if (configFile) {
#ifdef DEBUG
				Serial.println(F("opened config file"));
#endif
				size_t size = configFile.size();
				// Allocate a buffer to store contents of the file.
				std::unique_ptr<char[]> buf(new char[size]);

				configFile.readBytes(buf.get(), size);
				DynamicJsonBuffer jsonBuffer;
				JsonObject& json = jsonBuffer.parseObject(buf.get());
#ifdef DEBUG
				json.printTo(Serial);
				Serial.println("");
#endif
				if (json.success()) {
#ifdef DEBUG
					Serial.println(F("parsed json"));
#endif
					strncpy(mqttServer, json["mqttServer"], sizeof(mqttServer));
					strncpy(mqttPort, json["mqttPort"], sizeof(mqttPort));
				} else {
					wifiManager.resetSettings();
#ifdef DEBUG
					Serial.println(F("failed to load json config"));
#endif
				}
				configFile.close();
			} else {
				wifiManager.resetSettings();
#ifdef DEBUG
				Serial.println(F("cannot open config file"));
#endif
			}
		} else {
			wifiManager.resetSettings();
#ifdef DEBUG
			Serial.println(F("config file not found"));
#endif
		}
	} else {
#ifdef DEBUG
		Serial.println(F("failed to start spiffs"));
		for(;;) {}
#endif
	}

#ifdef DEBUG
	Serial.println(F("setup wifi"));
#endif

	WiFiManagerParameter customMqttServer("server", "mqtt server", mqttServer, 60);
	wifiManager.addParameter(&customMqttServer);
	WiFiManagerParameter customMqttPort("port", "mqtt port", mqttPort, 6);
	wifiManager.addParameter(&customMqttPort);

	wifiManager.setTimeout(120);
	wifiManager.setSaveConfigCallback(saveConfigCallback);

	if (!wifiManager.autoConnect("h801setup", "h801login")) {
#ifdef DEBUG
		Serial.println(F("failed to connect and hit timeout"));
#endif
		delay(3000);
		ESP.reset();
		delay(5000);
	}

#ifdef DEBUG
	Serial.println(F("connected to wifi"));
#endif

	strncpy(mqttServer, customMqttServer.getValue(), sizeof(mqttServer));
	strncpy(mqttPort, customMqttPort.getValue(), sizeof(mqttPort));

	if (shouldSaveConfig) {
#ifdef DEBUG
		Serial.println(F("saving config"));
#endif
		DynamicJsonBuffer jsonBuffer;
		JsonObject& json = jsonBuffer.createObject();
		json["mqttServer"] = mqttServer;
		json["mqttPort"] = mqttPort;

		File configFile = SPIFFS.open("/config.json", "w");
		if (!configFile) {
#ifdef DEBUG
			Serial.println(F("failed to open config file for writing"));
#endif
		} else {
#ifdef DEBUG
			json.printTo(Serial);
			Serial.println("");
#endif
			json.printTo(configFile);
			configFile.close();
		}
	}

#ifdef DEBUG
	Serial.printf("local ip: %s\n", WiFi.localIP().toString().c_str());
	Serial.printf("mqtt connection: %s:%s\n", mqttServer, mqttPort);
#endif

#ifdef DEBUG
	Serial.println(F("start update server"));
#endif
	if (password != "") {
		MDNS.begin(dnsName.c_str());
		httpUpdater.setup(&httpServer, username, password);
		httpServer.begin();
		MDNS.addService("http", "tcp", 80);
#ifdef DEBUG
	} else {
		Serial.println(F("no password given, disabling ota"));
#endif
	}

	httpServer.on("/", HTTP_GET, [&]() {
        httpServer.sendHeader("Connection", "close");
        String temp(serverIndex);
        temp.replace("%VERSION%", VERSION);
        temp.replace("%CHIPID%", chipId);
        httpServer.send(200, "text/html", temp);
    });

	client.setServer(mqttServer, String(mqttPort).toInt());
  	client.setCallback(mqttCallback);

#ifdef DEBUG
	Serial.println(F("entering main loop"));
#endif

}

void setColor(unsigned int r, unsigned int g, unsigned int b) {
#ifdef DEBUG
	Serial.printf("setting color to %d %d %d\n", r, g, b);
#endif	
	targetRed = constrain(r, 0, 1023);
	targetGreen = constrain(g, 0, 1023);
	targetBlue = constrain(b, 0, 1023);
	publishTopic(topicStatusHsb, hsb.toString());
}

void setWhite1(unsigned int w) {
#ifdef DEBUG
	Serial.printf("setting white1 to %d\n", w);
#endif	
	targetWhite1 = constrain(w, 0, 1023);
	publishTopic(topicStatusWhite1, String(rawTargetWhite1));
}

void setWhite2(unsigned int w) {
#ifdef DEBUG
	Serial.printf("setting white2 to %d\n", w);
#endif	
	targetWhite2 = constrain(w, 0, 1023);
	publishTopic(topicStatusWhite2, String(rawTargetWhite2));
}

void setSpeed(unsigned int speed) {
	delayValue = -499 * speed + 50000;
#ifdef DEBUG
	Serial.printf("delay value: %d\n", delayValue);
#endif
	publishTopic(topicStatusSpeed, String(speed));
}

void mqttCallback(char* topicRaw, byte* payloadRaw, unsigned int length) {
	// this is much faster than converting to String and using startswith
	// first test if the message is for this device
	if (strncmp(topicRaw, baseTopic.c_str(), baseTopic.length()) == 0) {
		// is our topic
#ifdef DEBUG
		Serial.printf("received topic: %s\n", topicRaw);
#endif
		// if one of our topics triggered convert to string for easier processing
		String topic(topicRaw);
		topic = topic.substring(baseTopic.length(), topic.length());
#ifdef DEBUG
		Serial.printf("extracted topic: %s\n", topic.c_str());
#endif
		char payload[length + 1];
		memset(payload, '\0', length + 1);
		memcpy(payload, payloadRaw, length);
		String payloadString(payload);
		payloadString.trim();
#ifdef DEBUG
		Serial.printf("payload is: %s\n", payload);
#endif
		if (topic == topicWhite1 || topic == topicWhite2) {
			unsigned int value = constrain(payloadString.toInt(), 0, 100);
			if (topic == topicWhite1) {
				rawTargetWhite1 = value;
				setWhite1(value * 10.23);
			} else if (topic == topicWhite2) {
				rawTargetWhite2 = value;
				setWhite2(value * 10.23);
			}
		} else if (topic == topicSpeed) {
			unsigned int speed = constrain(payloadString.toInt(), 0, 100);
			setSpeed(speed);
		} else if (topic == topicHsb) {
			unsigned int hue = split(payloadString, ',', 0).toInt();
			unsigned int saturation = split(payloadString, ',', 1).toInt();
			unsigned int brightness = split(payloadString, ',', 2).toInt();
#ifdef DEBUG
			Serial.printf("hsb: %d %d %d\n", hue, saturation, brightness);
#endif
			hsb.setHue(hue);
			hsb.setSaturation(saturation);
			hsb.setBrightness(brightness);
			unsigned int _red = 0;
			unsigned int _green = 0;
			unsigned int _blue = 0;
			if (hsb.toRgb(&_red, &_green, &_blue, 1023)) {
				setColor(_red, _green, _blue);
#ifdef DEBUG
			} else {
				Serial.println(F("cannot convert to rgb"));
#endif
			}
		}

	}
}

String split(String s, char delimiter, int index) {
	int count = 0;
	int fromIndex = 0;
	int toIndex = -1;
	while (true) {
		fromIndex = toIndex + 1;
		toIndex = s.indexOf(delimiter, fromIndex);
		if (count == index) {
			if (toIndex == -1) {
				// have reached our index and not found a next delimiter
				// this means the end of the string is reached
				toIndex = s.length();
			}
			return s.substring(fromIndex, toIndex);
		}
		if (toIndex == -1) {
			// char not found
			break;
		} else {
			count++;
		}
	}
	return "";
}

void subscribeTopic(String topic) {
	String temp = baseTopic + topic;
#ifdef DEBUG
	Serial.printf("subscribing to topic: %s\n", temp.c_str());
#endif
	client.subscribe(temp.c_str());
}

void publishTopic(String topic, String value) {
	String temp = baseTopic + topic;
	client.publish(temp.c_str(), value.c_str());
}

void reconnectMqtt() {
	while (!client.connected()) {
#ifdef DEBUG
		Serial.println(F("trying to connect to mqtt broker"));
#endif
		String heartbeatTopic = baseTopic + topicStatus;
		if (client.connect(nodeName.c_str(), heartbeatTopic.c_str(), 1, true, "offline")) {
#ifdef DEBUG
			Serial.println(F("mqtt connected"));
#endif
#ifndef DEVBOARD
			for (unsigned int i = 0; i < 10; i++){
				delay(100);
				digitalWrite(PIN_LED_GREEN, 0);
				delay(100);
				digitalWrite(PIN_LED_GREEN, 1);
			}
#endif

			publishTopic(topicStatus, "online");

			setColor(0, 0, 0);
			setWhite1(0);
			setWhite2(0);
			setSpeed(80);

			subscribeTopic(topicWhite1);
			subscribeTopic(topicWhite2);
			subscribeTopic(topicHsb);
			subscribeTopic(topicSpeed);
			
		} else {
#ifdef DEBUG
			Serial.printf("failed to connect to mqtt broker %s: retCode=%d, retrying in 5 seconds\n", mqttServer, client.state());
#endif
			for (unsigned int i = 0; i < 5; i++) {
				delay(500);
				digitalWrite(PIN_LED_RED, LOW);
				delay(500);
				digitalWrite(PIN_LED_RED, HIGH);
			}
		}
	}
}

bool processColor(unsigned int target, unsigned int *current) {
	bool update = false;
	if (*current != target) {
		update = true;
		if (*current < target) {
			(*current)++;
		} else if (*current > target) {
			(*current)--;
		}
	}
	return update;
}

void loop() {
	
	httpServer.handleClient();
	
	if (!client.connected()) {
#ifdef DEBUG
		Serial.println(F("lost connection to mqtt broker, try to reconnect"));
		if (!WiFi.isConnected()) {
			Serial.println(F("wifi is not connected"));
		} else {
			Serial.println(F("WiFi is connected"));
		}
#endif
		reconnectMqtt();
	}
	client.loop();

	if ((micros() - lastRunColors) > delayValue) {
		lastRunColors = micros();

		if (processColor(targetRed, &currentRed) ||
			processColor(targetGreen, &currentGreen) || 
			processColor(targetBlue, &currentBlue) || 
			processColor(targetWhite1, &currentWhite1) || 
			processColor(targetWhite2, &currentWhite2)) {
// #ifdef DEBUG
// 			Serial.printf("updating color outputs to %d %d %d %d %d\n", red, green, blue, white1, white2);
// #endif
			analogWrite(PIN_RGB_RED, pgm_read_word(&gamma16[currentRed]));
			analogWrite(PIN_RGB_GREEN, pgm_read_word(&gamma16[currentGreen]));
			analogWrite(PIN_RGB_BLUE, pgm_read_word(&gamma16[currentBlue]));
			analogWrite(PIN_WHITE1, pgm_read_word(&gamma16[currentWhite1]));
			analogWrite(PIN_WHITE2, pgm_read_word(&gamma16[currentWhite2]));
		}
	}

}