#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "SSD1306Wire.h"
#include <map>
#include "time.h" 

// --- 1. 用户配置 ---
const char* ssid       = "ciallo";
const char* password   = "1234567890";
const char* serverUrl  = "http://10.94.200.180:5005/api/hospital/upload"; 
String MACHINE_ID      = "maimai_1"; 

// --- 2. 引脚定义 ---
#define BUZZER_PIN 15
#define RST_PIN    4
#define SS_PIN     5

// LCD 使用的 I2C 引脚 (对应默认 Wire)
#define LCD_SDA    21
#define LCD_SCL    22

// OLED 使用的 I2C 引脚 (对应 Wire1)
#define OLED_SDA   26
#define OLED_SCL   25

// --- 3. 业务变量 ---
int queueCount = 0;          
int dailyTempId = 1;         
std::map<String, int> activeQueue; 
int lastMin = -1;            
bool hasResetToday = false;   

// --- 4. 异步上传结构体 ---
struct UploadPacket {
    char uid[16];
    char action[8];
    int qCount;
};
QueueHandle_t uploadQueue; 

// --- 5. 硬件对象 ---
// LCD 默认绑定到硬件 Wire (I2C_0)
LiquidCrystal_I2C lcd(0x27, 16, 2); 

//OLED 声明使用 I2C_TWO (Wire1)
SSD1306Wire oled(0x3c, OLED_SDA, OLED_SCL, GEOMETRY_128_32, I2C_TWO); 

MFRC522 mfrc522(SS_PIN, RST_PIN);  

//新增：OLED 访问互斥量
SemaphoreHandle_t oledMutex;

//功能函数
void beep(int ms) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(ms);
  digitalWrite(BUZZER_PIN, LOW);
}

// 刷新 LCD 显示内容
void refreshLCD(String id) {
  // 第一行：显示卡号
  lcd.setCursor(0, 0);
  lcd.print("ID: ");
  lcd.print(id);
  lcd.print("        "); 

  // 第二行：显示排队人数和时间
  lcd.setCursor(0, 1);
  struct tm timeinfo;
  char buf[17];
  
  if (getLocalTime(&timeinfo)) {
      sprintf(buf, "Q:%02d  Time:%02d:%02d", queueCount, timeinfo.tm_hour, timeinfo.tm_min);
  } else {
      sprintf(buf, "Q:%02d  Time:--:--", queueCount);
  }
  lcd.print(buf);
}

// ======================== 后台任务 (Core 0): 专职负责网络上传 ========================
void taskNetwork(void * pvParameters) {
    WiFi.begin(ssid, password);
    configTime(8 * 3600, 0, "ntp.aliyun.com", "time.asia.apple.com");

    UploadPacket packet;
    while (true) {
        if (xQueueReceive(uploadQueue, &packet, portMAX_DELAY)) {
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[网络] WiFi 未连接，丢弃本次包");
                continue; 
            }

            WiFiClient *client = new WiFiClient;
            if (client) {
                HTTPClient http;
                http.setTimeout(5000); 

                if (http.begin(*client, serverUrl)) {
                    http.addHeader("Content-Type", "application/json");

                    //去除可能存在的隐藏字符
                    String cleanUid = String(packet.uid);
                    cleanUid.trim(); 
                    String cleanAct = String(packet.action);
                    cleanAct.trim(); 

                    // 构造JSON
                    String body = "{\"uid\":\"" + cleanUid + 
                                  "\",\"mid\":\"" + MACHINE_ID + 
                                  "\",\"act\":\"" + cleanAct + 
                                  "\",\"total\":" + String(packet.qCount) + "}";

                    //打印准备发送的原始数据
                    Serial.println("\n===============================");
                    Serial.print("[网络] 发送 JSON: ");
                    Serial.println(body);

                    //执行 POST 请求
                    int code = http.POST(body);
                    
                    //获取服务器的详细回复内容
                    String responseBody = http.getString(); 
                    
                    Serial.printf("[网络] HTTP 状态码: %d\n", code);
                    Serial.print("[网络] 服务器的回复: ");
                    Serial.println(responseBody);
                    Serial.println("===============================\n");

                    // OLED 显示处理
                    if (xSemaphoreTake(oledMutex, portMAX_DELAY)) {
                        oled.clear();
                        oled.setFont(ArialMT_Plain_10);
                        if (code == 200 || code == 201) {
                            oled.drawString(0, 0, "UPLOAD: OK");
                            oled.drawString(0, 16, "Code: " + String(code));
                        } else if (code > 0) {
                            oled.drawString(0, 0, "UPLOAD: REJECTED");
                            oled.drawString(0, 16, "Code: " + String(code));
                        } else {
                            oled.drawString(0, 0, "UPLOAD: ERR");
                            oled.drawString(0, 16, http.errorToString(code));
                        }
                        oled.display();
                        xSemaphoreGive(oledMutex);
                    }
                    http.end();
                }
                delete client; 
            }
        }
        vTaskDelay(100 / portTICK_PERIOD_MS); 
    }
}

