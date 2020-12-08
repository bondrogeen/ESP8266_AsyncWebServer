#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFSEditor.h>
#include <EEPROM.h>
#include "RTClib.h"
#include <rdm6300.h>
#include <Wire.h>
#include "../lib/eepromi2c.h"
#include "../lib/struct.h"
#include "AsyncJson.h"
#include "ArduinoJson.h"

#define DEF_EEPROM_ADDRESS 0x57
#define DEF_EEPROM_WRITE_TIME 30
#define DEF_EEPROM_SIZE 4096

#define RDM6300_RX_PIN 13 // can be only 13 - on esp8266 force hardware uart!
#define READ_LED_PIN 15
#define PIN_STATE 12
#define CONFIG_VERSION "lk9"
#define CONFIG_START 32
#define INDEX_START_ADRESS 0

#define DEF_WIFI_MODE WIFI_AP_STA
#define DEF_WIFI_SSID "sfinks_72"
#define DEF_WIFI_PASS "ub,bcrec"
#define DEF_SERVER_URL "192.168.1.37"
#define DEF_SERVER_PORT 3000
#define DEF_HTTP_MODE 1
#define DEF_HTTP_LOGIN "admin"
#define DEF_HTTP_PASS "admin"
#define DEF_DEVICE_LOCATION "brig1"

#define DEF_DEVICE_FIRMWARE "0.0.6"

Rdm6300 rdm6300;
WiFiClient WiFIclient;
RTC_DS3231 rtc;

uint32_t espID; 

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncEventSource events("/events");
AsyncWebSocketClient * client;

FSInfo fs_info;

uint8_t isSend = 1;
uint8_t reset_counter = 1;
uint16_t start_adress = 0;
uint32_t last_time = 0;
char device_firmware[6] = "0.0.6";
bool isWsConnected = false;
bool isLoad = false;
bool isErase = false;
bool shouldReboot = false;

struct Data {
  uint32_t card;
  uint32_t time;
  uint32_t fund;
  uint8_t type;
  uint8_t send;
  uint8_t next;
  uint8_t check;
};

Data cardTemp;

struct StoreStruct {
  char version[4];
  char server_url[21];
  uint16_t server_port;
  uint8_t wifi_mode;
  char wifi_ssid[21];
  char wifi_pass[21];
  uint8_t http_mode;
  char http_login[11];
  char http_pass[11];
  char device_location[11];
} storage = {
  CONFIG_VERSION,
  DEF_SERVER_URL,
  DEF_SERVER_PORT,
  DEF_WIFI_MODE,
  DEF_WIFI_SSID,
  DEF_WIFI_PASS,
  DEF_HTTP_MODE,
  DEF_HTTP_LOGIN,
  DEF_HTTP_PASS,
  DEF_DEVICE_LOCATION
};

void writeIntIntoEEPROM(uint8_t address, uint16_t number) { 
  uint8_t byte1 = number >> 8;
  uint8_t byte2 = number & 0xFF;
  EEPROM.write(address, byte1);
  EEPROM.write(address + 1, byte2);
  EEPROM.commit();
}

int readIntFromEEPROM(int address) {
  uint16_t value = 0;
  uint8_t byte1 = EEPROM.read(address);
  uint8_t byte2 = EEPROM.read(address + 1);
  value = (byte1 << 8) + byte2;
  if (value % 16 != 0 || value > 4080 || value < 0) value = 0;
  return value;
}

void saveConfig() {
  for (unsigned int t=0; t<sizeof(storage); t++) {
    EEPROM.write(CONFIG_START + t, *((char*)&storage + t));
  }
    Serial1.print("Save config");
    EEPROM.commit();
}

void loadConfig() {
  if (EEPROM.read(CONFIG_START + 0) == CONFIG_VERSION[0] &&
      EEPROM.read(CONFIG_START + 1) == CONFIG_VERSION[1] &&
      EEPROM.read(CONFIG_START + 2) == CONFIG_VERSION[2]) {
        for (unsigned int t=0; t<sizeof(storage); t++) *((char*)&storage + t) = EEPROM.read(CONFIG_START + t);
    Serial1.print("Load config");
  } else {
    saveConfig();
  } 
}

