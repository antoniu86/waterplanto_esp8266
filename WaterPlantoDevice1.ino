#include "ESP8266WiFi.h"
#include "EEPROM.h"
#include "ESP8266HTTPClient.h"
#include "ArduinoJson.h"
#include "DHT.h"
#include "WiFiClientSecure.h"

// CONSTANTS

// SERIAL NUMBER
#define SERIAL_NUMBER                 "WP-0001-0001"

// KEY
#define KEY                           10021986

// retea
#define NET_SSID                      "waterplanto"
#define NET_PASS                      "password"
#define ENDPOINT_DOMAIN               "https://waterplanto.herokuapp.com"
#define ENDPOINT_UPDATE               "/device_api/update"

// umiditate a solului
#define MOISTURE_THRESHOLD            20 // in percent; 0% - 100% humidity => 430 - 120
#define POMP_RUNNING_TIME             5 // 5 seconds

// temperatura
#define DHTTYPE                       DHT22

// VARIABLES

// http
String update_endpoint =              ENDPOINT_DOMAIN + (String)ENDPOINT_UPDATE;

// network
const int addr_ssid =                 0;  // ssid index
const int addr_ssid_size =            20; // ssid index size
const int addr_password =             20; // password index
const int addr_password_size =        20; // password index size
const int addr_moisture =             40; // humidity index
const int addr_pomp =                 41; // pomp running time

String ssid =                         NET_SSID;
String password =                     NET_PASS;

int wifi_connect_retries =            0;
int wifi_connect_retries_limit =      20;
int default_network_count =           0;
int default_network_limit =           2;
bool default_network_flag =           false;

// pins
const int moisture_Pin =              0;
const int nivel_apa =                 1;
const int relay =                     5;
const int DHTPin =                    4; 

// request
const int request_timing =            15; // request every 15 seconds
int request_timing_start =            12;
int request_timing_count =            request_timing_start;

// sensors
// soil
int moisture_value =                  0;
int moisture_percent =                100;
int moisture_threshold =              MOISTURE_THRESHOLD;
const int moisture_threshold_max =    50; // in percent
const int moisture_max =              130; // max humidity - immersed in watter
const int moisture_min =              430; // min humidity - perfectly dry

// pomp
bool start_pomp =                     false;
int pomp_running_time =               POMP_RUNNING_TIME;

bool deep_sleep_enter =               false;
int deep_sleep_count =                1;
const int deep_sleep_enter_count =    30;
const int sleep_time =                (5 * 60 * 1000);

// DHT22 variables
float temperature;
float humidity;

// initialize
DHT dht(DHTPin, DHTTYPE);

HTTPClient http;
WiFiClientSecure client;

void setup() {
  Serial.begin(115200);

//  // power soil sensor
//  pinMode(0, OUTPUT);
//  digitalWrite(0, HIGH);
//
//  // power relay
//  pinMode(2, OUTPUT);
//  digitalWrite(2, HIGH);
//
//    // power DHT22
//  pinMode(14, OUTPUT);
//  digitalWrite(14, HIGH);

  // pinul pentru releu
  pinMode(relay, OUTPUT);

  // pinul pentru temperatura
  pinMode(DHTPin, INPUT);

  dht.begin();

  delay(100);

  WiFi.mode(WIFI_STA);

  Serial.println("");
  Serial.println("Read from EEPROM");

  readMemory();
}

