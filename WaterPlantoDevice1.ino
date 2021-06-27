#include "ESP8266WiFi.h"
#include "EEPROM.h"
#include "ESP8266HTTPClient.h"
#include "ArduinoJson.h"
#include "DHT.h"

// CONSTANTS

// SERIAL NUMBER
#define SERIAL_NUMBER               "WP-0001-0001"

// KEY
#define KEY                         10021986

// retea
#define NET_SSID                    "waterplanto"
#define NET_PASS                    "password"
#define ENDPOINT_DOMAIN             "http://waterplanto.herokuapp.com"
#define ENDPOINT_UPDATE             "/device_api/update"

// umiditate a solului
#define MOISTURE_THRESHOLD          20 // in percent; 0% - 100% humidity => 430 - 120
#define POMP_RUNNING_TIME           5 // 5 seconds

// temperatura
#define DHTTYPE                     DHT22

// VARIABLES

// http
String update_endpoint =            ENDPOINT_DOMAIN + (String)ENDPOINT_UPDATE;

// network
const int addr_ssid =               0;  // ssid index
const int addr_ssid_size =          20; // ssid index size
const int addr_password =           20; // password index
const int addr_password_size =      20; // password index size
const int addr_moisture =           40; // humidity index
const int addr_pomp =               41; // pomp running time

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
const int DHTPin =                  4; 

// request
const int request_timing =          15; // request every 15 seconds
int request_timing_start =          12;
int request_timing_count =          request_timing_start;

// sensors
// soil
int moisture_value =                0;
int moisture_percent =              100;
int moisture_threshold =            MOISTURE_THRESHOLD;
const int moisture_threshold_max =  50; // in percent
const int moisture_max =            130; // max humidity - immersed in watter
const int moisture_min =            430; // min humidity - perfectly dry

// pomp
bool start_pomp =                   false;
int pomp_running_time =             POMP_RUNNING_TIME;

// DHT22 variables
float temperature;
float humidity;

// initialize
DHT dht(DHTPin, DHTTYPE);
HTTPClient http;
WiFiClient client;

void setup() {
  Serial.begin(115200);

  // pinul pentru releu
  pinMode(relay, OUTPUT);

  // pinul pentru temperatura
  pinMode(DHTPin, INPUT);

  dht.begin();

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
  int pomp_running_time_read = EEPROM.read(addr_pomp);
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
    Serial.println("Moisture threshold from EEPROM different");
    Serial.print("Moisture threshold: " + moisture_threshold);
    
    moisture_threshold = (int)moisture_threshold_read;

    Serial.println(" / NEW Moisture threshold (taken from EEPROM): " + moisture_threshold);
  }

  // set pomp running time
  if (pomp_running_time_read) {
    Serial.println("Pomp running time from EEPROM different");
    Serial.print("Pomp running time: " + pomp_running_time);
    
    pomp_running_time = (int)pomp_running_time_read;

    Serial.println(" / NEW Pomp running time (taken from EEPROM): " + pomp_running_time);
  }

  WiFi.mode(WIFI_STA);
}

void loop() {
  //wifi_set_sleep_type(NONE_SLEEP_T);
  //delay(100);
  
  Serial.print("SSID = ");
  Serial.println(ssid);

  Serial.print("Password = ");
  Serial.println(password);

  Serial.print("Moisture = ");
  Serial.println(moisture_threshold);

  Serial.print("Pomp running time = ");
  Serial.println(pomp_running_time);

  checkMoisture();
  checkTemperature();
  
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
  }

  Serial.println("------------------------------------------");

  delay(1000);
 
  //wifi_set_sleep_type(LIGHT_SLEEP_T);
  //delay(4900);

  // put the board to sleep for 5 minutes
  // ESP.deepSleep(5e6);
}

// request

void server_request() {
  bool disconnect_from_network = false;
  String payload = http_request_update();

  if (payload != "{}") {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);
    
    Serial.print("payload: ");
    Serial.println(payload);
  
    int status = doc["status"];
  
    Serial.print("status: ");
    Serial.println(status);
  
    if (status == 1) {
      // save netword ssid if needed
      String netname = doc["netname"];
  
      Serial.print("netname: ");
      Serial.println(netname);
      
      if (netname != "null" && netname != ssid) {
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
    
      // save network password if needed
      String netpass = doc["netpass"];
  
      Serial.print("netpass: ");
      Serial.println(netpass);
      
      if (netpass != "null" && netpass != password) {
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
    
      // disconnect and reconnect to network if one of the above was changed
      if (disconnect_from_network == true) {
        WiFi.disconnect();
        connect_to_wifi();
      }
    
      // save moisture threshold if needed
      int limit = doc["limit"];
      
      if (limit != moisture_threshold && limit <= moisture_threshold_max) {
        moisture_threshold = limit;
    
        EEPROM.begin(512);
        EEPROM.put(addr_moisture, moisture_threshold);
        EEPROM.end();
      }
  
      // save pomp running time if needed
      int duration = doc["duration"];
      
      if (duration != pomp_running_time) {
        pomp_running_time = duration;
    
        EEPROM.begin(512);
        EEPROM.put(addr_pomp, pomp_running_time);
        EEPROM.end();
      }
    
      // update pomp flag if needed
      bool pomp = doc["pomp"];
    
      if (pomp == true) {
        start_pomp = pomp;
      }
    }
  }
}

String http_request_update() {
  int circle = random(1, 10);
  int square = random(11, 20);
  int triangle = random(21, 30);
  int key = calculate_key(circle, square, triangle);
  
  http.begin(client, update_endpoint + "?code=" + (String)SERIAL_NUMBER + "&ci=" + circle + "&sq=" + square + "&tr=" + triangle + "&soil=" + moisture_percent + "&temperature=" + temperature + "&humidity=" + humidity);
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
    Serial.printf("[HTTP] GET failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
  }
  
  http.end();

  return payload;
}

// soil humidity

void checkMoisture() {
  moisture_value = analogRead(moisture_Pin);
  moisture_percent = abs((((double)(moisture_value - moisture_max) / (moisture_min - moisture_max)) * 100) - 100);
  
  Serial.print("Soil humidity: ");
  Serial.println(moisture_value);

  Serial.print("Soil humidity %: ");
  Serial.println(moisture_percent);

  if (start_pomp) {
    Serial.println("Start water pomp");
    Serial.print("For (seconds) = ");
    Serial.println(pomp_running_time);
    digitalWrite(relay, HIGH);
    delay(pomp_running_time * 1000);
    Serial.println("Water stop and resume execution");
    start_pomp = false;
    digitalWrite(relay, LOW);
  }

  if (moisture_percent < moisture_threshold){
    digitalWrite(relay, HIGH);
    Serial.println("Current Flowing");
  } else {
    digitalWrite(relay, LOW);
    Serial.println("Current not Flowing");
  }
}

// temperature

void checkTemperature() {
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();

  Serial.print("Temperature = ");
  Serial.println(temperature);

  Serial.print("Humidity = ");
  Serial.println(humidity);
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

    if (wifi_connect_retries % 4 == 0) {
      checkMoisture();
    }

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
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    request_timing_count = request_timing_start;
  } else {
    Serial.println("");
    Serial.println("WiFi NOT connected !!!");
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