// ======================== 初始化 (Core 1) ========================
void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  //创建 OLED 互斥量
  oledMutex = xSemaphoreCreateMutex();

  //1: OLED 静态初始化 (Wire1)
  oled.init();
  oled.flipScreenVertically();
  oled.clear(); 
  // ======================== OLED 显示就绪信息 ========================
  if (xSemaphoreTake(oledMutex, portMAX_DELAY)) {
    oled.drawString(0, 0, "[硬件] OLED 屏幕就绪");
    oled.display();
    xSemaphoreGive(oledMutex);
  }
  delay(1000);
  Serial.println("[硬件] OLED 屏幕就绪");

  //2: LCD 初始化 (Wire)
  Wire.begin(LCD_SDA, LCD_SCL); 
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ID: Ready...");
  lcd.setCursor(0, 1);
  lcd.print("Q:00  Time:--:--");
  // ======================== OLED 显示 LCD 初始化完成 ========================
  if (xSemaphoreTake(oledMutex, portMAX_DELAY)) {
    oled.clear();
    oled.drawString(0, 0, "LCD Initialized");
    oled.display();
    xSemaphoreGive(oledMutex);
  }
  delay(1000);
  Serial.println("LCD (Wire0:21/22) Initialized.");

  //3: 初始化 NFC ---
  SPI.begin();
  mfrc522.PCD_Init();
  
  //4: 启动网络任务 ---
  uploadQueue = xQueueCreate(10, sizeof(UploadPacket));
  xTaskCreatePinnedToCore(taskNetwork, "NetTask", 20480, NULL, 1, NULL, 0);

  // ======================== OLED 显示系统就绪 ========================
  if (xSemaphoreTake(oledMutex, portMAX_DELAY)) {
    oled.clear();
    oled.drawString(0, 0, "System Ready!");
    oled.display();
    xSemaphoreGive(oledMutex);
  }
  Serial.println("System Ready!");
}

// ======================== 主循环 (Core 1) ========================
void loop() {
  // --- A. 定时刷新 LCD 时间与凌晨 4 点清零 ---
  static unsigned long lastTimeCheck = 0;
  if (millis() - lastTimeCheck > 1000) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      if (timeinfo.tm_min != lastMin) {
        refreshLCD("Waiting...");
        lastMin = timeinfo.tm_min;
      }
      
      if (timeinfo.tm_hour == 4 && timeinfo.tm_min == 0) {
        if (!hasResetToday) {
          activeQueue.clear(); 
          queueCount = 0; 
          dailyTempId = 1;
          hasResetToday = true;
          Serial.println("[系统] 凌晨 4:00 数据重置");
          // ======================== OLED 显示重置信息 ========================
          if (xSemaphoreTake(oledMutex, portMAX_DELAY)) {
            oled.clear();
            oled.drawString(0, 0, "Reset at 4:00");
            oled.display();
            xSemaphoreGive(oledMutex);
          }
        }
      } else {
        hasResetToday = false;
      }
    }
    lastTimeCheck = millis();
  }

  // --- B. NFC 读卡检测 ---
  if ( ! mfrc522.PICC_IsNewCardPresent() || ! mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  beep(100);

  // 1. 读取卡号
  String cardID = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    String hexPart = String(mfrc522.uid.uidByte[i], HEX);
    if (mfrc522.uid.uidByte[i] < 0x10) hexPart = "0" + hexPart;
    hexPart.toUpperCase();
    cardID += hexPart;
  }

  // 2. 上下机逻辑判定与数据包组装
  String action = "";
  UploadPacket p;
  memset(&p, 0, sizeof(p)); 
  strncpy(p.uid, cardID.c_str(), sizeof(p.uid) - 1);

  if (activeQueue.count(cardID)) {
    action = "EXIT";
    strncpy(p.action, "exit", sizeof(p.action) - 1);
    activeQueue.erase(cardID);
    beep(500);
    if (queueCount > 0) queueCount--;
  } else {
    action = "ENTER";
    strncpy(p.action, "enter", sizeof(p.action) - 1);
    activeQueue[cardID] = dailyTempId++;
    queueCount++;
  }
  p.qCount = queueCount;

  // 3. 串口实时反馈
  Serial.printf(">> %s | ID: %s | Total: %d\n", action.c_str(), cardID.c_str(), queueCount);

  // 4. 更新 LCD (立即显示最新卡号、人数)
  refreshLCD(cardID);

  // 5. 发送数据给副核心去异步上传
  xQueueSend(uploadQueue, &p, 0);

  // ======================== OLED 显示本次操作 ========================
  if (xSemaphoreTake(oledMutex, portMAX_DELAY)) {
    oled.clear();
    oled.drawString(0, 0, action + " " + cardID);
    oled.drawString(0, 10, "Total: " + String(queueCount));
    oled.display();
    xSemaphoreGive(oledMutex);
  }

  // 6. 停止卡片读取，防止误触
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();

  // 7. 刷卡后短暂停留，防止用户连刷
  delay(500); 
}