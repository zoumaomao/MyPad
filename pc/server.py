import json
import os
import threading
import time
from datetime import datetime
import platform
import subprocess
import base64
import plistlib
import tempfile
import urllib.request
import urllib.parse
import xml.etree.ElementTree as ET
import websocket
from pathlib import Path
from io import BytesIO

import psutil
from PIL import Image
from flask import Flask, jsonify, request, send_from_directory, Response
from flask_cors import CORS

try:
    import serial
    import serial.tools.list_ports
    HAS_SERIAL = True
except ImportError:
    HAS_SERIAL = False

BASE_DIR = Path(__file__).resolve().parent
CONFIG_PATH = BASE_DIR / "config.json"
WEB_DIR = BASE_DIR / "web"

app = Flask(__name__, static_folder=str(WEB_DIR))
CORS(app, origins=["http://127.0.0.1:5800", "http://localhost:5800"])

ser = None
serial_lock = threading.Lock()
running = True

OS_NAME = platform.system()


def get_cpu_model():
    try:
        r = subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"],
                           capture_output=True, text=True, timeout=2)
        if r.returncode == 0 and r.stdout.strip():
            return r.stdout.strip()
    except Exception:
        pass
    return platform.processor() or "Unknown"


CPU_MODEL = get_cpu_model()

monitor_data = {
    "cpu_percent": 0, "cpu_temp": 0, "cpu_model": CPU_MODEL,
    "mem_percent": 0, "mem_used": 0, "mem_total": 0,
    "gpu_percent": 0, "gpu_temp": 0,
    "disk_percent": 0, "disk_used": 0, "disk_total": 0,
    "net_up": 0, "net_down": 0,
}


def scan_installed_apps():
    """扫描已安装应用，返回 {app_name: app_path} 字典"""
    apps = {}
    if OS_NAME == "Darwin":
        for d in ["/Applications", str(Path.home() / "Applications")]:
            p = Path(d)
            if p.exists():
                for app in p.glob("*.app"):
                    name = app.stem
                    apps[name.lower()] = str(app)
    elif OS_NAME == "Windows":
        import glob
        dirs = [
            os.environ.get("ProgramFiles", "C:\\Program Files"),
            os.environ.get("ProgramFiles(x86)", "C:\\Program Files (x86)"),
            str(Path.home() / "AppData" / "Local" / "Programs"),
            str(Path.home() / "AppData" / "Roaming" / "Microsoft" / "Windows" / "Start Menu" / "Programs"),
        ]
        for d in dirs:
            for exe in glob.glob(os.path.join(d, "**/*.exe"), recursive=True):
                name = Path(exe).stem
                if name.lower() not in apps:
                    apps[name.lower()] = exe
    return apps


# 常用应用别名映射: 别名 -> 实际应用名
APP_ALIASES = {
    "vscode": ["visual studio code", "code"],
    "chrome": ["google chrome", "chrome"],
    "safari": ["safari"],
    "firefox": ["firefox"],
    "terminal": ["terminal", "终端"],
    "wechat": ["wechat", "微信", "weixin"],
    "feishu": ["feishu", "飞书", "lark"],
    "qq": ["qq"],
    "finder": ["finder", "访达"],
    "edge": ["microsoft edge", "edge"],
    "word": ["microsoft word", "word"],
    "excel": ["microsoft excel", "excel"],
    "ppt": ["microsoft powerpoint", "powerpoint"],
    "notes": ["notes", "备忘录"],
    "music": ["music", "音乐", "apple music"],
    "photos": ["photos", "照片"],
    "settings": ["system preferences", "system settings", "系统设置", "设置"],
    "whatsapp": ["whatsapp"],
    "telegram": ["telegram"],
    "slack": ["slack"],
    "discord": ["discord"],
    "spotify": ["spotify"],
    "youtube": ["youtube"],
    "netflix": ["netflix"],
    "zoom": ["zoom"],
    "teams": ["microsoft teams", "teams"],
    "obsidian": ["obsidian"],
    "notion": ["notion"],
}

# 缓存已安装应用
_installed_apps_cache = {}


def find_app_path(app_name):
    """根据应用名查找实际路径"""
    global _installed_apps_cache
    if not _installed_apps_cache:
        _installed_apps_cache = scan_installed_apps()

    name_lower = app_name.lower().strip()

    # 直接匹配
    if name_lower in _installed_apps_cache:
        return _installed_apps_cache[name_lower]

    # 别名匹配
    for alias, names in APP_ALIASES.items():
        if name_lower == alias or name_lower in names:
            for n in names:
                if n.lower() in _installed_apps_cache:
                    return _installed_apps_cache[n.lower()]

    # 模糊匹配
    for installed_name, path in _installed_apps_cache.items():
        if name_lower in installed_name or installed_name in name_lower:
            return path

    return ""


def load_config():
    if CONFIG_PATH.exists():
        cfg = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
        # 兼容旧配置: 统一字段名，自动补全路径
        for slot in cfg.get("slots", []):
            if slot.get("name") and not slot.get("app_name"):
                slot["app_name"] = slot["name"]
            if slot.get("app_name") and not slot.get("name"):
                slot["name"] = slot["app_name"]
            if slot.get("app_name") and not slot.get("app_path"):
                slot["app_path"] = find_app_path(slot["app_name"])
        # 兼容旧配置: 补全 watchlist
        if "watchlist" not in cfg:
            cfg["watchlist"] = {"us_stocks": [], "kr_stocks": [], "crypto": []}
        wl = cfg["watchlist"]
        if "us_stocks" not in wl:
            wl["us_stocks"] = wl.pop("stocks", [])
        if "kr_stocks" not in wl:
            wl["kr_stocks"] = []
        return cfg
    return {
        "slots": [
            {"id": i + 1, "name": "", "app_name": "", "app_path": "", "icon": ""}
            for i in range(6)
        ],
        "wallpaper": "",
        "timeout": 30,
        "brightness": 80,
        "serial_port": "",
        "baud_rate": 115200,
        "watchlist": {"us_stocks": [], "kr_stocks": [], "crypto": []},
    }


def save_config(cfg):
    CONFIG_PATH.write_text(json.dumps(cfg, indent=2, ensure_ascii=False), encoding="utf-8")


def get_config():
    return load_config()


def launch_app(app_name):
    """根据应用名启动应用"""
    if not app_name:
        return False

    app_path = find_app_path(app_name)
    if not app_path:
        print(f"App not found: {app_name}")
        return False

    # 安全校验：路径必须在允许的目录下
    allowed_prefixes = ["/Applications", str(Path.home() / "Applications"),
                        os.environ.get("ProgramFiles", "C:\\Program Files"),
                        os.environ.get("ProgramFiles(x86)", "C:\\Program Files (x86)"),
                        str(Path.home() / "AppData")]
    if not any(app_path.startswith(p) for p in allowed_prefixes if p):
        print(f"Blocked launch (path not allowed): {app_path}")
        return False

    try:
        if OS_NAME == "Darwin":
            if app_path.endswith(".app"):
                subprocess.Popen(["open", app_path])
            else:
                subprocess.Popen(["open", "-a", app_path])
        elif OS_NAME == "Windows":
            subprocess.Popen(["start", "", app_path], shell=False)
        else:
            subprocess.Popen([app_path])
        print(f"Launched: {app_name} -> {app_path}")
        return True
    except Exception as e:
        print(f"Launch error: {e}")
        return False