uint8_t crc(uint32_t value) {
    uint8_t sum = 0;
    unsigned char *p = (unsigned char *)&value;
    for (int i=0; i<sizeof(value); i++) {
        sum += p[i];
    }
    return sum;
}

void eraseEeprom() {
  const uint8_t j = 8;
  uint8_t empty[j] = {0,0,0,0,0,0,0,0};
  uint16 adress = 0;
  for (size_t i = 0; i < (DEF_EEPROM_SIZE / j); i++) {
    eeWrite(adress, empty);
    adress += j;
    delay(DEF_EEPROM_WRITE_TIME);
  }
  writeIntIntoEEPROM(INDEX_START_ADRESS, 0);
  isErase = false;
}

void printCard(Data str) {
  Serial1.print("card: ");
  Serial1.println(str.card);
  Serial1.print("time: ");
  Serial1.println(str.time);
  Serial1.print("type: ");
  Serial1.println(str.type);
  Serial1.print("check: ");
  Serial1.println(str.check);
  Serial1.print("send: ");
  Serial1.println(str.send);
  Serial1.println();
}

void load() {
  uint8_t j = 32;
  uint8_t buffer[32];
  uint16 adress = 0;
  for (size_t i = 0; i < (DEF_EEPROM_SIZE / j); i++) {
    eeRead(adress, buffer);
    adress += j;
    ws.binaryAll(buffer, sizeof(buffer));
    delay(50);
  }
  isLoad = false;
}

// void findStruct(uint16_t byte) {
//   Data test;
//   uint16_t adr = 0;
//   uint8_t checksum = 0;
//   for (size_t i = 0; i < byte; i++) {
//     eeRead(adr, test);
//     adr = adr + sizeof(Data);
//     checksum = crc(test.card);
//     // if(checksum != test.any[0]) break;
//     // if (isWsConnected) {
//     //   ws.printfAll(test.card_id, HEX);
//     // }
//     printCard(test);
//   }
//   Serial1.println("Search is over");
// }

void findDevice() {
  uint8_t device, error, address;
  device = 0;
  for(address = 1; address < 127; address++ ) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
     if (error == 0) {
      Serial1.print("I2C device found at address 0x");
      if (address<16) Serial.print("0");
      Serial1.print(address,HEX);
      Serial1.println("  !"); 
      device++;
    } else if (error==4) {
      Serial1.print("Unknown error at address 0x");
      if (address<16) Serial1.print("0");
      Serial1.println(address,HEX);
    }    
  }
  if (device == 0) Serial1.println("No I2C devices found\n");
  else Serial1.println("done\n"); 
}

String macToString(const unsigned char* mac) {
  char buf[20];
  snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
  if(type == WS_EVT_CONNECT){
    Serial1.printf("ws[%s][%u] connect\n", server->url(), client->id());
    client->printf("Hello Client %u :)", client->id());
    client->ping();
    isWsConnected = true;
    isLoad = true;    
  } else if(type == WS_EVT_DISCONNECT){
    Serial1.printf("ws[%s][%u] disconnect\n", server->url(), client->id());
    isWsConnected = false;
  } else if(type == WS_EVT_ERROR){
    Serial1.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
  } else if(type == WS_EVT_PONG){
    Serial1.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len)?(char*)data:"");
  } else if(type == WS_EVT_DATA){
    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    String msg = "";
    if(info->final && info->index == 0 && info->len == len){
      //the whole message is in a single frame and we got all of it's data
      Serial1.printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT)?"text":"binary", info->len);

      if(info->opcode == WS_TEXT){
        for(size_t i=0; i < info->len; i++) {
          msg += (char) data[i];
        }
      } else {
        char buff[3];
        for(size_t i=0; i < info->len; i++) {
          sprintf(buff, "%02x ", (uint8_t) data[i]);
          msg += buff ;
        }
      }
      Serial1.printf("%s\n",msg.c_str());

      if(info->opcode == WS_TEXT)
        client->text("I got your text message");
      else
        client->binary("I got your binary message");
    } else {
      //message is comprised of multiple frames or the frame is split into multiple packets
      if(info->index == 0){
        if(info->num == 0)
          Serial1.printf("ws[%s][%u] %s-message start\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
        Serial1.printf("ws[%s][%u] frame[%u] start[%llu]\n", server->url(), client->id(), info->num, info->len);
      }

      Serial1.printf("ws[%s][%u] frame[%u] %s[%llu - %llu]: ", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT)?"text":"binary", info->index, info->index + len);

      if(info->opcode == WS_TEXT){
        for(size_t i=0; i < len; i++) {
          msg += (char) data[i];
        }
      } else {
        char buff[3];
        for(size_t i=0; i < len; i++) {
          sprintf(buff, "%02x ", (uint8_t) data[i]);
          msg += buff ;
        }
      }
      Serial1.printf("%s\n",msg.c_str());

      if((info->index + len) == info->len){
        Serial1.printf("ws[%s][%u] frame[%u] end[%llu]\n", server->url(), client->id(), info->num, info->len);
        if(info->final){
          Serial1.printf("ws[%s][%u] %s-message end\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
          if(info->message_opcode == WS_TEXT)
            client->text("I got your text message");
          else
            client->binary("I got your binary message");
        }
      }
    }
  }
}

