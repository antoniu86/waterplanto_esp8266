#include "ESP8266WiFi.h"
#include "EEPROM.h"
#include "ESP8266HTTPClient.h"
#include "ArduinoJson.h"

// CONSTANTS

// SERIAL NUMBER
#define SERIAL_NUMBER               "123456654321"

// KEY
#define KEY                         10021986

// retea
#define NET_SSID                    "waterplanto"
#define NET_PASS                    "password"
#define ENDPOINT_DOMAIN             "http://waterplanto.herokuapp.com"
#define ENDPOINT_UPDATE             "/device_api/update"

// umiditate a solului
#define MOISTURE_THRESHOLD          35
#define WATER_FOR                   3000 // 3 seconds

// VARIABLES

// http
String update_endpoint =            ENDPOINT_DOMAIN + (String)ENDPOINT_UPDATE;

// network
const int addr_ssid =               0;  // ssid index
const int addr_ssid_size =          20; // ssid index size
const int addr_password =           20; // password index
const int addr_password_size =      20; // password index size
const int addr_moisture =           40; // humidity index

String ssid =                       NET_SSID;
String password =                   NET_PASS;

int wifi_connect_retries =          0;
int wifi_connect_retries_limit =    30;
int default_network_count =         0;
int default_network_limit =         2;
bool default_network_flag =         false;

// pins
const int moisture_Pin =            0;
const int nivel_apa =               1;
const int relay =                   5;

// request
const int request_timing =          15; // request every 15 seconds
int request_timing_count =          15;

// sensors
int moisture_value =                0;
int moisture_threshold =            MOISTURE_THRESHOLD;
const int moisture_threshold_max =  50;

bool start_water =                  false;
int start_water_for =               WATER_FOR;

HTTPClient http;
WiFiClient client;

void setup() {
  Serial.begin(115200);

  // pinul pentru releu
  pinMode(relay, OUTPUT);

  delay(1000);

  Serial.println("");

  // Read from EEPROM
  EEPROM.begin(512);
  String ssid_read;
  for (int k = addr_ssid; k < addr_ssid + addr_ssid_size; k++) {
    ssid_read += char(EEPROM.read(k));
  }

  String password_read;
  for (int l = addr_password; l < addr_password + addr_password_size; l++) {
    password_read += char(EEPROM.read(l));
  }

  int moisture_threshold_read = EEPROM.read(addr_moisture);
  Serial.println("Moisture threshold: " + moisture_threshold_read);
  EEPROM.end();

  // set ssid and password if needed
  if ((ssid_read != "" && ssid_read != ssid) && (password_read != "" && password_read != password)) {
    Serial.println("SSID and Password from EEPROM diferent");
    Serial.print("SSID: " + ssid);
    Serial.println(" / Password: " + password);
    
    ssid = ssid_read;
    password = password_read;

    Serial.print("NEW SSID (taken from EEPROM): " + ssid);
    Serial.println(" / NEW Password (taken from EEPROM): " + password);
  }

  // set moisture threashold
  if (moisture_threshold_read) {
    Serial.println("Moisture threshold from EEPROM diferent");
    Serial.print("Moisture threshold: " + moisture_threshold);
    
    moisture_threshold = (int)moisture_threshold_read;

    Serial.print(" / NEW Moisture threshold (taken from EEPROM): " + moisture_threshold);
  }

  WiFi.mode(WIFI_STA);
}

void loop() {
  Serial.print("SSID = ");
  Serial.println(ssid);

  Serial.print("Password = ");
  Serial.println(password);

  Serial.print("Moisture = ");
  Serial.println(moisture_threshold);
  
  if (WiFi.status() != WL_CONNECTED) {
    connect_to_wifi();
  } else {
    default_network_count = 0;

    Serial.print("request_timing_count = ");
    Serial.println(request_timing_count);
    
    if (request_timing_count >= request_timing) {
      server_request();
      request_timing_count = 0;
    } else {
      request_timing_count += 1;
    }
    
    checkMoisture();
  }

  Serial.println("------------------------------------------");
  
  delay(1000);
}

