

---

# ESP32 智能排卡系统

这是一个基于 ESP32 的智能排队与机台管理系统。它通过 NFC 读取卡片 UID，本地计算排队编号与人数，实时上传至远程后端服务器，LCD屏幕显示主信息，OLED屏幕显示调试信息，适合新手上手学习。本人遇到的问题都会在文档里写出来，作为参考。引脚选择和定义可以根据需要修改。

##  核心功能

1.  **双核心异步架构**（太好了性能最宽裕的一集）：
    *   **Core 1 (逻辑核心)**：负责极速 NFC 读卡、本地排队计数、LCD/OLED 交互，确保刷卡秒响应。
    *   **Core 0 (网络核心)**：负责 WiFi 连接、NTP 网络对时、HTTP 数据上传，网络波动不卡顿。
    * 本项目不连接WiFi也可以实践，请删除或缩短下面代码里面的时间
```
    http.setTimeout(5000);
```
1.  **智能业务逻辑**：
    *   **自动判定**：首次刷卡进入排队，再次刷卡退出。
    *   **每日清零**：每天凌晨 04:00 自动重置所有本地排队数据。
2.  **多维反馈**：
    *   **LCD1602**：显示当前刷卡 ID、总排队人数、实时北京时间。
    *   **OLED 0.91"**：显示实时硬件状态反馈与操作指引。
    *   **蜂鸣器**：上机短鸣，下机长鸣提醒。
3.  **云端同步**：通过 HTTP POST 方式将 JSON 格式数据发送至Web API。

---

##  硬件接线指南

### 1. 传感器

| 模块                     | ESP32 引脚 | 备注       |
| :--------------------- | :------- | :------- |
| **有源蜂鸣器**              | GPIO 15  | 高电平触发    |
| **NFC (RC522) RST**    | GPIO 4   | 复位引脚     |
| **NFC (RC522) SDA/SS** | GPIO 5   | 片选引脚     |
| **NFC (RC522) SCK**    | GPIO 18  | SPI 时钟   |
| **NFC (RC522) MISO**   | GPIO 19  | SPI 主入从出 |
| **NFC (RC522) MOSI**   | GPIO 23  | SPI 主出从入 |

### 2. 显示屏幕

为了防止总线冲突，LCD 和 OLED 挂载在不同的硬件 I2C 通道上：

| 模块                 | ESP32 引脚 (SDA) | ESP32 引脚 (SCL) | 通道    |
| :----------------- | :------------- | :------------- | :---- |
| **LCD1602 (I2C版)** | GPIO 21        | GPIO 22        | Wire0 |
| **OLED (SSD1306)** | GPIO 26        | GPIO 25        | Wire1 |

**注意**：LCD1602 通常需要 **5V** 供电，RC522 严禁接 5V，必须接 **3.3V**。
也可以并联两块屏幕的I2C总线，本人手头没有找到面包板，所以分了两个通道。

---

## 软件环境配置

### 1. 开发环境
*   **IDE**: Arduino IDE 2.x (推荐) 或 VS Code + Arduino Extension。（本人卡在了配置环境上所以用了arduinoIDE）
*   **开发板管理器**: 安装 `esp32` by Espressif 系统库 (推荐版本 2.0.7+)。
* 如果卡在了下载库上，可以去普中官网找离线下载方式，或者使用加速器。

### 2. 依赖库列表
请通过 Arduino 库管理器 (Ctrl+Shift+I) 搜索并安装以下库：

1.  **MFRC522** (by GithubCommunity)
    *   用途：读取 NFC 卡片 UID。
    *   [下载路径](https://github.com/OSSLibraries/Arduino_MFRC522)
2.  **LiquidCrystal I2C**
    *   用途：控制 LCD1602 屏幕。
    *   [下载路径](https://github.com/johnrickman/LiquidCrystal_I2C)
3.  **ESP8266 and ESP32 OLED driver for SSD1306 displays**
    *   用途：控制 OLED 屏幕。
    *   [下载路径](https://github.com/ThingPulse/esp8266-oled-ssd1306)

---

##  部署前设置

在上传代码前，请修改代码顶部的以下变量：

```cpp
const char* ssid       = "your_wifi_name";     // 你的 WiFi 名称
const char* password   = "your_password";      // 你的 WiFi 密码
const char* serverUrl  = "https://ip:port/api/Queue"; // 后端 API 地址
```

如果连接不上，请检查网络是否为2.4G，大部分老型号ESP32开发板只支持2.4G。

## 后端接口规范

系统会向服务器发送 **JSON POST** 请求。示例数据格式如下：

```json
{
  "uid": "A1B2C3D4",
  "mid": "maimai_1",
  "act": "enter",
  "total": 5
}
```

*   `Uid`: 卡片唯一序列号。
*   `Mid`: 机台编号。
*   `Act`: 动作类型 (`enter` 或 `exit`)。
*   `Total`: 刷卡后的本地总排队人数。

---

## 故障排除

1.  **LCD1602 亮起但无内容**：
    *   请旋转 LCD 背面 I2C 转接板上的蓝色电位器来调节对比度。
2.  **设备频繁重启**：
    *   代码中已将网络任务栈设为 `20480` 以防止 HTTPS 握手溢出。(一直内存溢出溢出溢出)
3.  **HTTPS 上传失败**：
    *  请确保服务器防火墙放行了对应端口。内网测试中可以在cmd中选择开放端口或者暂时关闭防火墙。
4.  **OLED 不亮**：
    *   检查引脚是否为 25/26。
    *   代码使用了硬件互斥量 `oledMutex`，确保多核心访问 OLED 时的稳定性。

---