DateTime getTime() {
  return rtc.now();
  // Serial.print(now.unixtime());
  // Serial.print("Temperature: ");
  // Serial.print(rtc.getTemperature());
  // Serial.println(" C");
}

void onUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if(!index) {
      // Serial1.printf("Start upload: %s\n", filename.c_str());
      request->_tempFile = SPIFFS.open("/"+filename, "w");
    }
    if(len) request->_tempFile.write(data,len);
    if(final) {
      // Serial1.printf("UploadEnd: size:%x",(index+len));
      request->_tempFile.close();
    }
}

uint8_t send(Data card) {
  if (WiFIclient.connect(storage.server_url, storage.server_port)) {
    Serial1.println("connection");
   
    // while (client.available()) {
    // char line = client.read();
    // }

    String data = "{\"person_card\":" + String(card.card);
      data += ",\"person_status\":" + String(card.type);
      data += ",\"device_time\":" + String(card.time);
      data += ",\"device_location\":\"" + String(storage.device_location) + "\"";
      data += ",\"device_id\":" + String(espID) + "}";



    Serial.print("Requesting POST: ");
    // Send request to the server:
    WiFIclient.println("POST /api/v1/card HTTP/1.1");
    WiFIclient.print("Host: ");
    WiFIclient.println(storage.server_url);
    WiFIclient.println("Accept: */*");
    WiFIclient.println("Content-Type: application/json");
    WiFIclient.print("Content-Length: ");
    WiFIclient.println(data.length());
    WiFIclient.println();
    WiFIclient.print(data);
    delay(100); // Can be changed
    // while (WiFIclient.available()) {
    //   char line = WiFIclient.read();
    // }
    // Serial1.println(char);
    if (WiFIclient.connected()) { 
      WiFIclient.stop();  // DISCONNECT FROM THE SERVER
    }
    return 1;
  } else {
    Serial1.println("not connection");
    return 0;
  }
}

void cb(WiFiEvent_t event){
  switch(event) {
    case WIFI_EVENT_STAMODE_GOT_IP:
      Serial1.println("WiFi connected");
      Serial1.println("IP address: ");
      Serial1.println(WiFi.localIP());
      break;
    case WIFI_EVENT_STAMODE_DISCONNECTED:
      Serial1.println("WiFi lost connection");
      break;
  }
}