def list_serial_ports():
    if not HAS_SERIAL:
        return []
    return [
        {"device": p.device, "description": p.description}
        for p in serial.tools.list_ports.comports()
    ]


def extract_app_icon(app_path):
    """提取应用图标，返回 base64 PNG"""
    if not app_path:
        return ""

    try:
        if OS_NAME == "Darwin" and app_path.endswith(".app"):
            return _extract_mac_icon(app_path)
        elif OS_NAME == "Windows":
            return _extract_win_icon(app_path)
    except Exception as e:
        print(f"Icon extraction error: {e}")
    return ""


def _extract_mac_icon(app_path):
    """macOS: 从 .app 包提取图标"""
    info_plist = Path(app_path) / "Contents" / "Info.plist"
    if not info_plist.exists():
        return ""

    with open(info_plist, "rb") as f:
        plist = plistlib.load(f)

    icon_name = plist.get("CFBundleIconFile", "")
    if not icon_name:
        return ""

    # 尝试不同扩展名
    resources = Path(app_path) / "Contents" / "Resources"
    icon_path = None
    for ext in ["", ".icns", ".png"]:
        candidate = resources / f"{icon_name}{ext}"
        if candidate.exists():
            icon_path = candidate
            break

    if not icon_path:
        return ""

    if icon_path.suffix == ".icns":
        # 用 sips 转换 icns -> png
        with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as tmp:
            tmp_path = tmp.name
        subprocess.run(
            ["sips", "-s", "format", "png", "-z", "128", "128", str(icon_path), "--out", tmp_path],
            capture_output=True, timeout=5
        )
        with open(tmp_path, "rb") as f:
            data = f.read()
        Path(tmp_path).unlink(missing_ok=True)
        return base64.b64encode(data).decode()
    else:
        with open(icon_path, "rb") as f:
            data = f.read()
        return base64.b64encode(data).decode()


def _extract_win_icon(app_path):
    """Windows: 从 .exe 提取图标 (需要 pywin32)"""
    try:
        import win32api
        import win32ui
        import win32gui
        import win32con

        large, small = win32gui.ExtractIconEx(app_path, 0)
        if not large:
            return ""

        hicon = large[0]
        hdc = win32ui.CreateDCFromHandle(win32gui.GetDC(0))
        bmp = win32ui.CreateBitmap()
        bmp.CreateCompatibleBitmap(hdc, 128, 128)
        hdc.SelectObject(bmp)
        hdc.DrawIcon((0, 0), hicon)

        bmpinfo = bmp.GetInfo()
        bmpstr = bmp.GetBitmapBits(True)

        img = Image.frombuffer("RGBA", (bmpinfo["bmWidth"], bmpinfo["bmHeight"]), bmpstr, "raw", "BGRA", 0, 1)
        img = img.resize((128, 128), Image.LANCZOS)

        buf = BytesIO()
        img.save(buf, format="PNG")
        win32gui.DestroyIcon(hicon[0])

        return base64.b64encode(buf.getvalue()).decode()
    except ImportError:
        return ""


def serial_connect(port, baud=115200):
    global ser
    if not HAS_SERIAL:
        return False
    try:
        if ser and ser.is_open:
            ser.close()
        ser = serial.Serial(port, baud, timeout=2)
        # 发送握手包验证设备
        ser.write(b"PING\n")
        time.sleep(0.5)
        response = ser.readline().decode("utf-8", errors="ignore").strip()
        if "PONG" in response:
            print(f"Connected to MyPad device on {port}")
            return True
        else:
            # 没收到 PONG，可能不是我们的设备，但还是连上了串口
            print(f"Connected to {port} (no PONG response, might not be MyPad)")
            return True
    except Exception as e:
        print(f"Serial connect error: {e}")
        if ser and ser.is_open:
            ser.close()
        ser = None
        return False


def serial_send(msg):
    global ser
    with serial_lock:
        if ser and ser.is_open:
            try:
                data = (msg + "\n").encode("utf-8")
                # Chunk to prevent ESP32 USB-JTAG FIFO overflow
                for i in range(0, len(data), 128):
                    ser.write(data[i:i+128])
                    ser.flush()
                    time.sleep(0.005)
                return True
            except Exception:
                pass
    return False


def serial_read_loop():
    global ser
    buf = ""
    while running:
        # Avoid holding serial_lock during read to prevent blocking serial_send
        s = ser
        if s and s.is_open:
            try:
                waiting = s.in_waiting
                if waiting > 0:
                    data = s.read(waiting)
                    if data:
                        buf += data.decode("utf-8", errors="ignore")
                        while "\n" in buf:
                            line, buf = buf.split("\n", 1)
                            line = line.strip()
                            if line:
                                print(f"MCU: {line}", flush=True)
                                if line.startswith("LAUNCH:"):
                                    slot_id = line.split(":")[1].strip()
                                    handle_launch(int(slot_id))
                                elif line == "FILE_ACK":
                                    file_ack_event.set()
                                elif line == "FILE_ERR":
                                    print("MCU reported FILE_ERR")
            except Exception:
                pass
        time.sleep(0.05)


def handle_launch(slot_id):
    cfg = load_config()
    for slot in cfg.get("slots", []):
        if slot["id"] == slot_id and slot.get("app_name"):
            print(f"Launching slot {slot_id}: {slot['app_name']}")
            launch_app(slot["app_name"])
            return
    print(f"Slot {slot_id} is empty")


def get_gpu_info():
    try:
        result = subprocess.run(
            ["nvidia-smi", "--query-gpu=utilization.gpu,temperature.gpu",
             "--format=csv,noheader,nounits"],
            capture_output=True, text=True, timeout=2
        )
        if result.returncode == 0:
            parts = result.stdout.strip().split(",")
            return int(parts[0].strip()), int(parts[1].strip())
    except Exception:
        pass
    return 0, 0


# ========== 股票/虚拟货币监控 ==========

def fetch_json(url, timeout=5):
    """通用 JSON 请求"""
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read().decode())
    except Exception:
        return None


def fetch_crypto_prices(symbols):
    """从 Binance 获取虚拟货币价格"""
    results = {}
    for sym in symbols:
        sym = sym.upper().replace("/", "").replace("-", "").replace("USDT", "")
        pair = sym + "USDT"
        data = fetch_json(f"https://api.binance.com/api/v3/ticker/24hr?symbol={pair}")
        if data:
            results[sym] = {
                "price": float(data["lastPrice"]),
                "change": float(data["priceChangePercent"]),
            }
    return results