// request

void server_request() {
  bool disconnect_from_network = false;
  
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, http_request_update());

  String netname = doc["netname"];
  
  if (netname != ssid) {
    ssid = netname;
    disconnect_from_network = true;

    EEPROM.begin(512);
    int i = 0;
    for (int k = addr_ssid; k < addr_ssid + addr_ssid_size; k++) {
      EEPROM.write(k, ssid[i]);
      i++;
    }
    EEPROM.end();
  }

  String netpass = doc["netpass"];
  
  if (netpass != password) {
    password = netpass;
    disconnect_from_network = true;

    EEPROM.begin(512);
    int i = 0;
    for (int k = addr_password; k < addr_password + addr_password_size; k++) {
      EEPROM.write(k, password[i]);
      i++;
    }
    EEPROM.end();
  }

  if (disconnect_from_network == true) {
    WiFi.disconnect();
  }

  int limit = ((double)doc["limit"] / 100) * moisture_threshold_max;
  
  if (limit != moisture_threshold) {
    moisture_threshold = limit;

    EEPROM.begin(512);
    EEPROM.put(addr_moisture, moisture_threshold);
    EEPROM.end();
  }

  bool water = doc["water"];

  if (water == true) {
    start_water = water;
  }
}

String http_request_update() {
  int circle = random(1, 10);
  int square = random(11, 20);
  int triangle = random(21, 30);
  int key = calculate_key(circle, square, triangle);
  
  http.begin(client, update_endpoint + "?code=" + (String)SERIAL_NUMBER + "&ci=" + circle + "&sq=" + square + "&tr=" + triangle + "&humidity=" + moisture_value);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("Token", "Key " + (String)key);
  
  int httpResponseCode = http.GET();

  String payload = "{}";
  
  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    payload = http.getString();
    Serial.println(payload);
  } else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
    Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
  }
  
  http.end();

  return payload;
}

// soil humidity

void checkMoisture() {
  Serial.print("Nivel umiditate sol : ");
  moisture_value = analogRead(moisture_Pin);
  moisture_value = moisture_value/10;
  Serial.println(moisture_value);

  if (moisture_value > moisture_threshold || start_water){
    digitalWrite(relay, HIGH);
    Serial.println("Current Flowing");

    if (start_water) {
      delay(start_water_for);
    }
  } else {
    digitalWrite(relay, LOW);
    Serial.println("Current not Flowing");
  }
}

// WiFi Connect

void connect_to_wifi() {
  if (default_network_flag == true) {
    default_network_flag = false;
    
    Serial.print("Connecting to SSID: ");
    Serial.print(NET_SSID);
  
    Serial.print(" with password: ");
    Serial.println(NET_PASS);
    
    // Connect to Wi-Fi
    WiFi.begin(NET_SSID, NET_PASS);
  } else {
    Serial.print("Connecting to SSID: ");
    Serial.print(ssid);
  
    Serial.print(" with password: ");
    Serial.println(password);
    
    // Connect to Wi-Fi
    WiFi.begin(ssid, password);
  }
  
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);

    wifi_connect_retries++;

    if (wifi_connect_retries >= wifi_connect_retries_limit) {
      wifi_connect_retries = 0;
      default_network_count++;
      WiFi.disconnect();
      break;
    }
  }

  if (default_network_count >= default_network_limit) {
    default_network_flag = true;
    default_network_count = 0;
  }

  // When connected
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }
}

// key

int calculate_key(int circle, int square, int triangle) {
  return (int)(((KEY * triangle) / (square * circle)) / (triangle + circle - square));
}

// erase EEPROM memory

void eraseMemory() {
  EEPROM.begin(512);
  // write a 0 to all 512 bytes of the EEPROM
  for (int i = 0; i < 512; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.end();
}
