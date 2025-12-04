// =======================
// Include Libraries
// =======================
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFiNINA.h>        // MKR 1010 built-in Wi-Fi library
#include <PubSubClient.h>    // MQTT Client library

// =======================
// Wi-Fi Configuration
// =======================
const char* ssid = "CE-Hub-Student";         // <<< Replace with your Wi-Fi SSID
const char* password = "casa-ce-gagarin-public-service"; // <<< Replace with your Wi-Fi Password
int status = WL_IDLE_STATUS;

// =======================
// MQTT Configuration (Based on your image)
// =======================
const char* mqtt_server = "mqtt.cetools.org";
const int mqtt_port = 1884;
const char* mqtt_user = "student";
const char* mqtt_pass = "ce2021-mqtt-forget-whale";
// Client ID left empty, Broker will auto-assign a temporary ID

// Define Topics to publish
const char* TOPIC_LOUDNESS = "student/ucfnjhe/project/loudness";
const char* TOPIC_FREQUENCY = "student/ucfnjhe/project/frequency";
const char* TOPIC_NOTE = "student/ucfnjhe/project/note";

// Initialize Wi-Fi and MQTT clients
WiFiClient wifiClient;       // Use WiFiNINA client
PubSubClient mqttClient(wifiClient);


// =======================
// LCD
// =======================
// Please ensure your LCD address is 0x27
LiquidCrystal_I2C lcd(0x27, 16, 2);

// =======================
// Encoder pins
// =======================
#define ENC_CLK 13
#define ENC_DT  A1
#define ENC_SW  A2

int lastCLK = HIGH;
int mode = 0;

// =======================
// Microphone
// =======================
#define MIC_PIN A0

// =======================
// 7-segment pins (Based on your LED diagram)
// =======================
// A B C D E F G DP
int segPins[] = {2, 3, 4, 5, 6, 7, 8, 9};

// Common cathode pins (Digit select)
#define COM_ONES A3
#define COM_TENS A4

#define COM_ON  LOW
#define COM_OFF HIGH

// Digit map (Common Cathode: HIGH=On)
int digits[10][8] = {
  {1,1,1,1,1,1,0,0}, //0
  {0,1,1,0,0,0,0,0}, //1
  {1,1,0,1,1,0,1,0}, //2
  {1,1,1,1,0,0,1,0}, //3
  {0,1,1,0,0,1,1,0}, //4
  {1,0,1,1,0,1,1,0}, //5
  {1,0,1,1,1,1,1,0}, //6
  {1,1,1,0,0,0,0,0}, //7
  {1,1,1,1,1,1,1,0}, //8
  {1,1,1,1,0,1,1,0}  //9
};

// =======================
// Timing
// =======================
unsigned long t_scan = 0;
unsigned long t_loud = 0;
unsigned long t_freq = 0;
unsigned long t_wave = 0;
unsigned long t_mqtt_publish = 0;
const long PUBLISH_INTERVAL = 500; // Publish data every 500ms

// =======================
// Variables
// =======================
int loudness = 0;
float freqValue = 0;

// =======================
// LCD waveform characters
// =======================
byte barChar[8][8] = {
  {0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,31},
  {0,0,0,0,0,0,31,31},
  {0,0,0,0,0,31,31,31},
  {0,0,0,0,31,31,31,31},
  {0,0,0,31,31,31,31,31},
  {0,0,31,31,31,31,31,31},
  {0,31,31,31,31,31,31,31}
};

// =======================
// Function: Connect Wi-Fi (for MKR 1010)
// =======================
void setup_wifi() {
  Serial.println();
  // Check if WiFi module is present
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    lcd.clear();
    lcd.print("WiFi ERROR");
    while (true); 
  }

  // Attempt to connect to WPA/WPA2 network
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Connecting:");
    lcd.setCursor(0,1);
    lcd.print(ssid);
    
    // Connect to network
    status = WiFi.begin(ssid, password);
    delay(10000); 
  }
  
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("WiFi Connected!");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  delay(1000); 
}

// =======================
// Function: Connect/Reconnect MQTT (No Client ID)
// =======================
void reconnect() {
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection (No ID)...");
    lcd.setCursor(0,0);
    lcd.print("MQTT Connect...");
    
    // Use empty string "" for Client ID when connecting
    if (mqttClient.connect("", mqtt_user, mqtt_pass)) { 
      Serial.println("connected (using temporary ID)");
      lcd.setCursor(0,1);
      lcd.print("Connected!");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" trying again in 5 seconds");
      lcd.setCursor(0,1);
      lcd.print("Failed. Retrying.");
      delay(5000);
    }
  }
  lcd.clear(); // Clear screen, ready for main display mode
}

// =======================
// Function: Publish Data to MQTT
// =======================
void publishData() {
    // 1. Publish Loudness
    char loudStr[4]; 
    sprintf(loudStr, "%d", loudness);
    mqttClient.publish(TOPIC_LOUDNESS, loudStr, true);

    // 2. Publish Frequency - Fixed dtostrf error
    // Use String(value, decimalPlaces) constructor
    String freqString = String(freqValue, 2); 
    mqttClient.publish(TOPIC_FREQUENCY, freqString.c_str(), true);
    
    // 3. Publish Note
    String note = freqToNote(freqValue);
    mqttClient.publish(TOPIC_NOTE, note.c_str(), true); 
}


