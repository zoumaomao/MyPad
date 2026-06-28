# MyPad - 智能副屏

7寸电容触摸屏（1024×600）+ ESP32-P4，USB 连接 PC，四页面滑动切换。

## 功能

| 页面 | 功能 |
|------|------|
| 第1页 | **App 启动器** — 6宫格快捷启动，自动扫描本机应用，一键打开 |
| 第2页 | **系统监控** — CPU/内存/GPU/温度/网速 + 自选股/加密货币实时行情 |
| 第3页 | **财经资讯** — Yahoo Finance + CNBC + FJ快讯（Playwright 实时抓取）+ 经济日历 (US/JP/KR) |
| 第4页 | **壁纸时钟** — 时间/日期/天气，支持自定义壁纸，永不熄屏 |

## 特性

- 🔄 **实时快讯** — Playwright 控制 Chrome 抓取 Financial Juice 网站，延迟 ≤60s
- 📅 **经济日历** — 自动筛选美/日/韩三国事件，中文翻译
- 💹 **行情监控** — yfinance + Binance WebSocket 实时推送，支持盘前盘后
- 🌐 **SSE 推送** — 行情和新闻通过 Server-Sent Events 实时推送到 Web 界面
- 📡 **MCU 同步** — 串口自动同步应用配置、快讯、日历、行情
- 🎨 **自定义壁纸** — Web 端上传 JPEG，串口传输到 MCU
- ⏱️ **智能待机** — 可配置超时熄屏，第4页永不熄屏
- 🖥️ **跨平台** — 支持 macOS 和 Windows

## 目录结构

```
MyPad/
├── pc/                  # PC 端服务
│   ├── server.py        # Flask 后端（串口 + Playwright + 行情 + 新闻）
│   ├── requirements.txt
│   └── web/             # Web 配置界面
│       ├── index.html
│       ├── style.css
│       └── app.js
├── esp32/               # ESP32 固件 (ESP-IDF v5.4)
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults
│   ├── partitions.csv
│   └── main/
│       ├── main.c           # 入口：显示、串口、SD卡初始化
│       ├── ui.c / ui.h      # LVGL UI 框架：页面切换、熄屏、触摸手势
│       ├── serial_comm.c/h  # 串口通信：JSON 解析、文件传输
│       ├── display.c/h      # 显示驱动 (EK79007) + GT911 触摸
│       ├── page_app.c       # 第1页：App 启动器
│       ├── page_monitor.c   # 第2页：系统监控 + 行情
│       ├── page_news.c      # 第3页：资讯 + 快讯 + 日历
│       ├── page_clock.c     # 第4页：壁纸时钟
│       └── fonts/           # 内置字库
└── README.md
```

## 快速开始

### PC 端

```bash
cd pc
pip install -r requirements.txt
playwright install chromium
python server.py
```

浏览器打开 http://127.0.0.1:5800

**Windows 额外步骤：**
```cmd
set CHROME_PATH=C:\Program Files\Google\Chrome\Application\chrome.exe
```

### ESP32 固件编译烧录

需要 ESP-IDF v5.4+：

```bash
cd esp32
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/cu.usbmodem* flash
```

### TF 卡（可选，用于完整字库和壁纸）

格式：FAT32。文件放根目录：
- `font.bin` — LVGL 二进制字库（覆盖 20,992 个 CJK 汉字 + 符号），[生成方法见 wiki](#)
- `wallpaper.jpg` — 壁纸 (1024×600)

不插 TF 卡也能用，内置字库覆盖常用字。

## 通信协议

### PC → ESP32

| 命令 | 格式 | 说明 |
|------|------|------|
| SYNC_START / SYNC_END | 纯文本 | 同步开始/结束标记 |
| SLOT | `SLOT:1\|VSCode` | 同步应用槽位 |
| ICON | `ICON:0:base64...` | 应用图标 (RGB565, 80×80) |
| MON | `MON:{json}` | 系统监控数据 |
| FINANCE | `FINANCE:{json}` | 自选股行情 |
| NEWS | `NEWS:{json}` | 新闻列表 |
| CALENDAR | `CALENDAR:{json}` | 经济日历 |
| ENV | `ENV:{json}` | 时间/日期/天气 |
| TIMEOUT | `TIMEOUT:30` | 待机超时（秒） |
| BRIGHTNESS | `BRIGHTNESS:80` | 亮度（%） |
| FILE_START/DATA/END | 带 ACK | 文件传输协议 |

### ESP32 → PC

| 命令 | 格式 | 说明 |
|------|------|------|
| LAUNCH | `LAUNCH:1~6` | 点击启动应用 |
| PING / PONG | 纯文本 | 心跳 |
| READY | 纯文本 | 启动完成 |
| FILE_ACK / FILE_ERR | 纯文本 | 文件传输应答 |

## 技术栈

- **PC**: Python Flask + Playwright + yfinance + Binance WS + cloudscraper + pyserial
- **MCU**: ESP-IDF + LVGL + cJSON + FreeRTOS
- **显示**: EK79007 7寸 1024×600 + GT911 电容触摸
- **通信**: UART (CH343) 115200bps