def fetch_stock_prices(symbols, market="us"):
    """从 yfinance 获取股票价格（含盘前盘后）"""
    try:
        import yfinance as yf
    except ImportError:
        return {}
    from datetime import datetime, timezone, timedelta
    results = {}
    for sym in symbols:
        sym = sym.upper().strip()
        try:
            ticker_sym = sym + ".KS" if market == "kr" else sym
            t = yf.Ticker(ticker_sym)
            info = t.info
            if not info:
                continue

            reg_price = info.get("regularMarketPrice", 0) or 0
            prev = info.get("regularMarketPreviousClose") or info.get("previousClose") or reg_price
            pre_price = info.get("preMarketPrice")
            post_price = info.get("postMarketPrice")
            market_state = info.get("marketState", "REGULAR")

            # 判断时段
            now_utc = datetime.now(timezone.utc)
            if market == "kr":
                kst = now_utc + timedelta(hours=9)
                t_min = kst.hour * 60 + kst.minute
                wd = kst.weekday()
                if wd >= 5: session = "休市"
                elif 510 <= t_min < 540: session = "盘前"
                elif 540 <= t_min < 930: session = "盘中"
                elif 930 <= t_min < 960: session = "盘后"
                else: session = "休市"
            else:
                # 用 yfinance 返回的 marketState
                state_map = {"PRE": "盘前", "PREPRE": "盘前", "REGULAR": "盘中",
                             "POST": "盘后", "POSTPOST": "盘后", "CLOSED": "休市"}
                session = state_map.get(market_state, "休市")

            # 当前时段的价格
            if session == "盘前" and pre_price:
                cur_price = pre_price
            elif session == "盘后" and post_price:
                cur_price = post_price
            else:
                cur_price = reg_price

            cur_change = ((cur_price - prev) / prev * 100) if prev else 0
            reg_change = ((reg_price - prev) / prev * 100) if prev else 0

            name = info.get("shortName") or info.get("longName") or sym

            results[sym] = {
                "name": name,
                "price": round(cur_price, 2),
                "change": round(cur_change, 2),
                "regular_price": round(reg_price, 2),
                "regular_change": round(reg_change, 2),
                "pre_price": round(pre_price, 2) if pre_price else None,
                "post_price": round(post_price, 2) if post_price else None,
                "session": session,
                "currency": info.get("currency", "USD"),
            }
        except Exception as e:
            print(f"yfinance error for {sym}: {e}")
    return results


# 缓存行情数据
finance_cache = {"us_stocks": {}, "kr_stocks": {}, "crypto": {}, "last_update": 0}
finance_callbacks = []  # SSE 回调列表


def get_sorted_finance():
    """按 watchlist 顺序排列行情数据"""
    cfg = load_config()
    wl = cfg.get("watchlist", {})
    result = {}
    for key in ["us_stocks", "kr_stocks", "crypto"]:
        items = finance_cache.get(key, {})
        order = wl.get(key, [])
        sorted_items = {}
        for sym in order:
            if sym in items:
                sorted_items[sym] = items[sym]
        for sym, val in items.items():
            if sym not in sorted_items:
                sorted_items[sym] = val
        result[key] = sorted_items
    result["last_update"] = finance_cache.get("last_update", 0)
    return result


def notify_finance_update():
    """通知所有 SSE 客户端行情更新 + 同步到单片机"""
    data = json.dumps(get_sorted_finance(), ensure_ascii=False)
    for cb in finance_callbacks[:]:
        try:
            cb(data)
        except Exception:
            finance_callbacks.remove(cb)
    # 同步到单片机
    serial_send(f"FINANCE:{data}")


def binance_ws_loop():
    """Binance WebSocket 实时推送虚拟货币价格（断线自动重连）"""
    while running:
        cfg = load_config()
        symbols = cfg.get("watchlist", {}).get("crypto", [])
        if not symbols:
            time.sleep(30)
            continue

        streams = [f"{s.lower()}usdt@ticker" for s in symbols]
        url = f"wss://stream.binance.com:9443/stream?streams={'/'.join(streams)}"

        def on_message(ws, message):
            try:
                data = json.loads(message)
                d = data.get("data", {})
                sym = d.get("s", "").replace("USDT", "")
                if sym:
                    finance_cache.setdefault("crypto", {})[sym] = {
                        "price": float(d.get("c", 0)),
                        "change": float(d.get("P", 0)),
                    }
                    notify_finance_update()
            except Exception:
                pass

        def on_error(ws, error):
            pass

        def on_close(ws, code, msg):
            pass

        try:
            ws = websocket.WebSocketApp(url,
                on_message=on_message,
                on_error=on_error,
                on_close=on_close)
            ws.run_forever(ping_interval=20)
        except Exception:
            pass
        time.sleep(5)


def stock_poll_loop():
    """股票轮询（每5秒）"""
    while running:
        try:
            cfg = load_config()
            wl = cfg.get("watchlist", {})
            us = wl.get("us_stocks", [])
            kr = wl.get("kr_stocks", [])
            crypto = wl.get("crypto", [])
            changed = False
            if us:
                data = fetch_stock_prices(us, "us")
                if data:
                    ordered = {}
                    for sym in us:
                        if sym in data:
                            ordered[sym] = data[sym]
                    if ordered:
                        finance_cache["us_stocks"] = ordered
                        changed = True
            if kr:
                data = fetch_stock_prices(kr, "kr")
                if data:
                    ordered = {}
                    for sym in kr:
                        if sym in data:
                            ordered[sym] = data[sym]
                    if ordered:
                        finance_cache["kr_stocks"] = ordered
                        changed = True
            if crypto:
                data = fetch_crypto_prices(crypto)
                if data:
                    ordered = {}
                    for sym in crypto:
                        clean = sym.upper().replace("USDT", "")
                        if clean in data:
                            ordered[clean] = data[clean]
                    if ordered:
                        finance_cache["crypto"] = ordered
                        changed = True
            if changed:
                finance_cache["last_update"] = time.time()
                notify_finance_update()
        except Exception as e:
            print(f"Stock poll error: {e}")
        time.sleep(5)


def update_finance_data():
    global finance_cache
    cfg = load_config()
    wl = cfg.get("watchlist", {})
    us = wl.get("us_stocks", [])
    kr = wl.get("kr_stocks", [])
    crypto = wl.get("crypto", [])

    if us:
        data = fetch_stock_prices(us, "us")
        finance_cache["us_stocks"] = {sym: data[sym] for sym in us if sym in data}
    if kr:
        data = fetch_stock_prices(kr, "kr")
        finance_cache["kr_stocks"] = {sym: data[sym] for sym in kr if sym in data}
    if crypto:
        data = fetch_crypto_prices(crypto)
        finance_cache["crypto"] = {sym.upper().replace("USDT", ""): data[sym.upper().replace("USDT", "")] for sym in crypto if sym.upper().replace("USDT", "") in data}
    finance_cache["last_update"] = time.time()
    notify_finance_update()


@app.route("/api/finance-stream")
def api_finance_stream():
    """SSE 实时推送行情"""
    def generate():
        q = []
        finance_callbacks.append(q.append)
        try:
            while True:
                if q:
                    data = q.pop(0)
                    yield f"data: {data}\n\n"
                else:
                    yield ": keepalive\n\n"
                time.sleep(0.5)
        finally:
            if q.append in finance_callbacks:
                finance_callbacks.remove(q.append)

    return Response(generate(), mimetype="text/event-stream",
                    headers={"Cache-Control": "no-cache", "X-Accel-Buffering": "no"})


# ========== 新闻 ==========

news_cache = []
news_cache_translated = []
news_callbacks = []
calendar_cache = []