// =======================
// Encoder read
// =======================
void readEncoder() {
  int clkState = digitalRead(ENC_CLK);
  if (clkState != lastCLK) {
    if (digitalRead(ENC_DT) != clkState) mode++;
    else mode--;
    mode = (mode + 2) % 2;
    lcd.clear();
  }
  lastCLK = clkState;
}

// =======================
// Loudness detection
// =======================
void computeLoudness() {
  int maxV = 0, minV = 1023;
  unsigned long start = millis();

  while (millis() - start < 30) {
    int v = analogRead(MIC_PIN);
    if (v > maxV) maxV = v;
    if (v < minV) minV = v;
  }
  
  // 1. Normal mapping
  loudness = map(maxV - minV, 0, 512, 0, 99);
  
  // 2. Clamp result: Ensure it does not exceed 99
  if (loudness > 99) {
    loudness = 99;
  }
}

// =======================
// Frequency detection (autocorrelation)
// =======================
float detectFreq() {
  const int N = 180;
  unsigned int buf[N];

  for (int i = 0; i < N; i++) {
    buf[i] = analogRead(MIC_PIN);
    delayMicroseconds(150);  
  }

  float mean = 0;
  for (int i=0;i<N;i++) mean += buf[i];
  mean /= N;

  float best=0;
  int bestLag=20;

  for (int lag=20; lag<80; lag++){
    float corr=0;
    for (int i=0;i<N-lag;i++)
      corr += (buf[i]-mean)*(buf[i+lag]-mean);

    if(corr > best){
      best = corr;
      bestLag = lag;
    }
  }

  return 6600.0 / bestLag;
}

// =======================
// Note name
// =======================
String freqToNote(float f) {
  if(f < 20 || f > 2000) return "---";
  String NAMES[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
  int n = round(12 * log(f/440.0)/log(2)) + 57;
  return NAMES[n % 12] + String(n/12 - 1);
}

// =======================
// Show one digit
// =======================
void showDigit(int num) {
  for (int i=0;i<8;i++) digitalWrite(segPins[i], digits[num][i]);
}

// =======================
// Scan 7-segment (anti-flicker)
// =======================
void scan7Seg() {
  static bool toggle=false;

  int tens = loudness / 10;
  int ones = loudness % 10;

  if(!toggle) {
    digitalWrite(COM_ONES, COM_OFF);
    digitalWrite(COM_TENS, COM_ON);
    showDigit(tens);
  } else {
    digitalWrite(COM_TENS, COM_OFF);
    digitalWrite(COM_ONES, COM_ON);
    showDigit(ones);
  }

  toggle = !toggle;
}

// =======================
// LCD Display Mode 0 (Freq + Note)
// =======================
void showFreqNote() {
  lcd.setCursor(0,0);
  lcd.print("Freq:");
  lcd.print(freqValue,0);
  lcd.print("Hz     ");

  lcd.setCursor(0,1);
  lcd.print("Note:");
  lcd.print(freqToNote(freqValue));
  lcd.print("     ");
}

// =======================
// LCD Display Mode 1 (Waveform)
// =======================
void showWaveform() {
  lcd.setCursor(0,0);
  lcd.print("Waveform:");

  lcd.setCursor(0,1);
  for(int i=0;i<16;i++){
    int v = analogRead(MIC_PIN);
    int level = map(v,0,1023,0,7);
    lcd.write(byte(level));
  }
}

// =======================
// Setup
// =======================
void setup() {
  Serial.begin(115200);

  lcd.init();
  lcd.backlight();
  
  // Load custom chars
  for(int i=0;i<8;i++)
    lcd.createChar(i, barChar[i]);

  // Segment pins
  for(int i=0;i<8;i++) pinMode(segPins[i], OUTPUT);

  pinMode(COM_ONES, OUTPUT);
  pinMode(COM_TENS, OUTPUT);

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);

  // === MQTT/WiFi Setup ===
  setup_wifi(); // Connect Wi-Fi
  mqttClient.setServer(mqtt_server, mqtt_port); // Set MQTT Server
}

// =======================
// Loop
// =======================
void loop() {
  // === MQTT/WiFi Loop ===
  if (!mqttClient.connected()) {
      reconnect();
  }
  // Keep MQTT client connection active
  mqttClient.loop(); 

  // === User Interface and Signal Processing ===
  readEncoder();

  // 7-seg scan every 2ms
  if(millis() - t_scan >= 2){
    t_scan = millis();
    scan7Seg();
  }

  // Loudness every 80ms
  if(millis() - t_loud >= 80){
    t_loud = millis();
    computeLoudness();
  }

  // Frequency every 150ms
  if(millis() - t_freq >= 150){
    t_freq = millis();
    freqValue = detectFreq();
  }

  // LCD update every 50ms
  if(millis() - t_wave >= 50){
    t_wave = millis();
    if(mode == 0) showFreqNote();
    else          showWaveform();
  }

  // === MQTT Publishing ===
  // Publish data to MQTT periodically
  if (millis() - t_mqtt_publish >= PUBLISH_INTERVAL) {
      t_mqtt_publish = millis();
      publishData(); 
  }
}