# MyPad 单片机固件说明文档

## 1. 硬件平台

| 项目 | 规格 |
|------|------|
| MCU | ESP32-P4NRW32 |
| 屏幕 | 7寸 1024×600 电容触摸屏（MIPI-DSI） |
| 串口 | CH343 USB转串口（GPIO37-TX / GPIO38-RX） |
| WiFi | ESP32-C6 模组（独立，本项目未使用） |
| Flash | 16MB QSPI |
| PSRAM | 32MB |

---

## 2. 固件功能

三个页面，左右滑动切换：

### 第1页：App 启动器
- 6个槽位（2×3 网格）
- 显示应用名称
- 点击发送 `LAUNCH:N` 指令给 PC，PC 启动对应软件

### 第2页：系统 & 行情
- 系统监控：CPU、内存、GPU、磁盘使用率 + 温度 + 网速
- 行情数据：美股/韩股/虚拟货币实时价格 + 涨跌幅
- 数据由 PC 端推送，每2-5秒更新

### 第3页：资讯
- 财经新闻标题（中文翻译）
- 来源：Financial Juice + Yahoo + CNBC
- 每60秒自动更新

---

## 3. 目录结构

```
esp32/
├── CMakeLists.txt          项目配置
├── sdkconfig.defaults      ESP32-P4 编译配置
├── partitions.csv          分区表
└── main/
    ├── CMakeLists.txt      组件配置
    ├── main.c              入口：初始化硬件 + 启动 UI
    ├── ui.c / ui.h         页面管理：导航栏 + 滑动切换
    ├── serial_comm.c / h   串口通信：接收数据 + cJSON 解析
    ├── page_app.c          第1页：App 启动器
    ├── page_monitor.c      第2页：系统 + 行情
    └── page_news.c         第3页：资讯
```

---

## 4. 串口协议

### 4.1 物理参数

| 参数 | 值 |
|------|-----|
| 波特率 | 115200 |
| 数据位 | 8 |
| 停止位 | 1 |
| 校验 | 无 |
| 引脚 | GPIO37(TX) / GPIO38(RX) |
| 芯片 | CH343 USB转串口 |

### 4.2 PC → ESP32 指令

| 指令 | 格式 | 说明 |
|------|------|------|
| 同步开始 | `SYNC_START` | 清空现有配置 |
| 同步结束 | `SYNC_END` | 刷新 UI |
| App 槽位 | `SLOT:1\|应用名` | 设置第 N 个槽位 |
| 系统监控 | `MON:{json}` | CPU/内存/GPU 等数据 |
| 行情数据 | `FINANCE:{json}` | 股票/币种价格 |
| 新闻数据 | `NEWS:{json}` | 新闻标题列表 |
| 超时设置 | `TIMEOUT:30` | 待机超时秒数 |
| 亮度设置 | `BRIGHTNESS:80` | 屏幕亮度百分比 |
| 心跳 | `PING` | 检测设备在线 |

### 4.3 ESP32 → PC 指令

| 指令 | 格式 | 说明 |
|------|------|------|
| 启动应用 | `LAUNCH:1` ~ `LAUNCH:6` | 用户点击了第几个 App |
| 心跳回复 | `PONG` | 响应 PING |
| 设备就绪 | `READY` | 固件启动完成 |

### 4.4 数据格式示例

**MON（系统监控）：**
```json
{
  "cpu_percent": 19.3,
  "cpu_temp": 0,
  "mem_percent": 73.5,
  "mem_used": 6.3,
  "mem_total": 16.0,
  "gpu_percent": 0,
  "gpu_temp": 0,
  "disk_percent": 94.4,
  "net_up": 2.0,
  "net_down": 5.1
}
```

**FINANCE（行情数据）：**
```json
{
  "us_stocks": {
    "AAPL": {
      "name": "Apple Inc.",
      "price": 294.3,
      "change": -0.72,
      "regular_price": 294.3,
      "regular_change": -0.72,
      "pre_price": 297.5,
      "post_price": null,
      "session": "盘前",
      "currency": "USD"
    }
  },
  "kr_stocks": { ... },
  "crypto": {
    "BTC": { "price": 63000, "change": -1.5 }
  }
}
```