def fetch_economic_calendar():
    """获取经济日历（Forex Factory JSON API），加翻译"""
    from datetime import datetime, timezone, timedelta
    try:
        now = datetime.now(timezone.utc)
        weekday = now.weekday()  # 0=Mon, 5=Sat, 6=Sun

        # 使用 JSON API（比 XML 更可靠，日期已是 ISO 格式）
        raw = fetch_json("https://nfs.faireconomy.media/ff_calendar_thisweek.json", timeout=10)
        if not raw:
            return []

        all_data = []
        for item in raw:
            impact = item.get("impact", "Low")
            title = item.get("title", "")
            if not title:
                continue

            try:
                dt = datetime.fromisoformat(item["date"])
                if dt.tzinfo is None:
                    dt = dt.replace(tzinfo=timezone.utc)
            except Exception:
                continue

            # 工作日：过滤24小时前的事件
            # 周末：显示本周所有事件
            if weekday < 5:
                if dt < now - timedelta(hours=24):
                    continue
            else:
                this_monday = now.replace(hour=0, minute=0, second=0, microsecond=0) - timedelta(days=weekday)
                if dt < this_monday:
                    continue

            local_dt = dt.astimezone(None)
            all_data.append({
                "title": title,
                "country": item.get("country", ""),
                "date": local_dt.strftime("%Y-%m-%dT%H:%M"),
                "impact": impact,
                "forecast": item.get("forecast", "") or "",
                "previous": item.get("previous", "") or ""
            })

        # 按影响级别排序（High > Medium > Low），同级别按时间
        impact_order = {"High": 0, "Medium": 1, "Low": 2}
        all_data.sort(key=lambda x: (impact_order.get(x["impact"], 3), x["date"]))

        events = all_data[:30]

        # 批量翻译事件名
        for ev in events:
            ev["event_zh"] = translate_text(ev["title"])
            time.sleep(0.1)

        return events[:30]
    except Exception as e:
        print(f"Calendar fetch error: {e}")
        return []


def calendar_refresh_loop():
    """每小时刷新一次经济日历"""
    global calendar_cache
    while running:
        try:
            calendar_cache = fetch_economic_calendar()
            if calendar_cache and ser and ser.is_open:
                serial_send(f"CALENDAR:{json.dumps(calendar_cache, ensure_ascii=False)}")
        except Exception:
            pass
        time.sleep(3600)


def _relative_time(time_str):
    """将 RSS 时间转为相对时间（如 '5分钟前'）"""
    if not time_str:
        return "刚刚"
    try:
        from email.utils import parsedate_to_datetime
        from datetime import timezone as tz
        dt = parsedate_to_datetime(time_str)
        now = datetime.now(tz.utc)
        diff = now - dt
        secs = int(diff.total_seconds())
        if secs < 0: return "刚刚"
        if secs < 60: return "刚刚"
        if secs < 3600: return f"{secs // 60}分钟前"
        if secs < 86400: return f"{secs // 3600}小时前"
        if secs < 172800: return "昨天"
        return f"{secs // 86400}天前"
    except Exception:
        return "刚刚"


def news_for_mcu():
    """获取精简后的新闻数据（限制条数和字符串长度，适配 MCU 串口缓冲区）"""
    raw = news_cache_translated or news_cache
    # 优先 FJ快讯（快讯排前面），再按原顺序取其他
    fj = [item for item in raw if item.get("source") == "FJ快讯"]
    others = [item for item in raw if item.get("source") != "FJ快讯"]
    ordered = fj[:10] + others[:10]
    items = []
    for item in ordered:
        d = {}
        d["source"] = (item.get("source") or "")[:16]
        d["title"] = (item.get("title_zh") or item.get("title") or "")[:60]
        d["desc"] = (item.get("desc_zh") or item.get("desc") or "")[:60]
        d["time"] = _relative_time(item.get("time"))
        items.append(d)
    return items


def notify_news_update():
    # 推送完整翻译数据到 Web SSE（不做截断，保留原始时间格式）
    raw = news_cache_translated or news_cache
    web_data = json.dumps(raw, ensure_ascii=False)
    for cb in news_callbacks[:]:
        try:
            cb(web_data)
        except Exception:
            news_callbacks.remove(cb)
    # 推送精简数据到单片机（字段截断、FJ优先、相对时间）
    mcu_items = news_for_mcu()
    serial_send(f"NEWS:{json.dumps(mcu_items, ensure_ascii=False)}")

def fetch_rss(url, source_name, equities_only=False):
    """通用 RSS 解析，过滤广告和非市场新闻"""
    items = []
    # 过滤：非市场新闻
    skip_keywords = [
        "mortgage", "prescription", "walgreens", "tax season", "Social Security",
        "retirement", "annuit", "inherit", "home buyer", "house", "NBA", "NFL",
        "QR code", "medical", "steak", "insurance", "401k", "IRA", "Roth",
        "credit card", "debt payoff", "savings account", "student loan",
        "HELOC", "CD rate", "money market", "high-yield savings",
    ]
    # 保留：影响股价的关键信息
    keep_keywords = [
        "stock", "market", "S&P", "Dow", "Nasdaq", "earnings", "Fed", "rate",
        "GDP", "inflation", "CPI", "PCE", "unemployment", "oil", "gold", "bond",
        "yield", "treasury", "IPO", "SPAC", "crypto", "bitcoin", "ETF", "trade",
        "tariff", "recession", "bull", "bear", "rally", "selloff", "AI", "tech",
        "Apple", "Google", "Amazon", "Tesla", "Microsoft", "Nvidia", "Meta",
        "SpaceX", "revenue", "profit", "CEO", "CFO", "acquisition", "merger",
        "hike", "cut", "dovish", "hawkish", "FOMC", "Powell", "Warsh",
        "geopolitical", "sanction", "tariff", "war", "crisis", "default",
    ]
    # Equities 专用关键词（$TICKER + 公司名 + 股市术语）
    equities_keywords = [
        "$",
        "stock", "equit", "shares", "dividend", "buyback",
        "S&P", "Dow", "Nasdaq", "NYSE", "Wall Street",
        "earnings", "revenue", "profit", "EPS", "IPO", "SPAC",
        "市值", "股价", "盘前", "盘后", "目标价", "回购",
        "Apple", "AAPL", "Google", "GOOGL", "Amazon", "AMZN",
        "Tesla", "TSLA", "Microsoft", "MSFT", "Nvidia", "NVDA",
        "Meta", "Netflix", "AMD", "Intel", "INTC",
        "Qualcomm", "QCOM", "TSMC", "Micron", "MU", "Broadcom", "AVGO",
        "JPMorgan", "Goldman", "Morgan Stanley", "FedEx", "FDX",
        "upgrade", "downgrade", "target price",
        "ETF", "SpaceX", "Anthropic", "OpenAI", "GPT",
        "Mac", "MacBook", "iPad", "iPhone", "Xbox",
        "苹果", "谷歌", "微软", "特斯拉", "高通", "美光", "英伟达",
        "芯片", "半导体", "数据中心",
    ]
    try:
        req = urllib.request.Request(url, headers={
            "User-Agent": "Mozilla/5.0",
            "Cache-Control": "no-cache, no-store, must-revalidate",
            "Pragma": "no-cache",
        })
        with urllib.request.urlopen(req, timeout=10) as r:
            xml_data = r.read().decode("utf-8")
        root = ET.fromstring(xml_data)
        for item in root.findall(".//item"):
            title = item.findtext("title", "")
            desc = item.findtext("description", "")
            link = item.findtext("link", "")
            pub = item.findtext("pubDate", "")
            category = ""
            for prefix in ["{http://dowjones.net/rss/}", "{http://purl.org/dc/elements/1.1/}"]:
                cat_el = item.find(f"{prefix}articletype")
                if cat_el is not None:
                    category = cat_el.text or ""
                    break

            # 过滤广告和非市场内容
            text = (title + " " + desc + " " + category).lower()
            if any(kw.lower() in text for kw in skip_keywords):
                if not any(kw.lower() in text for kw in keep_keywords):
                    continue

            # Equities 专用过滤
            if equities_only:
                if not any(kw.lower() in text for kw in equities_keywords):
                    continue

            # pubDate 为空时用当前时间
            if not pub:
                from email.utils import formatdate
                pub = formatdate(usegmt=True)

            items.append({
                "source": source_name,
                "title": title,
                "desc": desc,
                "link": link,
                "time": pub,
                "category": category,
            })
    except Exception as e:
        print(f"RSS fetch error ({source_name}): {e}")
    return items


