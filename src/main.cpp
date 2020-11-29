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

#define EEPROM_ADDRESS 0x57
#define RDM6300_RX_PIN 13 // can be only 13 - on esp8266 force hardware uart!
#define READ_LED_PIN 15
#define PIN_STATE 12
#define CONFIG_VERSION "ls1"
#define CONFIG_START 32

Rdm6300 rdm6300;
WiFiClient WiFIclient;
RTC_DS3231 rtc;

uint32_t espID; 

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncEventSource events("/events");
AsyncWebSocketClient * client;

FSInfo fs_info;

uint8_t reset_counter = 0;
uint16_t start_adress = 0;
uint32_t last_time = 0;

// char host[] = "192.168.1.31";
// uint16_t port = 3000;

// const char* ssid = "Net_1";
// const char* password = "Sotex110605";
// const char* http_username = "admin";
// const char* http_password = "admin";
bool shouldReboot = false;

struct Data {
  uint32_t unixtime;
  uint32_t card_id;
  uint32_t state;
  uint8_t any[4];
};

// struct LastAdr {
//   char version[4];
//   uint16_t lastAdr;
//   uint8_t crc;
// } start_adress = {
//   CONFIG_VERSION,
//   0,
//   15
// };

struct StoreStruct {
  char version[4];
  char host[15];
  uint16_t port;
  char ssid[20];
  char password[20];
  char http_username[10];
  char http_password[10];
} storage = {
  CONFIG_VERSION,
  "192.168.1.31",
  3000,
  "Net_1",
  "Sotex110605",
  "admin",
  "admin"  
};

void loadConfig() {
  if (EEPROM.read(CONFIG_START + 0) == CONFIG_VERSION[0] &&
      EEPROM.read(CONFIG_START + 1) == CONFIG_VERSION[1] &&
      EEPROM.read(CONFIG_START + 2) == CONFIG_VERSION[2])
    for (unsigned int t=0; t<sizeof(storage); t++) *((char*)&storage + t) = EEPROM.read(CONFIG_START + t);
    Serial1.print("Load config: ");
}

void saveConfig() {
  for (unsigned int t=0; t<sizeof(storage); t++)
    EEPROM.write(CONFIG_START + t, *((char*)&storage + t));
    Serial1.print("Save config: ");
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
  uint8_t bi = 255;
  for (size_t i = 0; i < 4096; i++) {
    eeWrite(i, bi);
    delay(10);
  }
}

void printCard(Data str) {
  Serial1.print("card_id: ");
  Serial1.println(str.card_id);
  Serial1.print("unixtime: ");
  Serial1.println(str.unixtime);
  Serial1.print("state: ");
  Serial1.println(str.state);
  Serial1.print("checksum: ");
  Serial1.println(str.any[0]);
  Serial1.print("send: ");
  Serial1.println(str.any[1]);
  Serial1.println();
}

void findStruct(uint8_t byte) {
  Data test;
  uint16_t adr = 0;
  uint8_t checksum = 0;
  for (size_t i = 0; i < byte; i++) {
    eeRead(adr, test);
    adr = adr + sizeof(Data);
    checksum = crc(test.card_id);
    if(checksum != test.any[0]) break;
    printCard(test);
  }
  Serial1.println("Search is over");
}

void findDevice() {
  byte error, address;
  int nDevices; 
  Serial1.println("Scanning...");
  nDevices = 0;
  for(address = 1; address < 127; address++ ) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
     if (error == 0) {
      Serial1.print("I2C device found at address 0x");
      if (address<16) Serial.print("0");
      Serial1.print(address,HEX);
      Serial1.println("  !"); 
      nDevices++;
    } else if (error==4) {
      Serial1.print("Unknown error at address 0x");
      if (address<16) Serial1.print("0");
      Serial1.println(address,HEX);
    }    
  }
  if (nDevices == 0) Serial1.println("No I2C devices found\n");
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
  } else if(type == WS_EVT_DISCONNECT){
    Serial1.printf("ws[%s][%u] disconnect\n", server->url(), client->id());
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
  if (WiFIclient.connect(storage.host, storage.port)) {
    Serial1.println("connection");
   
    // while (client.available()) {
    // char line = client.read();
    // }

    String data = "{\"card\":" + String(card.card_id)  + ",\"time_device\":" + String(card.unixtime) + ",\"state\":" + card.state + "}";
    Serial.print("Requesting POST: ");
    // Send request to the server:
    WiFIclient.println("POST /api/v1/card HTTP/1.1");
    WiFIclient.print("Host: ");
    WiFIclient.println(storage.host);
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
  EEPROM.begin(512);
  pinMode(PIN_STATE,INPUT);
  pinMode(READ_LED_PIN,OUTPUT);
  // start_adress = EEPROM.get(0, start_adress);
  Serial1.println();
  // WiFi.onEvent();

  WiFi.onEvent (cb, WIFI_EVENT_ANY);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(hostName, storage.password);
  WiFi.begin(storage.ssid, storage.password);

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
  loadConfig();

  getTime();
  // eraseEeprom();
  findStruct(10);

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
		}
		else {
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
  server.addHandler(new SPIFFSEditor(storage.http_username,storage.http_password));
  
  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(getTime().unixtime()));
  });

  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.htm");
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

  server.on("/upload", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", "<form method='POST' action='/upload' enctype='multipart/form-data'><input type='file' name='upload'><input type='submit' value='Upload'></form>");
  });

  server.onFileUpload([](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final){
    if(!index)
      Serial1.printf("UploadStart: %s\n", filename.c_str());
    Serial1.printf("%s", (const char*)data);
    if(final)
      Serial1.printf("UploadEnd: %s (%u)\n", filename.c_str(), index+len);
  });

  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    if(!index)
      Serial1.printf("BodyStart: %u\n", total);
    Serial1.printf("%s", (const char*)data);
    if(index + len == total)
      Serial1.printf("BodyEnd: %u\n", total);
  });
    // Simple Firmware Update Form
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

  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request){
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


  server.on("/eeprom", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "[";
    Data test;
    uint16_t adr = 0;
    uint8_t checksum = 0;
    for (size_t i = 0; i < 10; i++) {
      eeRead(adr, test);
      adr = adr + sizeof(Data);
      checksum = crc(test.card_id);
      if(checksum != test.any[0]) break;

      if(i) json += ",";
      json += "{";
      json += "\"card_id\":" + test.card_id;
      json += ",\"unixtime\":" + test.unixtime;
      json += ",\"state\":" + test.state;
      json += "}";
    }
    json += "]";
    request->send(200, "application/json", json);
    json = String();
  });



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

  if (now - last_time > 2000) {
    digitalWrite(READ_LED_PIN, LOW);
    reset_counter=1;
    last_time = now;
  }

  if (rdm6300.update() && reset_counter) {
    digitalWrite(READ_LED_PIN, HIGH);   
    reset_counter=0;
    uint_fast32_t card_id = rdm6300.get_tag_id();

    Data card;
    card.card_id = card_id;
    card.unixtime = getTime().unixtime();
    card.state = digitalRead(PIN_STATE);

    uint8_t checksum = crc(card_id);
    Serial1.print("checksum=");
    Serial1.println(checksum);
    card.any[0] = checksum;
    uint8_t sendOK = send(card);
    card.any[1] = sendOK;
    eeWrite(start_adress,card);
    start_adress = start_adress + sizeof(Data);
    if (start_adress == 4096) start_adress = 0;
    
    // send(card);
    printCard(card);
  }
}