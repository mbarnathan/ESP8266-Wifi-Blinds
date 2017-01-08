#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266SSDP.h>

const char* ssid = "SSID";
const char* password = "password";

ESP8266WebServer HTTP(80);

const int RED_LED = 0;
const int BLUE_LED = 2;

const int PIN_UP = 15;
const int PIN_DOWN = 16;

const int UP_TRIGGER = 13;
const int DOWN_TRIGGER = 12;

unsigned long riseStart = 0;
unsigned long fallStart = 0;

volatile boolean pendingUp = false;
volatile boolean pendingDown = false;

const int DRAW_MS = 18000; // Time from 100% up to 100% down / vice versa

int currentLevel = 0;
String updateAddress = "";

template<class T> inline Print &operator <<(Print &obj, T arg) { obj.print(arg); return obj; }

// These ISRs are idempotent until the polling loop picks up the var, so no need to debounce.

void ICACHE_RAM_ATTR upPressed() {
  if (digitalRead(UP_TRIGGER) == LOW) {
    // Falling.
    digitalWrite(PIN_UP, HIGH);
    pendingUp = true;
  } else {
    // Rising
    digitalWrite(PIN_UP, LOW);
  }
}

void ICACHE_RAM_ATTR downPressed() {
  if (digitalRead(DOWN_TRIGGER) == LOW) {
    // Falling.
    digitalWrite(PIN_DOWN, HIGH);
    pendingDown = true;
  } else {
    // Rising
    digitalWrite(PIN_DOWN, LOW);
  }
}

void setup() {
  resetPins();

  Serial.begin(9600);
  Serial.println();
  Serial.println("Starting WiFi...");

  setupWifi();
}

void setupWifi() {
  while (true) {
    digitalWrite(BLUE_LED, HIGH);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
  
    if (WiFi.waitForConnectResult() == WL_CONNECTED) {
      digitalWrite(BLUE_LED, LOW);
      Serial.println(WiFi.localIP());
  
      Serial.printf("Starting HTTP...\n");
      HTTP.on("/set", [](){
        String level = HTTP.arg("level");
        Serial << "Set request: " << level << "\n";
        echoArgs(HTTP);

        int levelInt = level.toInt();
        if (levelInt < 0 || levelInt > 100) {
          HTTP.send(400, "text/plain", "Level out of range");
          return;
        }
        setLevel(levelInt);
        HTTP.send(200, "text/plain", String("level: ") + level);
      });
  
      HTTP.on("/get", [](){
        Serial << "Get request: " << currentLevel << "\n";
        HTTP.send(200, "text/plain", String("level: ") + String(currentLevel));
      });

      HTTP.on("/register", [](){
        updateAddress = HTTP.client().remoteIP().toString();
        HTTP.send(200, "text/plain", "Hi! I'll send updates your way.");
      });

      HTTP.on("/description.xml", HTTP_GET, [](){
        Serial.println("description.xml request");
        SSDP.schema(HTTP.client());
      });

      addUpdatingTo(HTTP);

      HTTP.begin();
  
      Serial.printf("Starting SSDP...\n");
      SSDP.setSchemaURL("description.xml");
      SSDP.setHTTPPort(80);
      SSDP.setName("Shangri-La Sheer Shades");
      SSDP.setSerialNumber("948135487535");
      SSDP.setDeviceType("urn:schemas-blinds:device:WifiBlinds:1");
      SSDP.setURL("index.html");
      SSDP.setModelName("Shangri-La Sheer Shades, ESP8266 Wifi");
      SSDP.setModelNumber("498198197823");
      SSDP.setModelURL("http://www.github.com/mbarnathan");
      SSDP.setManufacturer("Alight Development Studio");
      SSDP.setManufacturerURL("http://alight.studio");
      SSDP.begin();

      Serial.println("Attaching interrupt...");
      attachInterrupt(digitalPinToInterrupt(UP_TRIGGER), upPressed, CHANGE);
      attachInterrupt(digitalPinToInterrupt(DOWN_TRIGGER), downPressed, CHANGE);

      Serial.printf("Ready!\n");
      return;
    } else {
      digitalWrite(BLUE_LED, LOW);
      Serial.printf("Wifi Failed\n");
      for (int second = 0; second < 10; second++) {
        digitalWrite(RED_LED, HIGH);
        delay(500);
        digitalWrite(RED_LED, LOW);
        delay(500);
      }
  
      // Try again
      Serial.println("Retrying Wifi connection...");
    }
  }
}

void echoArgs(ESP8266WebServer& http) {
  Serial << http.args() << " arguments: ";
  for (int arg = 0; arg < http.args(); arg++) {
    Serial << http.arg(arg) << ", ";
  }
  Serial.println();
}

