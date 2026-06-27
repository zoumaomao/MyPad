# MyPad - 便携应用启动屏

7寸电容触摸屏（1024×600）+ ESP32-P4，USB连接PC，滑动切换页面。

## 功能

| 页面 | 功能 |
|------|------|
| 第1页 | 6宫格 App 启动器 |
| 第2页 | 系统监控（CPU/内存/GPU/温度/网速） |
| 第3页 | 财经资讯（中文翻译） |

## 目录结构

```
MyPad/
├── pc/                  # PC端软件
│   ├── server.py        # Python 后端（Flask + 串口）
│   ├── requirements.txt
│   └── web/             # Web 配置界面
│       ├── index.html
│       ├── style.css
│       └── app.js
├── esp32/               # ESP32 固件
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults
│   ├── partitions.csv
│   └── main/
│       ├── main.c
│       ├── ui.c / ui.h
│       ├── serial_comm.c / serial_comm.h
│       ├── page_app.c
│       ├── page_monitor.c
│       └── page_news.c
└── README.md
```

## PC端启动

```bash
cd pc
pip install -r requirements.txt
python server.py
```

浏览器打开 http://127.0.0.1:5800

## ESP32 固件编译烧录

需要安装 ESP-IDF v5.x：

```bash
cd esp32
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
```

## 通信协议

PC → ESP32：
- `SYNC_START` / `SYNC_END` 同步配置
- `SLOT:1|VSCode|icon` 同步槽位
- `MON:{json}` 推送系统数据
- `WALLPAPER:data` 壁纸
- `TIMEOUT:30` 超时秒数
- `BRIGHTNESS:80` 亮度

ESP32 → PC：
- `LAUNCH:1~6` 点击启动应用
- `PING` / `PONG` 心跳