_fj_cache = []
_fj_last_attempt = 0      # 上次尝试时间（成功或失败都更新）
_fj_last_success = 0       # 上次成功获取时间
_FJ_CACHE_TTL = 1800       # 缓存过期时间：30分钟
_FJ_RETRY_OK = 600         # 成功后重试间隔：10分钟
_FJ_RETRY_FAIL = 900       # 失败后重试间隔：15分钟

def fetch_all_news():
    """获取新闻源：Financial Juice(股票) + Yahoo + CNBC"""
    global _fj_cache, _fj_last_attempt, _fj_last_success
    feeds = [
        ("https://feeds.finance.yahoo.com/rss/2.0/headline?s=^GSPC,^DJI,^IXIC&region=US&lang=en-US", "Yahoo市场", False),
        ("https://feeds.finance.yahoo.com/rss/2.0/headline?s=BTC-USD,ETH-USD&region=US&lang=en-US", "Yahoo加密", False),
        ("https://search.cnbc.com/rs/search/combinedcms/view.xml?partnerId=wrss01&id=10000664", "CNBC财经", False),
    ]
    combined = []
    for url, source, eq_only in feeds:
        combined.extend(fetch_rss(url, source, equities_only=eq_only))
    # FJ 限流严重（HTTP 429），自适应重试间隔
    now = time.time()
    retry_interval = _FJ_RETRY_OK if _fj_last_success > _fj_last_attempt else _FJ_RETRY_FAIL
    if now - _fj_last_attempt > retry_interval:
        _fj_last_attempt = now
        fj = fetch_rss("https://www.financialjuice.com/feed.ashx?xy=rss", "FJ快讯", equities_only=True)
        if fj:
            _fj_cache = fj
            _fj_last_success = now
            print(f"[FJ] Fetched {len(fj)} articles")
        else:
            print("[FJ] Fetch failed (rate limited?), will retry later")
    # 缓存过期自动清掉，避免展示僵尸数据
    if _fj_last_success and (now - _fj_last_success) > _FJ_CACHE_TTL:
        _fj_cache = []
        _fj_last_success = 0
        print("[FJ] Cache expired, cleared stale data")
    if _fj_cache:
        combined.extend(_fj_cache)
    def _sort_key(item):
        try:
            from email.utils import parsedate_to_datetime
            return parsedate_to_datetime(item.get("time", "")).timestamp()
        except Exception:
            return 0
    combined.sort(key=_sort_key, reverse=True)
    return combined[:50]


def fetch_env():
    """获取当前时间、日期、天气"""
    from datetime import datetime
    now = datetime.now()
    time_str = now.strftime("%H:%M")
    
    # 格式化日期：6月27日 周六
    weekdays = ["周一", "周二", "周三", "周四", "周五", "周六", "周日"]
    date_str = f"{now.month}月{now.day}日 {weekdays[now.weekday()]}"
    
    # 格式化副日期：2026年 · 第26周
    week_num = now.isocalendar()[1]
    sub_date = f"{now.year}年 · 第{week_num}周"
    
    # 获取天气
    temp, desc = "25", "晴"
    try:
        req = urllib.request.Request("https://wttr.in/?format=j1", headers={'User-Agent': 'Mozilla/5.0'})
        with urllib.request.urlopen(req, timeout=3) as r:
            data = json.loads(r.read().decode())
            cc = data['current_condition'][0]
            temp = cc['temp_C']
            desc_en = cc.get('weatherDesc', [{'value': 'Clear'}])[0]['value']
            desc = translate_text(desc_en)
    except Exception as e:
        print(f"Weather fetch error: {e}")
        
    return {
        "time": time_str,
        "date": date_str,
        "sub_date": sub_date,
        "temp": temp,
        "desc": desc
    }

def translate_text(text, target="zh-CN", retries=2):
    """翻译文本（Google 免费接口，带重试）"""
    if not text or len(text) < 3:
        return text
    for attempt in range(retries):
        try:
            encoded = urllib.parse.quote(text[:500])
            url = f"https://translate.googleapis.com/translate_a/single?client=gtx&sl=en&tl={target}&dt=t&q={encoded}"
            req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(req, timeout=8) as r:
                data = json.loads(r.read().decode())
            if data and data[0]:
                result = "".join(part[0] for part in data[0] if part[0])
                if result and result != text:
                    return result
        except Exception:
            if attempt < retries - 1:
                time.sleep(0.5)
    return text


_translating = False

def translate_news_async(items):
    """异步翻译：立即让英文新闻可用，后台逐条翻译并实时推送"""
    global news_cache_translated, _translating
    import copy
    translated = copy.deepcopy(items)
    news_cache_translated = translated  # 立即设为英文版，Web 立即可看
    _translating = True

    def _do_translate():
        global _translating
        count = 0
        for item in translated:
            if not _translating:  # 被新的翻译任务取代，提前退出
                return
            title = item.get("title", "")
            desc = item.get("desc", "")
            if title:
                item["title_zh"] = translate_text(title)
                time.sleep(0.05)
            if desc:
                item["desc_zh"] = translate_text(desc)
                time.sleep(0.05)
            count += 1
            if count % 5 == 0:  # 每翻译 5 条推送一次
                notify_news_update()
        _translating = False
        notify_news_update()  # 全部翻译完成，最终推送
        print(f"[News] Translation complete: {count} articles")

    threading.Thread(target=_do_translate, daemon=True).start()


def news_refresh_loop():
    """新闻刷新：每 120 秒获取+翻译，每 10 秒推送到单片机"""
    global news_cache, news_cache_translated, _translating
    fetch_tick = 99  # 首次立即获取
    while running:
        fetch_tick += 1
        if fetch_tick >= 12:
            fetch_tick = 0
            try:
                news = fetch_all_news()
                if news:
                    old_by_src = {}
                    for item in news_cache:
                        old_by_src.setdefault(item.get("source", ""), []).append(item)
                    new_by_src = {}
                    for item in news:
                        new_by_src.setdefault(item.get("source", ""), []).append(item)
                    merged = []
                    for src in set(list(old_by_src.keys()) + list(new_by_src.keys())):
                        merged.extend(new_by_src.get(src, old_by_src.get(src, [])))
                    seen = set()
                    deduped = [i for i in merged if i.get("title") and i["title"] not in seen and not seen.add(i["title"])]
                    news_cache = deduped
                    _translating = False  # 取消旧的翻译任务
                    translate_news_async(deduped)  # 不阻塞，立即返回
            except Exception as e:
                print(f"News fetch error: {e}")
        # 每 10 秒推送缓存到单片机
        if news_cache:
            notify_news_update()
        time.sleep(10)