**NEWS（新闻数据）：**
```json
[
  {
    "source": "FJ快讯",
    "title": "Trump: US ready to assist Venezuela",
    "title_zh": "特朗普：美国准备好援助委内瑞拉",
    "desc": "...",
    "desc_zh": "...",
    "time": "Thu, 25 Jun 2026 03:45:21 GMT",
    "link": "https://..."
  }
]
```

---

## 5. 编译烧录

### 5.1 安装 ESP-IDF

```bash
# 安装 ESP-IDF v5.4+
mkdir -p ~/esp
cd ~/esp
git clone -b v5.4 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32p4
source export.sh
```

### 5.2 编译

```bash
cd MyPad/esp32
idf.py set-target esp32p4
idf.py build
```

### 5.3 烧录

```bash
# Mac
idf.py -p /dev/cu.usbserial* flash

# Windows
idf.py -p COM3 flash
```

### 5.4 监控日志

```bash
idf.py -p /dev/cu.usbserial* monitor
```

按 `Ctrl+]` 退出监控。

---

## 6. 引脚定义

| 功能 | GPIO | 说明 |
|------|------|------|
| UART TX | 37 | 连接 CH343 RXD |
| UART RX | 38 | 连接 CH343 TXD |
| 触摸 SDA | 3 | 电容触摸 I2C 数据 |
| 触摸 SCL | 4 | 电容触摸 I2C 时钟 |
| 触摸 INT | 2 | 触摸中断信号 |
| 触摸 RST | 5 | 触摸复位 |
| LCD 背光 | 21 | 屏幕背光控制 |
| LCD 复位 | 22 | 屏幕复位 |
| LCD 电源 | 23 | 屏幕电源控制 |
| LED | 53 | 板载蓝色 LED |

---

## 7. 需要适配的部分

板子到手后需要根据实际 SDK 示例修改：

### 7.1 屏幕初始化
LCD 驱动初始化代码需要参考板子厂商的示例。不同厂家的 MIPI-DSI 初始化参数不同。

### 7.2 触摸驱动
I2C 触摸芯片型号需要确认（通常是 GT911 或 CST816），驱动代码需要适配。

### 7.3 LVGL 移植
需要将厂商的 LCD 和触摸驱动对接到 LVGL 的 `display_driver` 和 `indev_driver`。

### 7.4 cJSON
ESP-IDF 自带 cJSON 组件，无需额外安装。

---

## 8. 工作流程

```
上电
  │
  ├── 初始化屏幕 + 触摸
  ├── 初始化串口 (115200)
  ├── 创建 LVGL UI (3页)
  ├── 发送 "READY" 给 PC
  │
  └── 主循环
        ├── lv_timer_handler()  ← 处理 UI + 触摸
        └── 串口接收任务
              ├── 收到 SLOT:N|Name  → 更新 App 槽位
              ├── 收到 MON:{json}   → 更新系统监控
              ├── 收到 FINANCE:{json} → 更新行情
              ├── 收到 NEWS:{json}  → 更新新闻
              ├── 收到 PING         → 回复 PONG
              └── 收到 SYNC_END     → 刷新所有页面

触摸事件
  │
  ├── 左滑 → 切换到下一页
  ├── 右滑 → 切换到上一页
  └── 点击 App 槽位 → 发送 "LAUNCH:N" 给 PC
```

---

## 9. 注意事项

1. **串口缓冲区**：新闻数据较大（~15KB JSON），缓冲区设为 16KB
2. **cJSON 解析**：大 JSON 可能占用较多内存，ESP32-P4 的 32MB PSRAM 足够
3. **LVGL 刷新**：主循环每 10ms 调用一次 `lv_timer_handler()`
4. **屏幕方向**：默认横屏，如需竖屏需修改 LVGL 显示驱动配置
5. **翻译**：PC 端已完成翻译，单片机直接显示中文即可
