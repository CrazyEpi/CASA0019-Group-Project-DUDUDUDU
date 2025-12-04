// =======================
// 库文件引入
// =======================
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFiNINA.h>        // MKR 1010 内置 Wi-Fi 库
#include <PubSubClient.h>    // MQTT 客户端库

// =======================
// Wi-Fi 配置
// =======================
const char* ssid = "CE-Hub-Student";         // <<< 替换为您的 Wi-Fi 名称
const char* password = "casa-ce-gagarin-public-service"; // <<< 替换为您的 Wi-Fi 密码
int status = WL_IDLE_STATUS;

// =======================
// MQTT 配置 (基于您的图片)
// =======================
const char* mqtt_server = "mqtt.cetools.org";
const int mqtt_port = 1884;
const char* mqtt_user = "student";
const char* mqtt_pass = "ce2021-mqtt-forget-whale";
// Client ID 留空，Broker 将自动分配一个临时 ID

// 定义要发布的主题 (Topics)
const char* TOPIC_LOUDNESS = "student/ucfnjhe/project/loudness";
const char* TOPIC_FREQUENCY = "student/ucfnjhe/project/frequency";
const char* TOPIC_NOTE = "student/ucfnjhe/project/note";

// 初始化 Wi-Fi 和 MQTT 客户端
WiFiClient wifiClient; // 使用 WiFiNINA 的客户端
PubSubClient mqttClient(wifiClient);


// =======================
// LCD
// =======================
// 请确保您的 LCD 地址是 0x27
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
// 7-segment pins (基于您的 LED 图)
// =======================
// A B C D E F G DP
int segPins[] = {2, 3, 4, 5, 6, 7, 8, 9};

// Common cathode pins (位选)
#define COM_ONES A3
#define COM_TENS A4

#define COM_ON  LOW
#define COM_OFF HIGH

// Digit map (共阴极: HIGH=亮)
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
const long PUBLISH_INTERVAL = 500; // 每 500ms 发布一次数据

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
// 函数：连接 Wi-Fi (适用于 MKR 1010)
// =======================
void setup_wifi() {
  Serial.println();
  // 检查是否有 WiFi 模块
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    lcd.clear();
    lcd.print("WiFi ERROR");
    while (true); 
  }

  // 尝试连接到 WPA/WPA2 网络
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Connecting:");
    lcd.setCursor(0,1);
    lcd.print(ssid);
    
    // 连接到网络
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
// 函数：连接/重连 MQTT (不使用 Client ID)
// =======================
void reconnect() {
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection (No ID)...");
    lcd.setCursor(0,0);
    lcd.print("MQTT Connect...");
    
    // 连接时 Client ID 使用空字符串 ""
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
  lcd.clear(); // 清屏准备进入主要显示模式
}

// =======================
// 函数：发布数据到 MQTT
// =======================
void publishData() {
    // 1. 发布响度 (Loudness)
    char loudStr[4]; 
    sprintf(loudStr, "%d", loudness);
    mqttClient.publish(TOPIC_LOUDNESS, loudStr, true);

    // 2. 发布频率 (Frequency) - 修正了 dtostrf 错误
    // 使用 String(value, decimalPlaces) 构造函数
    String freqString = String(freqValue, 2); 
    mqttClient.publish(TOPIC_FREQUENCY, freqString.c_str(), true);
    
    // 3. 发布音符 (Note)
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
  
  // 1. 正常映射
  loudness = map(maxV - minV, 0, 512, 0, 99);
  
  // 2. 钳制结果：确保它不超过 99
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
  lcd.print("Hz    ");

  lcd.setCursor(0,1);
  lcd.print("Note:");
  lcd.print(freqToNote(freqValue));
  lcd.print("     ");
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
  setup_wifi(); // 连接 Wi-Fi
  mqttClient.setServer(mqtt_server, mqtt_port); // 设置 MQTT 服务器
}

// =======================
// Loop
// =======================
void loop() {
  // === MQTT/WiFi Loop ===
  if (!mqttClient.connected()) {
      reconnect();
  }
  // 保持 MQTT 客户端连接活跃
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
  // 定时发布数据到 MQTT
  if (millis() - t_mqtt_publish >= PUBLISH_INTERVAL) {
      t_mqtt_publish = millis();
      publishData(); 
  }
}