@app.route("/api/news", methods=["GET"])
def api_news():
    if news_cache_translated:
        return jsonify(news_cache_translated)
    return jsonify(news_cache)


@app.route("/api/news-stream")
def api_news_stream():
    """SSE 实时推送新闻"""
    def generate():
        q = []
        news_callbacks.append(q.append)
        try:
            while True:
                if q:
                    data = q.pop(0)
                    yield f"data: {data}\n\n"
                else:
                    yield ": keepalive\n\n"
                time.sleep(1)
        finally:
            if q.append in news_callbacks:
                news_callbacks.remove(q.append)

    return Response(generate(), mimetype="text/event-stream",
                    headers={"Cache-Control": "no-cache", "X-Accel-Buffering": "no"})


@app.route("/api/calendar", methods=["GET"])
def api_calendar():
    return jsonify(calendar_cache)


def get_cpu_temp():
    # Linux: psutil
    try:
        temps = psutil.sensors_temperatures()
        for name in ["coretemp", "cpu_thermal", "k10temp", "acpitz"]:
            if name in temps and temps[name]:
                return int(temps[name][0].current)
    except Exception:
        pass

    # macOS
    if OS_NAME == "Darwin":
        # 方法1: osx-cpu-temp (brew install osx-cpu-temp)
        try:
            result = subprocess.run(
                ["osx-cpu-temp"],
                capture_output=True, text=True, timeout=2
            )
            if result.returncode == 0:
                temp_str = result.stdout.strip().replace("°C", "").replace("°F", "")
                return int(float(temp_str))
        except (FileNotFoundError, Exception):
            pass
        # 方法2: IOKit (Apple Silicon, 无需sudo)
        try:
            result = subprocess.run(
                ["ioreg", "-rfl", "AppleSmartBattery"],
                capture_output=True, text=True, timeout=3
            )
            for line in result.stdout.split("\n"):
                if "Temperature" in line:
                    parts = line.split("=")
                    if len(parts) == 2:
                        temp_raw = int(parts[1].strip())
                        if temp_raw > 1000:
                            return temp_raw // 1000
                        return temp_raw
        except Exception:
            pass
        # 方法3: powermetrics (需要sudo)
        try:
            result = subprocess.run(
                ["sudo", "powermetrics", "--samplers", "smc", "-n", "1", "-i", "100"],
                capture_output=True, text=True, timeout=3
            )
            for line in result.stdout.split("\n"):
                if "CPU die temperature" in line:
                    temp = line.split(":")[-1].strip().replace("C", "")
                    return int(float(temp))
        except Exception:
            pass

    return 0


def monitor_loop():
    global monitor_data
    prev_net = psutil.net_io_counters()
    prev_time = time.time()
    psutil.cpu_percent(interval=None)
    finance_timer = 0

    while running:
        try:
            time.sleep(1.5)
            cpu = psutil.cpu_percent(interval=None)
            mem = psutil.virtual_memory()
            disk = psutil.disk_usage("/")
            cpu_temp = get_cpu_temp()
            disk_used_gb = round(disk.used / (1024**3), 1)
            disk_total_gb = round(disk.total / (1024**3), 1)
            gpu_pct, gpu_temp = get_gpu_info()

            now = time.time()
            dt = now - prev_time
            net = psutil.net_io_counters()
            up = (net.bytes_sent - prev_net.bytes_sent) / dt / 1024
            down = (net.bytes_recv - prev_net.bytes_recv) / dt / 1024
            prev_net = net
            prev_time = now

            monitor_data = {
                "cpu_percent": round(cpu, 1),
                "cpu_temp": cpu_temp,
                "cpu_model": CPU_MODEL,
                "mem_percent": round(mem.percent, 1),
                "mem_used": round(mem.used / (1024**3), 1),
                "mem_total": round(mem.total / (1024**3), 1),
                "gpu_percent": gpu_pct,
                "gpu_temp": gpu_temp,
                "disk_percent": round(disk.percent, 1),
                "disk_used": disk_used_gb,
                "disk_total": disk_total_gb,
                "net_up": round(up, 1),
                "net_down": round(down, 1),
                "time_str": time.strftime("%H:%M"),
            }

            # 每30秒刷新一次行情
            if now - finance_timer > 30:
                threading.Thread(target=update_finance_data, daemon=True).start()
                finance_timer = now

            serial_send(f"MON:{json.dumps(monitor_data)}")
        except Exception as e:
            print(f"Monitor error: {e}")

        time.sleep(0.5)


@app.route("/")
def index():
    resp = send_from_directory(str(WEB_DIR), "index.html")
    resp.headers["Cache-Control"] = "no-cache, no-store, must-revalidate"
    return resp


@app.route("/<path:filename>")
def static_files(filename):
    resp = send_from_directory(str(WEB_DIR), filename)
    resp.headers["Cache-Control"] = "no-cache, no-store, must-revalidate"
    return resp


@app.route("/api/config", methods=["GET"])
def api_get_config():
    return jsonify(get_config())


@app.route("/api/config", methods=["POST"])
def api_set_config():
    cfg = request.json
    save_config(cfg)
    return jsonify({"ok": True})


@app.route("/api/slots", methods=["POST"])
def api_update_slot():
    data = request.json
    if not data or not isinstance(data.get("id"), int) or not (1 <= data["id"] <= 6):
        return jsonify({"ok": False, "error": "Invalid slot ID"}), 400
    cfg = load_config()
    for slot in cfg["slots"]:
        if slot["id"] == data["id"]:
            # 兼容 name 和 app_name 两种字段
            app_name = data.get("app_name") or data.get("name", "")
            if app_name:
                slot["name"] = app_name
                slot["app_name"] = app_name
                slot["app_path"] = find_app_path(app_name)
            else:
                slot["name"] = ""
                slot["app_name"] = ""
                slot["app_path"] = ""
                slot["icon"] = ""
            if "icon" in data and data["icon"]:
                slot["icon"] = data["icon"]
            if not slot.get("icon") and slot.get("app_path"):
                slot["icon"] = extract_app_icon(slot["app_path"])
            break
    save_config(cfg)
    return jsonify({"ok": True})


@app.route("/api/extract-icon", methods=["POST"])
def api_extract_icon():
    data = request.json
    app_path = data.get("path", "")
    # 安全校验：只允许提取已知目录下的应用图标
    allowed_prefixes = ["/Applications", str(Path.home() / "Applications"),
                        os.environ.get("ProgramFiles", "C:\\Program Files"),
                        os.environ.get("ProgramFiles(x86)", "C:\\Program Files (x86)"),
                        str(Path.home() / "AppData")]
    if not any(app_path.startswith(p) for p in allowed_prefixes if p):
        return jsonify({"icon": ""})
    icon = extract_app_icon(app_path)
    return jsonify({"icon": icon})