void setup() {  
  // Serial1.begin(115200);
  const char * hostName = "ESP";
  espID = ESP.getChipId();
  Serial1.begin(115200);
  EEPROM.begin(256);
  pinMode(PIN_STATE,INPUT);
  pinMode(READ_LED_PIN,OUTPUT);
  start_adress = readIntFromEEPROM(INDEX_START_ADRESS);
  Serial1.print("start_adress: ");
  Serial1.println(start_adress);
  // WiFi.onEvent();

  WiFi.onEvent (cb, WIFI_EVENT_ANY);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(hostName, storage.wifi_pass);
  WiFi.begin(storage.wifi_ssid, storage.wifi_pass);

  // if (WiFi.waitForConnectResult() != WL_CONNECTED) {
  //   Serial1.println("STA: Failed!\n");
  //   WiFi.disconnect(false);
  //   delay(1000);
  //   WiFi.begin(ssid, password);
  // }
   
  if (rtc.begin()) {
    if (rtc.lostPower()) {
      Serial1.println("RTC lost power, let's set the time!");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
  }

  Serial1.println("Start...");
  delay(2000);
  loadConfig();

  getTime();
  

  // findStruct(256);

	// pinMode(READ_LED_PIN, OUTPUT);
	// digitalWrite(READ_LED_PIN, LOW);

	rdm6300.begin(RDM6300_RX_PIN);

  //Send OTA events to the browser
  ArduinoOTA.onStart([]() { events.send("Update Start", "ota"); });
  ArduinoOTA.onEnd([]() { events.send("Update End", "ota"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    char p[32];
    sprintf(p, "Progress: %u%%\n", (progress/(total/100)));
    events.send(p, "ota");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    if(error == OTA_AUTH_ERROR) events.send("Auth Failed", "ota");
    else if(error == OTA_BEGIN_ERROR) events.send("Begin Failed", "ota");
    else if(error == OTA_CONNECT_ERROR) events.send("Connect Failed", "ota");
    else if(error == OTA_RECEIVE_ERROR) events.send("Recieve Failed", "ota");
    else if(error == OTA_END_ERROR) events.send("End Failed", "ota");
  });
  ArduinoOTA.setHostname(hostName);
  ArduinoOTA.begin();

  MDNS.addService("http","tcp",80);

  if (!SPIFFS.begin()) {
		Serial1.print(F("[ WARN ] Formatting filesystem..."));
		if (SPIFFS.format()) {
      Serial1.println(F("Filesystem formatted!"));
		} else {
			Serial1.println(F(" failed!"));
			Serial1.println(F("[ WARN ] Could not format filesystem!"));
		}
	}
  SPIFFS.info(fs_info);

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  events.onConnect([](AsyncEventSourceClient *client){
    
    client->send("hello!",NULL,millis(),1000);
  });
  server.addHandler(&events);
  // server.addHandler(new SPIFFSEditor(storage.http_username,storage.http_password));
  
  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(getTime().unixtime()));
  });

  server.serveStatic("/", SPIFFS, "/")
    .setDefaultFile("index.html")
    .setAuthentication(DEF_HTTP_LOGIN, DEF_HTTP_PASS);

  // server.serveStatic("/", SPIFFS, "/www/").setDefaultFile("default.html");

  server.onNotFound([](AsyncWebServerRequest *request){
    Serial1.printf("NOT_FOUND: ");
    if(request->method() == HTTP_GET)
      Serial1.printf("GET");
    else if(request->method() == HTTP_POST)
      Serial1.printf("POST");
    else if(request->method() == HTTP_DELETE)
      Serial1.printf("DELETE");
    else if(request->method() == HTTP_PUT)
      Serial1.printf("PUT");
    else if(request->method() == HTTP_PATCH)
      Serial1.printf("PATCH");
    else if(request->method() == HTTP_HEAD)
      Serial1.printf("HEAD");
    else if(request->method() == HTTP_OPTIONS)
      Serial1.printf("OPTIONS");
    else
      Serial1.printf("UNKNOWN");
    Serial1.printf(" http://%s%s\n", request->host().c_str(), request->url().c_str());

    if(request->contentLength()){
      Serial1.printf("_CONTENT_TYPE: %s\n", request->contentType().c_str());
      Serial1.printf("_CONTENT_LENGTH: %u\n", request->contentLength());
    }

    int headers = request->headers();
    int i;
    for(i=0;i<headers;i++){
      AsyncWebHeader* h = request->getHeader(i);
      Serial1.printf("_HEADER[%s]: %s\n", h->name().c_str(), h->value().c_str());
    }

    int params = request->params();
    for(i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isFile()){
        Serial1.printf("_FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
      } else if(p->isPost()){
        Serial1.printf("_POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
      } else {
        Serial1.printf("_GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
      }
    }
    request->send(404);
  });

  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request){
    request->send(200);
  }, onUpload);

  server.on("/test", HTTP_GET, [](AsyncWebServerRequest *request){
    uint8_t buf[8] = {255,255,50,255,255,255,65,0};
    request->send_P(200, "text/plain", buf, sizeof(buf));
  });

  server.on("/upload", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", "<form method='POST' action='/upload' enctype='multipart/form-data'><input type='file' name='upload'><input type='submit' value='Upload'></form>");
  });

  // server.onFileUpload([](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final){
  //   if(!index)
  //     Serial1.printf("UploadStart: %s\n", filename.c_str());
  //   Serial1.printf("%s", (const char*)data);
  //   if(final)
  //     Serial1.printf("UploadEnd: %s (%u)\n", filename.c_str(), index+len);
  // });

  server.on("/reboot", HTTP_ANY, [](AsyncWebServerRequest *request){
    request->send(200, "application/json", "{\"state\":true}");
    shouldReboot = true;
  });

  server.on("/erase", HTTP_ANY, [](AsyncWebServerRequest *request){
    isErase = true;
    request->send(200, "application/json", "{\"state\":true}");
    shouldReboot = true;
  });

  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>");
  });

  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
    shouldReboot = !Update.hasError();
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot?"OK":"FAIL");
    response->addHeader("Connection", "close");
    request->send(response);
    },[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
      if(!index){
        Serial1.printf("Update Start: %s\n", filename.c_str());
        Update.runAsync(true);
        if(!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)){
          Update.printError(Serial1);
        }
      }
      if(!Update.hasError()){
        if(Update.write(data, len) != len){
          Update.printError(Serial1);
        }
      }
      if(final){
        if(Update.end(true)){
          Serial1.printf("Update Success: %uB\n", index+len);
        } else {
          Update.printError(Serial1);
        }
      }
    });

  server.on("/scan", HTTP_POST, [](AsyncWebServerRequest *request){
    String json = "[";
    int n = WiFi.scanComplete();
    if(n == -2){
      WiFi.scanNetworks(true);
    } else if(n){
      for (int i = 0; i < n; ++i){
        if(i) json += ",";
        json += "{";
        json += "\"rssi\":"+String(WiFi.RSSI(i));
        json += ",\"ssid\":\""+WiFi.SSID(i)+"\"";
        json += ",\"bssid\":\""+WiFi.BSSIDstr(i)+"\"";
        json += ",\"channel\":"+String(WiFi.channel(i));
        json += ",\"secure\":"+String(WiFi.encryptionType(i));
        json += ",\"hidden\":"+String(WiFi.isHidden(i)?"true":"false");
        json += "}";
      }
      WiFi.scanDelete();
      if(WiFi.scanComplete() == -2){
        WiFi.scanNetworks(true);
      }
    }
    json += "]";
    request->send(200, "application/json", json);
    json = String();
  });

  server.on("/settings", HTTP_POST, [](AsyncWebServerRequest *request){
    String json;
      json += "{";
      json += "\"server_url\":\"" + String(storage.server_url) + "\"";;
      json += ",\"server_port\":" + String(storage.server_port);
      json += ",\"http_mode\":" + String(storage.http_mode);
      json += ",\"http_login\":\"" + String(storage.http_login) + "\"";
      json += ",\"http_pass\":\"" + String(storage.http_pass) + "\"";
      json += ",\"wifi_mode\":" + String(storage.wifi_mode);
      json += ",\"wifi_ssid\":\"" + String(storage.wifi_ssid) + "\"";
      json += ",\"wifi_pass\":\"" + String(storage.wifi_pass) + "\"";
      json += ",\"device_location\":\"" + String(storage.device_location) + "\"";
      json += ",\"device_id\":" + String(espID);
      json += ",\"device_firmware\":\"" + String(device_firmware) + "\"";
      json += "}";
    request->send(200, "application/json", json);
    json = String();
  });

  AsyncCallbackJsonWebHandler *handler = new AsyncCallbackJsonWebHandler("/settings", [](AsyncWebServerRequest *request, JsonVariant &json) {
    StaticJsonDocument<200> data;
    if (json.is<JsonArray>()) {
      data = json.as<JsonArray>();
    } else if (json.is<JsonObject>()) {
      JsonObject data = json.as<JsonObject>();
      for (JsonPair kv : data) {
        // Serial1.print(kv.key().c_str());
        // Serial1.print(":");
        if(kv.value().is<char*>()) {
          // Serial1.println(kv.value().as<char*>());
          if (!strcmp(kv.key().c_str(), "http_login")) strncpy(storage.http_login, kv.value().as<char*>(), 10);
          if (!strcmp(kv.key().c_str(), "http_pass")) strncpy(storage.http_pass, kv.value().as<char*>(), 10);
          if (!strcmp(kv.key().c_str(), "server_url")) strncpy(storage.server_url, kv.value().as<char*>(), 20);
          if (!strcmp(kv.key().c_str(), "wifi_ssid")) strncpy(storage.wifi_ssid, kv.value().as<char*>(), 20);
          if (!strcmp(kv.key().c_str(), "wifi_pass")) strncpy(storage.wifi_pass, kv.value().as<char*>(), 20);
          if (!strcmp(kv.key().c_str(), "device_location")) strncpy(storage.device_location, kv.value().as<char*>(), 10);
          continue;
        } else {
          if (!strcmp(kv.key().c_str(), "wifi_mode")) storage.wifi_mode = kv.value().as<uint8_t>();
          else if (!strcmp(kv.key().c_str(), "http_mode")) storage.http_mode = kv.value().as<uint8_t>();
          else if (!strcmp(kv.key().c_str(), "server_port")) storage.server_port = kv.value().as<uint16_t>();
        }
      }
      saveConfig();
      Serial1.println(storage.http_mode);
      Serial1.println(storage.http_login);
      Serial1.println(storage.http_pass);
      Serial1.println(storage.wifi_mode);
      Serial1.println(storage.wifi_ssid);
      Serial1.println(storage.wifi_pass);
      Serial1.println(storage.server_url);
      Serial1.println(storage.server_port);

        // if(kv.value().is<uint32_t>()) {
        //   Serial1.println(kv.value().as<uint32_t>());
        //   continue;
        // }
        // if(kv.value().is<uint16_t>()) {
        //   Serial1.println(kv.value().as<uint16_t>());
        //   continue;
        // }
        // if(kv.value().is<uint8_t>()) Serial1.println(kv.value().as<uint8_t>());
    }
    String response;
    serializeJson(data, response);
    request->send(200, "application/json", response);
  });
  server.addHandler(handler);
  server.begin();
}

