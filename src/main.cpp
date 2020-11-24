#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFSEditor.h>
#include <EEPROM.h>
#include "RTClib.h"
#include <rdm6300.h>


#define RDM6300_RX_PIN 13 // can be only 13 - on esp8266 force hardware uart!
#define READ_LED_PIN 16
Rdm6300 rdm6300;
WiFiClient WiFIclient;
RTC_DS3231 rtc;

// SKETCH BEGIN
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncEventSource events("/events");
AsyncWebSocketClient * client;

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


const char* ssid = "Net_1";
const char* password = "Sotex110605";
const char * hostName = "esp-async";
const char* http_username = "admin";
const char* http_password = "admin";
bool shouldReboot = false;

char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

FSInfo fs_info;
int address = 0;

struct Data
{
  uint32_t time;
  uint32_t id;
  uint8_t num;
  uint8_t type;
  uint8_t state;
};

Data persons[] = {           // Создаем массив объектов пользовательской структуры
    {
      20,
      232,
      323,
      34,
      42
    }
  };

void save (Data *persons) {
  for (int i = 0; i < 3; i++) {
    EEPROM.put(address, persons[i]);    // Записываем значение переменной в EEPROM
    EEPROM.commit();
    address += sizeof(Data); // Корректируем адрес следующей записи на объем записываемых данных

  }
}

void load () {
  address = 0;
  Data person; // В переменную person будем считывать данные из EEPROM
  for (int i = 0; i < 3; i++) {
    EEPROM.get(address, person);      // Считываем данные из EEPROM в созданную переменную
    Serial1.println("Чтение пользовательской структуры из EEPROM по адресу: " + String(address));
    Serial1.printf("num: %x, time: %8x",  person.num, person.time);
    Serial1.println();
    address += sizeof(Data); // Корректируем адрес следующей записи на объем записываемых данных
  }
}

void setupTime() {
  if (rtc.begin()) {
    if (rtc.lostPower()) {
      Serial1.println("RTC lost power, let's set the time!");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
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

void onUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    if(!index){
      Serial1.printf("Start upload: %s\n", filename.c_str());
      // open the file on first call and store the file handle in the request object
      request->_tempFile = SPIFFS.open("/"+filename, "w");
    }
    if(len) {
      // stream the incoming chunk to the opened file
      request->_tempFile.write(data,len);
    }
    if(final){
      Serial1.printf("UploadEnd: size:%x",(index+len));
      // close the file handle as the upload is now done
      request->_tempFile.close();
      // request->redirect("/files");
    }
}

void send() {
  
  char host[] = "192.168.1.31";
  if (WiFIclient.connect(host, 3000)) {
    Serial1.println("connection");
    // client.print( "GET /?");
    // client.print("temp-1=1212");
    // client.println( " HTTP/1.1");
    // client.print( "Host:" );
    // client.println(host);
    // client.println( "Connection: close" );
    // client.println();
    // client.println();
   
    // while (client.available()) {
    // char line = client.read();
    // }

    String data = "pst=temperature>" + String(random(0,100)) +"||humidity>" + String(random(0,100)) + "||data>text";

      Serial.print("Requesting POST: ");
      // Send request to the server:
      WiFIclient.println("POST / HTTP/1.1");
      WiFIclient.print("Host: ");
      WiFIclient.println(host);
      WiFIclient.println("Accept: */*");
      WiFIclient.println("Content-Type: application/x-www-form-urlencoded");
      WiFIclient.print("Content-Length: ");
      WiFIclient.println(data.length());
      WiFIclient.println();
      WiFIclient.print(data);

      delay(500); // Can be changed
     if (WiFIclient.connected()) { 
       WiFIclient.stop();  // DISCONNECT FROM THE SERVER
     }

  } else {
    Serial1.println("not connection");
  }


}

void setup() {  
  // Serial1.begin(115200);
  Serial1.begin(115200);
  EEPROM.begin(512);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(hostName);
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial1.printf("STA: Failed!\n");
    WiFi.disconnect(false);
    delay(1000);
    WiFi.begin(ssid, password);
  }
  
  Serial1.println(String(sizeof(Data)));
  load();
  setupTime();
  // getTime();
  
  

	// pinMode(READ_LED_PIN, OUTPUT);
	// digitalWrite(READ_LED_PIN, LOW);

	rdm6300.begin(RDM6300_RX_PIN);

	// Serial1.println("\nPlace RFID tag near the rdm6300...");

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
  // server.addHandler(new SPIFFSEditor(http_username,http_password));
  
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

  if (rdm6300.update()) {
    Serial1.println(rdm6300.get_tag_id(), HEX);
    send();
  }

    // digitalWrite(READ_LED_PIN, rdm6300.is_tag_near());
    // delay(10);
}