@app.route("/api/list-apps", methods=["GET"])
def api_list_apps():
    """列出已安装的应用"""
    apps = []
    if OS_NAME == "Darwin":
        for d in ["/Applications", str(Path.home() / "Applications")]:
            p = Path(d)
            if p.exists():
                for app in sorted(p.glob("*.app")):
                    apps.append({"name": app.stem, "path": str(app)})
    elif OS_NAME == "Windows":
        import glob
        for d in [
            os.environ.get("ProgramFiles", "C:\\Program Files"),
            os.environ.get("ProgramFiles(x86)", "C:\\Program Files (x86)"),
            str(Path.home() / "AppData" / "Local" / "Programs"),
        ]:
            for exe in glob.glob(os.path.join(d, "**/*.exe"), recursive=True):
                apps.append({"name": Path(exe).stem, "path": exe})
    return jsonify(apps[:50])


@app.route("/api/ports", methods=["GET"])
def api_list_ports():
    return jsonify(list_serial_ports())


def send_slot_icons(cfg):
    """发送应用图标到 MCU（RGB565 格式）"""
    for slot in cfg.get("slots", []):
        icon_b64 = slot.get("icon", "")
        if not icon_b64:
            continue
        try:
            icon_data = base64.b64decode(icon_b64)
            img = Image.open(BytesIO(icon_data))
            img = img.convert("RGBA").resize((80, 80), Image.LANCZOS)
            bg = Image.new("RGBA", (80, 80), (255, 255, 255, 255))
            bg.alpha_composite(img)
            img = bg.convert("RGB")
            rgb565 = bytearray()
            for y in range(80):
                for x in range(80):
                    r, g, b = img.getpixel((x, y))
                    val = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
                    rgb565.extend(val.to_bytes(2, 'little'))
            encoded = base64.b64encode(bytes(rgb565)).decode()
            serial_send(f"ICON:{slot['id'] - 1}:{encoded}")
            time.sleep(0.5)
        except Exception as e:
            print(f"Icon send error slot {slot.get('id')}: {e}")


def auto_sync():
    """连接成功后自动同步所有数据"""
    global news_cache, news_cache_translated, calendar_cache
    time.sleep(1)
    cfg = load_config()
    # 同步 App 槽位
    serial_send("SYNC_START")
    time.sleep(0.1)
    for slot in cfg.get("slots", []):
        if slot.get("name"):
            serial_send(f"SLOT:{slot['id']}|{slot['name']}")
            time.sleep(0.05)
    # 发送应用图标
    send_slot_icons(cfg)

    # 立即发送一次时间/环境信息
    try:
        env_data = fetch_env()
        serial_send(f"ENV:{json.dumps(env_data, ensure_ascii=False)}")
        time.sleep(0.1)
    except Exception as e:
        print(f"[SYNC] env fetch error: {e}")

    # 等待新闻和日历缓存就绪（最多等 20 秒）
    for _ in range(40):
        if news_cache and calendar_cache:
            break
        time.sleep(0.5)
    # 如果缓存仍为空，主动获取
    if not news_cache:
        try:
            news = fetch_all_news()
            news_cache = news
            translate_news_async(news)
        except Exception as e:
            print(f"[SYNC] news fetch error: {e}")
    if not calendar_cache:
        try:
            calendar_cache = fetch_economic_calendar()
        except Exception as e:
            print(f"[SYNC] calendar fetch error: {e}")
    # 同步行情
    serial_send(f"FINANCE:{json.dumps(finance_cache, ensure_ascii=False)}")
    time.sleep(0.1)
    # 同步新闻
    news_data = news_for_mcu()
    print(f"[SYNC] news_cache={len(news_cache)} translated={len(news_cache_translated)} sending={len(news_data)}")
    serial_send(f"NEWS:{json.dumps(news_data, ensure_ascii=False)}")
    time.sleep(0.1)
    # 同步日历
    print(f"[SYNC] calendar_cache={len(calendar_cache)}")
    serial_send(f"CALENDAR:{json.dumps(calendar_cache, ensure_ascii=False)}")
    time.sleep(0.1)
    serial_send("SYNC_END")
    print("Auto sync done")


@app.route("/api/connect", methods=["POST"])
def api_connect():
    data = request.json
    port = data.get("port")
    baud = data.get("baud", 115200)
    ok = serial_connect(port, baud)
    if ok:
        # 连接成功后自动同步所有数据
        threading.Thread(target=auto_sync, daemon=True).start()
    return jsonify({"ok": ok})


@app.route("/api/disconnect", methods=["POST"])
def api_disconnect():
    global ser
    with serial_lock:
        if ser and ser.is_open:
            ser.close()
    return jsonify({"ok": True})


@app.route("/api/status", methods=["GET"])
def api_status():
    connected = ser is not None and ser.is_open
    return jsonify({
        "connected": connected,
        "port": ser.port if connected else None,
        "monitor": monitor_data,
    })


@app.route("/api/monitor", methods=["GET"])
def api_monitor():
    return jsonify(monitor_data)


@app.route("/api/finance", methods=["GET"])
def api_finance():
    return jsonify(get_sorted_finance())


@app.route("/api/watchlist", methods=["GET"])
def api_get_watchlist():
    cfg = load_config()
    return jsonify(cfg.get("watchlist", {"stocks": [], "crypto": []}))


@app.route("/api/watchlist", methods=["POST"])
def api_set_watchlist():
    data = request.json
    cfg = load_config()
    cfg["watchlist"] = {
        "us_stocks": [s.upper().strip() for s in data.get("us_stocks", [])],
        "kr_stocks": [s.upper().strip() for s in data.get("kr_stocks", [])],
        "crypto": [s.upper().strip().replace("USDT", "") for s in data.get("crypto", [])],
    }
    save_config(cfg)
    threading.Thread(target=update_finance_data, daemon=True).start()
    return jsonify({"ok": True})


@app.route("/api/watchlist/add", methods=["POST"])
def api_add_watchlist():
    data = request.json
    item_type = data.get("type")
    symbol = data.get("symbol", "").upper().strip().replace(" ", "").replace("USDT", "")
    if not symbol:
        return jsonify({"ok": False, "error": "参数错误"}), 400

    key_map = {"us_stock": "us_stocks", "kr_stock": "kr_stocks", "crypto": "crypto"}
    key = key_map.get(item_type)
    if not key:
        return jsonify({"ok": False, "error": "类型错误"}), 400

    cfg = load_config()
    wl = cfg.setdefault("watchlist", {"us_stocks": [], "kr_stocks": [], "crypto": []})
    if symbol not in wl[key]:
        wl[key].append(symbol)
        save_config(cfg)
        # 先返回，后台异步获取数据
        def fetch_and_update():
            try:
                if item_type == "us_stock":
                    price_data = fetch_stock_prices([symbol], "us")
                    finance_cache.setdefault("us_stocks", {}).update(price_data)
                elif item_type == "kr_stock":
                    price_data = fetch_stock_prices([symbol], "kr")
                    finance_cache.setdefault("kr_stocks", {}).update(price_data)
                elif item_type == "crypto":
                    price_data = fetch_crypto_prices([symbol])
                    finance_cache.setdefault("crypto", {}).update(price_data)
                finance_cache["last_update"] = time.time()
                notify_finance_update()
            except Exception as e:
                print(f"Async fetch error: {e}")
        threading.Thread(target=fetch_and_update, daemon=True).start()
    return jsonify({"ok": True, "data": finance_cache})