void loop(){
  ArduinoOTA.handle();
  ws.cleanupClients();
  if(shouldReboot){
    Serial1.println("Rebooting...");
    delay(100);
    ESP.restart();
  }

  uint32_t now = millis();

  if (rdm6300.update() && reset_counter) {
    last_time = now;
    reset_counter = 0;
    digitalWrite(READ_LED_PIN, HIGH);   
    
    uint_fast32_t card_id = rdm6300.get_tag_id();
    Data* card = &cardTemp;
    card->card = card_id;
    card->time = getTime().unixtime();
    card->type = digitalRead(PIN_STATE);

    uint8_t checksum = crc(card_id);
    Serial1.print("checksum=");
    Serial1.println(checksum);
    card->check = checksum;
    card->fund = 0;
    card->next = 0;
    uint8_t sendOK = send(*card);
    card->send = sendOK;
    eeWrite(start_adress,*card);
    start_adress = start_adress + sizeof(Data);
    if (start_adress == 4096) start_adress = 0;
    writeIntIntoEEPROM(INDEX_START_ADRESS, start_adress);
    if (isWsConnected) {
      uint8_t test[16];
      // for (unsigned int t=0; t<sizeof(card); t++) test[t] = *((char*)&card + t);
      writeAnything(test, *card);
      ws.binaryAll(test, sizeof(test));
    }
    // findStruct(10);
    printCard(*card);
  }
  if(isLoad) load();
  if(isErase) eraseEeprom();

  if (now - last_time > 5000) {
    digitalWrite(READ_LED_PIN, LOW);
    reset_counter=1;
    last_time = now;
  }
}