void loop() {
  Serial.print("deep_sleep_count = ");
  Serial.println(deep_sleep_count);
  
  Serial.print("SSID = ");
  Serial.println(ssid);

  Serial.print("Password = ");
  Serial.println(password);

  Serial.print("Moisture = ");
  Serial.println(moisture_threshold);

  Serial.print("Pomp running time = ");
  Serial.println(pomp_running_time);
  
  if (WiFi.status() != WL_CONNECTED) {
    connect_to_wifi();
  } else {
    checkMoisture();
    checkTemperature();
    
    if (request_timing_count >= request_timing) {
      server_request();
      request_timing_count = 0;
    } else {
      request_timing_count += 1;
    }

    default_network_count = 0;
  }

  // Power saving - enter deep sleep
  if (deep_sleep_enter) {
//    digitalWrite(0, LOW);
//    digitalWrite(2, LOW);
//    digitalWrite(14, LOW);
//
//    //WiFi.forceSleepBegin();
//    wifi_set_sleep_type(LIGHT_SLEEP_T);
//
//    Serial.println("sleep");
//
//    delay(5000);
//
//    Serial.println("wake up");
//
//    //WiFi.forceSleepWake();
//    wifi_set_sleep_type(NONE_SLEEP_T);
//
//    digitalWrite(0, HIGH);
//    digitalWrite(2, HIGH);
//    digitalWrite(14, HIGH);
    
    Serial.println("Enter Deep Sleep");
    ESP.deepSleep(300e6); // 5 * 60 seconds => 5 minutes
  } else {
    deep_sleep_count++;
  }
 
  if (deep_sleep_count == deep_sleep_enter_count) {
    deep_sleep_enter = true;
  }

  Serial.println("------------------------------------------");

  delay(1000);
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

  client.setInsecure();
  
  http.begin(client, update_endpoint);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("Token", "Key " + (String)key);
  
  int httpResponseCode = http.POST("code=" + (String)SERIAL_NUMBER + "&ci=" + circle + "&sq=" + square + "&tr=" + triangle + "&soil=" + moisture_percent + "&temperature=" + temperature + "&humidity=" + humidity);

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

void readMoisture() {
  moisture_value = analogRead(moisture_Pin);
  moisture_percent = abs((((double)(moisture_value - moisture_max) / (moisture_min - moisture_max)) * 100) - 100);

  Serial.print("Soil humidity: ");
  Serial.println(moisture_value);

  Serial.print("Soil humidity %: ");
  Serial.println(moisture_percent);
}

void pompStart() {
  digitalWrite(relay, HIGH);
  Serial.println("Pomp Start - Current Flowing");
}

void pompStop() {
  digitalWrite(relay, LOW);
  Serial.println("Pomp Stop - Current not Flowing");
}

void checkMoisture() {
  if (start_pomp) {
    // manual start
    Serial.println("Start water pomp");
    Serial.print("For (seconds) = ");
    Serial.println(pomp_running_time);

    pompStart();
    
    delay(pomp_running_time * 1000);
    Serial.println("Water stop and resume execution");
    start_pomp = false;

    pompStop();
  } else {
    readMoisture();
    
    // autostart if condition
    if (moisture_percent != 0 && moisture_percent < moisture_threshold){
      pompStart();
  
      // block code in this loop
      while (moisture_percent < moisture_threshold) {
        readMoisture();
        delay(500);
      }
  
      pompStop();
    }
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

// read variable from EEPROM

void readMemory() {
  EEPROM.begin(512);

  // set ssid and password if needed
  String ssid_read;
  String password_read;
  
  for (int k = addr_ssid; k < addr_ssid + addr_ssid_size; k++) {
    ssid_read += char(EEPROM.read(k));
  }
  
  for (int l = addr_password; l < addr_password + addr_password_size; l++) {
    password_read += char(EEPROM.read(l));
  }
  
  if ((ssid_read != "" && ssid_read != ssid) && (password_read != "" && password_read != password)) {
    ssid = ssid_read;
    password = password_read;
  }

  // set moisture threashold
  int moisture_threshold_read = EEPROM.read(addr_moisture);
  
  if (moisture_threshold_read) {
    moisture_threshold = (int)moisture_threshold_read;
  }

  // set pomp running time
  int pomp_running_time_read = EEPROM.read(addr_pomp);
  
  if (pomp_running_time_read) {
    pomp_running_time = (int)pomp_running_time_read;
  }

  EEPROM.end();
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