@app.route("/api/watchlist/remove", methods=["POST"])
def api_remove_watchlist():
    data = request.json
    item_type = data.get("type")
    symbol = data.get("symbol", "").upper().strip().replace("USDT", "")

    key_map = {"us_stock": "us_stocks", "kr_stock": "kr_stocks", "crypto": "crypto"}
    key = key_map.get(item_type)
    if not key:
        return jsonify({"ok": False, "error": "类型错误"}), 400

    cfg = load_config()
    wl = cfg.get("watchlist", {})
    items = wl.get(key, [])
    if symbol in items:
        items.remove(symbol)
        save_config(cfg)
        finance_cache.get(key, {}).pop(symbol, None)
        notify_finance_update()
    return jsonify({"ok": True, "data": finance_cache})


@app.route("/api/search-symbol", methods=["GET"])
def api_search_symbol():
    """搜索股票/币种代码"""
    query = request.args.get("q", "").strip()
    item_type = request.args.get("type", "us_stock")
    if len(query) < 1:
        return jsonify([])

    results = []
    if item_type in ("us_stock", "kr_stock"):
        # Yahoo Finance 搜索
        data = fetch_json(
            f"https://query2.finance.yahoo.com/v1/finance/search?q={query}&quotesCount=10&newsCount=0",
            timeout=3
        )
        if data and "quotes" in data:
            for q in data["quotes"]:
                sym = q.get("symbol", "")
                name = q.get("shortname") or q.get("longname", "")
                if item_type == "kr_stock":
                    if not sym.endswith(".KS"):
                        continue
                elif item_type == "us_stock":
                    if sym.endswith(".KS") or sym.endswith(".HK") or sym.endswith(".SI"):
                        continue
                results.append({"symbol": sym.replace(".KS", ""), "name": name})
    elif item_type == "crypto":
        # Binance 搜索
        data = fetch_json("https://api.binance.com/api/v3/exchangeInfo", timeout=5)
        if data and "symbols" in data:
            q = query.upper()
            for s in data["symbols"]:
                sym = s["symbol"]
                if sym.endswith("USDT") and q in sym:
                    base = sym.replace("USDT", "")
                    results.append({"symbol": base, "name": base})
                    if len(results) >= 8:
                        break

    return jsonify(results)


@app.route("/api/sync", methods=["POST"])
def api_sync():
    if not ser or not ser.is_open:
        return jsonify({"ok": False, "error": "未连接设备"}), 400
    cfg = load_config()
    # 如果新闻缓存为空，先同步获取一次
    global news_cache, news_cache_translated
    if not news_cache:
        try:
            news = fetch_all_news()
            news_cache = news
            translate_news_async(news)
        except Exception as e:
            print(f"[SYNC] news fetch error: {e}")
    serial_send("SYNC_START")
    time.sleep(0.1)
    
    # Update env
    try:
        env = fetch_env()
        serial_send(f"ENV:{json.dumps(env, ensure_ascii=False)}")
        time.sleep(0.1)
    except Exception as e:
        pass

    # 同步 App 槽位
    for slot in cfg.get("slots", []):
        if slot.get("name"):
            data = f"SLOT:{slot['id']}|{slot['name']}"
            serial_send(data)
            time.sleep(0.05)
    # 发送应用图标
    send_slot_icons(cfg)
    # 同步设置
    serial_send(f"TIMEOUT:{cfg.get('timeout', 30)}")
    time.sleep(0.05)
    serial_send(f"BRIGHTNESS:{cfg.get('brightness', 80)}")
    time.sleep(0.05)
    # 同步行情
    serial_send(f"FINANCE:{json.dumps(finance_cache, ensure_ascii=False)}")
    time.sleep(0.1)
    # 同步新闻
    news_data = news_for_mcu()
    print(f"[SYNC] news_cache={len(news_cache)} translated={len(news_cache_translated)} sending={len(news_data)}")
    serial_send(f"NEWS:{json.dumps(news_data, ensure_ascii=False)}")
    time.sleep(0.1)
    # 同步日历
    print(f"[SYNC] calendar_cache={len(calendar_cache)}")
    serial_send(f"CALENDAR:{json.dumps(calendar_cache, ensure_ascii=False)}")
    time.sleep(0.1)
    serial_send("SYNC_END")
    return jsonify({"ok": True})


@app.route("/api/launch/<int:slot_id>", methods=["POST"])
def api_launch(slot_id):
    handle_launch(slot_id)
    return jsonify({"ok": True})


file_ack_event = threading.Event()

def transfer_file_thread(filepath, target_filename):
    try:
        import base64
        size = os.path.getsize(filepath)
        print(f"\n[FILE] Starting transfer of {filepath} ({size} bytes) to {target_filename}")
        file_ack_event.clear()
        serial_send(f"FILE_START:{target_filename}|{size}")
        if not file_ack_event.wait(5.0):
            print("[FILE] Transfer failed: no ACK for FILE_START")
            return
            
        chunk_size = 768  # 768 bytes -> 1024 bytes base64
        with open(filepath, "rb") as f:
            total_sent = 0
            while True:
                chunk = f.read(chunk_size)
                if not chunk:
                    break
                b64 = base64.b64encode(chunk).decode('ascii')
                file_ack_event.clear()
                serial_send(f"FILE_DATA:{b64}")
                if not file_ack_event.wait(5.0):
                    print("[FILE] Transfer failed: no ACK for FILE_DATA")
                    return
                total_sent += len(chunk)
                if total_sent % (chunk_size * 20) == 0 or total_sent == size:
                    print(f"[FILE] Transfer progress: {total_sent}/{size} ({total_sent*100//size}%)")
                    
        file_ack_event.clear()
        serial_send("FILE_END")
        if file_ack_event.wait(5.0):
            print("[FILE] Transfer completed successfully! MCU is rebooting.")
        else:
            print("[FILE] Transfer completed but no final ACK received.")
    except Exception as e:
        print(f"[FILE] Transfer error: {e}")

@app.route("/api/transfer-font", methods=["POST"])
def api_transfer_font():
    font_path = "/Users/a1/Desktop/MyPad/font.bin"
    if not os.path.exists(font_path):
        return jsonify({"ok": False, "error": "Font file not found on desktop"})
    threading.Thread(target=transfer_file_thread, args=(font_path, "font.bin"), daemon=True).start()
    return jsonify({"ok": True, "message": "Transfer started in background. Check terminal."})

def main():
    cfg = load_config()
    if cfg.get("serial_port"):
        serial_connect(cfg["serial_port"], cfg.get("baud_rate", 115200))

    threading.Thread(target=serial_read_loop, daemon=True).start()
    threading.Thread(target=monitor_loop, daemon=True).start()
    threading.Thread(target=stock_poll_loop, daemon=True).start()
    threading.Thread(target=binance_ws_loop, daemon=True).start()
    threading.Thread(target=news_refresh_loop, daemon=True).start()
    threading.Thread(target=calendar_refresh_loop, daemon=True).start()

    print("MyPad Server running at http://127.0.0.1:5800")
    app.run(host="127.0.0.1", port=5800, debug=False, threaded=True)


if __name__ == "__main__":
    main()

def env_loop():
    import time
    while True:
        if ser and ser.is_open:
            try:
                env = fetch_env()
                serial_send(f"ENV:{json.dumps(env, ensure_ascii=False)}")
            except Exception as e:
                print(f"[ENV] Error: {e}")
        time.sleep(1)

threading.Thread(target=env_loop, daemon=True).start()
