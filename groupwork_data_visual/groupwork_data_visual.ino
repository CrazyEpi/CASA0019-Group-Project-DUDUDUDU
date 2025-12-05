// =======================
// Include Libraries
// =======================
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFiNINA.h>        
#include <PubSubClient.h>    
#include <Servo.h>           

// =======================
// Wi-Fi Configuration
// =======================
const char* ssid = "CE-Hub-Student";         
const char* password = "casa-ce-gagarin-public-service"; 
int status = WL_IDLE_STATUS;

// =======================
// MQTT Configuration
// =======================
const char* mqtt_server = "mqtt.cetools.org";
const int mqtt_port = 1884;
const char* mqtt_user = "student";
const char* mqtt_pass = "ce2021-mqtt-forget-whale";

const char* TOPIC_LOUDNESS = "student/ucfnjhe/project/loudness";
const char* TOPIC_FREQUENCY = "student/ucfnjhe/project/frequency";
const char* TOPIC_NOTE = "student/ucfnjhe/project/note";

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// =======================
// LCD Configuration
// =======================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// =======================
// Servo Configuration
// =======================
Servo loudServo;      
#define SERVO_PIN 1  

// =======================
// Encoder Pins & Variables
// =======================
#define ENC_CLK 13
#define ENC_DT  A1
#define ENC_SW  A2

int lastCLK = HIGH;
int mode = 0; 
unsigned long lastEncoderTime = 0;
const int debounceDelay = 20; 

// =======================
// Microphone Pin
// =======================
#define MIC_PIN A0

// =======================
// 7-segment Pins
// =======================
int segPins[] = {2, 3, 4, 5, 6, 7, 8, 9};
#define COM_ONES A3
#define COM_TENS A4
#define COM_ON  LOW
#define COM_OFF HIGH

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
// Timing Variables
// =======================
unsigned long t_scan = 0;
unsigned long t_loud = 0;
unsigned long t_freq = 0;
unsigned long t_wave = 0;
unsigned long t_mqtt_publish = 0;
unsigned long t_servo = 0; 
const long PUBLISH_INTERVAL = 500; 

// =======================
// Data Variables
// =======================
int loudness = 0;
float freqValue = 0;

// =======================
// LCD Custom Char (Bar)
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
// Helper: Connect Wi-Fi
// =======================
void setup_wifi() {
  Serial.println();
  if (WiFi.status() == WL_NO_MODULE) {
    lcd.clear();
    lcd.print("WiFi ERROR");
    while (true); 
  }

  while (status != WL_CONNECTED) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Connecting:");
    lcd.setCursor(0,1);
    lcd.print(ssid);
    status = WiFi.begin(ssid, password);
    delay(10000); 
  }
  
  lcd.clear();
  lcd.print("WiFi Connected!");
  delay(1000); 
}

// =======================
// Helper: Reconnect MQTT
// =======================
void reconnect() {
  while (!mqttClient.connected()) {
    lcd.setCursor(0,0);
    lcd.print("MQTT Connect...");
    if (mqttClient.connect("", mqtt_user, mqtt_pass)) { 
      lcd.setCursor(0,1);
      lcd.print("Connected!    ");
      delay(1000);
      lcd.clear();
    } else {
      lcd.setCursor(0,1);
      lcd.print("Retrying...   ");
      delay(5000);
    }
  }
}

// =======================
// Helper: Publish Data
// =======================
void publishData() {
    char loudStr[4]; 
    sprintf(loudStr, "%d", loudness);
    mqttClient.publish(TOPIC_LOUDNESS, loudStr, true);

    String freqString = String(freqValue, 2); 
    mqttClient.publish(TOPIC_FREQUENCY, freqString.c_str(), true);
    
    String note = freqToNote(freqValue);
    mqttClient.publish(TOPIC_NOTE, note.c_str(), true); 
}

// =======================
// Logic: Update Servo
// =======================
void updateServo() {
  int angle = map(loudness, 0, 99, 0, 180);
  angle = constrain(angle, 0, 180);
  loudServo.write(angle);
}