void setLevel(int percent) {
  if (percent == currentLevel) {
    // No-op.
    return;
  }

  Serial << "Setting level from " << currentLevel << " to " << percent << ".\n";

  int pinToEngage;
  int stopPin;

  if (percent < currentLevel) {
    Serial.println("Going up!");
    digitalWrite(BLUE_LED, HIGH);
    pinToEngage = PIN_UP;
    stopPin = PIN_DOWN;
    sendUp();
  } else {
    Serial.println("Going down!");
    digitalWrite(RED_LED, HIGH);
    pinToEngage = PIN_DOWN;
    stopPin = PIN_UP;
    sendDown();
  }

  // How long to get to the new level?
  int duration = abs(percent - currentLevel) * DRAW_MS / 100;

  // If all the way up or down, add an extra 1s
  // Since going all the way, it improves accuracy of this common use case and is "free" - the blinds can't go to 101%.
  if (percent == 0 || percent == 100) {
    duration += 1000;
  }

  Serial << "This will take " << duration << " ms.\n";

  engagePin(pinToEngage, 1000);
  Serial << "Wait for it...\n";
  delay(duration);
  engagePin(stopPin, 100);
  
  digitalWrite(BLUE_LED, LOW);
  digitalWrite(RED_LED, LOW);

  currentLevel = percent;
  sendCurrentLevel();
  Serial.println("Done setting level.");
}

void resetPins() {
  pinMode(UP_TRIGGER, INPUT);
  pinMode(DOWN_TRIGGER, INPUT);
  pinMode(PIN_UP, OUTPUT);
  pinMode(PIN_DOWN, OUTPUT);
  digitalWrite(PIN_UP, LOW);
  digitalWrite(PIN_DOWN, LOW);
}

// Pins never need to be held on, just "flicked".
void engagePin(int pin, int wait_ms) {
  Serial << "Setting pin " << pin << " HIGH\n";
  digitalWrite(pin, HIGH);
  delay(wait_ms);
  Serial << "Setting pin " << pin << " LOW\n";
  digitalWrite(pin, LOW);
}

void loop() {
  HTTP.handleClient();
  handlePins();
  delay(1);
}

void handlePins() {
  int newLevel = -1;

  if (pendingUp) {
    riseStart = millis();
    if (fallStart > 0) {
      newLevel = constrain(currentLevel - (riseStart - fallStart) / DRAW_MS * 100, 0, 100);
      fallStart = 0;
    }
  }

  if (pendingDown) {
    fallStart = millis();
    if (riseStart > 0) {
      newLevel = constrain(currentLevel + (fallStart - riseStart) / DRAW_MS * 100, 0, 100);
      riseStart = 0;
    }
  }

  if (newLevel >= 0 && newLevel != currentLevel) {
    currentLevel = newLevel;
    sendCurrentLevel();
  } else if (pendingUp && !pendingDown) {
    sendUp();
  } else if (pendingDown && !pendingUp) {
    sendDown();
  }

  pendingUp = false;
  pendingDown = false;
}

void sendUp() {
  if (currentLevel > 0) {
    request("status: rising");
  }
}

void sendDown() {
  if (currentLevel < 100) {
    request("status: falling");
  }
}

void sendCurrentLevel() {
  request(String("level: ") + String(currentLevel));
}

void request(String data) {
  if (updateAddress.length() == 0) {
    Serial << "No update address registered, not sending " << data << "\n";
    return;
  }

  Serial << "Sending data to " << updateAddress << ": " << data << "\n";

  HTTPClient hclient;
  if (!hclient.begin(String("http://") + updateAddress)) {
    Serial << "Error making HTTP request to " << updateAddress << "\n";
  }

  int response = hclient.POST(data);
  if (response != HTTP_CODE_OK) {
    Serial << "Response " << response << " from " << updateAddress << "\n";
  }

  hclient.end();
}

void addUpdatingTo(ESP8266WebServer &server) {
  server.on("/update", HTTP_GET, [&](){
    static const char* updateIndex = "<form method='POST' action='/do_update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";
    server.sendHeader("Connection", "close");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/html", updateIndex);
  });
  server.on("/do_update", HTTP_POST, [&](){
    server.sendHeader("Connection", "close");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", (Update.hasError())?"FAIL":"OK");
    ESP.restart();
  },[&](){
    HTTPUpload& upload = server.upload();
    if(upload.status == UPLOAD_FILE_START){
      Serial.setDebugOutput(true);
      WiFiUDP::stopAll();
      Serial.printf("Update: %s\n", upload.filename.c_str());
      uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      if(!Update.begin(maxSketchSpace)){//start with max available size
        Update.printError(Serial);
      }
    } else if(upload.status == UPLOAD_FILE_WRITE){
      if(Update.write(upload.buf, upload.currentSize) != upload.currentSize){
        Update.printError(Serial);
      }
    } else if(upload.status == UPLOAD_FILE_END){
      if(Update.end(true)){ //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
      Serial.setDebugOutput(false);
    }
    yield();
  });
}