// =======================
// Read Encoder
// =======================
void readEncoder() {
  int clkState = digitalRead(ENC_CLK);
  if (clkState != lastCLK && clkState == LOW && (millis() - lastEncoderTime > debounceDelay)) {
    if (digitalRead(ENC_DT) != clkState) mode++;
    else mode--;
    
    if (mode > 1) mode = 0;
    if (mode < 0) mode = 1;
    
    lcd.clear(); 
    lastEncoderTime = millis(); 
  }
  lastCLK = clkState;
}

// =======================
// Logic: Loudness
// =======================
void computeLoudness() {
  int maxV = 0, minV = 1023;
  unsigned long start = millis();

  while (millis() - start < 30) {
    int v = analogRead(MIC_PIN);
    if (v > maxV) maxV = v;
    if (v < minV) minV = v;
  }
  
  loudness = map(maxV - minV, 0, 512, 0, 99);
  if (loudness > 99) loudness = 99;
}

// =======================
// Logic: Frequency
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

  float best=0; int bestLag=20;
  for (int lag=20; lag<80; lag++){
    float corr=0;
    for (int i=0;i<N-lag;i++) corr += (buf[i]-mean)*(buf[i+lag]-mean);
    if(corr > best){ best = corr; bestLag = lag; }
  }
  return 6600.0 / bestLag;
}

String freqToNote(float f) {
  if(f < 20 || f > 2000) return "---";
  String NAMES[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
  int n = round(12 * log(f/440.0)/log(2)) + 57;
  return NAMES[n % 12] + String(n/12 - 1);
}

// =======================
// Display Helpers
// =======================
void showDigit(int num) {
  for (int i=0;i<8;i++) digitalWrite(segPins[i], digits[num][i]);
}

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

// [UPDATED] 增加了 Loudness 显示
void showFreqNote() {
  lcd.setCursor(0,0);
  lcd.print("Freq:");
  lcd.print(freqValue,0);
  lcd.print("Hz     "); 

  // 第二行显示 Note 和 Loudness
  lcd.setCursor(0,1);
  lcd.print("Note:");
  lcd.print(freqToNote(freqValue));
  lcd.print("   "); // 清除 Note 后面的残留

  // 在固定位置 (第9格) 显示音量
  lcd.setCursor(9, 1);
  lcd.print("Vol:");
  lcd.print(loudness);
  lcd.print(" "); // 清除残留位数
}

void showWaveform() {
  lcd.setCursor(0,0);
  lcd.print("Waveform:       ");
  lcd.setCursor(0,1);
  for(int i=0;i<16;i++){
    int v = analogRead(MIC_PIN);
    int level = map(v,0,1023,0,7);
    lcd.write(byte(level));
  }
}

// =======================
// MAIN SETUP
// =======================
void setup() {
  Serial.begin(115200);

  lcd.init();
  lcd.backlight();
  for(int i=0;i<8;i++) lcd.createChar(i, barChar[i]);

  // Pins
  for(int i=0;i<8;i++) pinMode(segPins[i], OUTPUT);
  pinMode(COM_ONES, OUTPUT);
  pinMode(COM_TENS, OUTPUT);
  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);

  // Servo Setup
  loudServo.attach(SERVO_PIN);
  loudServo.write(0); 

  setup_wifi(); 
  mqttClient.setServer(mqtt_server, mqtt_port);
}

// =======================
// MAIN LOOP
// =======================
void loop() {
  if (!mqttClient.connected()) reconnect();
  mqttClient.loop(); 

  readEncoder();

  unsigned long currentMillis = millis();

  if(currentMillis - t_scan >= 2){
    t_scan = currentMillis;
    scan7Seg();
  }

  if(currentMillis - t_loud >= 80){
    t_loud = currentMillis;
    computeLoudness();
  }

  if(currentMillis - t_freq >= 150){
    t_freq = currentMillis;
    freqValue = detectFreq();
  }
  
  if(currentMillis - t_servo >= 50){
    t_servo = currentMillis;
    updateServo();
  }

  if(currentMillis - t_wave >= 50){
    t_wave = currentMillis;
    if(mode == 0) showFreqNote();
    else          showWaveform();
  }

  if (currentMillis - t_mqtt_publish >= PUBLISH_INTERVAL) {
      t_mqtt_publish = currentMillis;
      publishData(); 
  }
}