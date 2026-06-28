const API = "http://127.0.0.1:5800/api";
let config = null;
let monitorTimer = null;
let financeTimer = null;
let newsTimer = null;
let newsSSE = null;
let financeSSE = null;
let searchDebounce = null;
let currentFinanceType = null;

function esc(s) {
  if (!s) return "";
  const d = document.createElement("div");
  d.textContent = String(s);
  return d.innerHTML;
}

async function api(path, method = "GET", body = null) {
  const opts = { method, headers: { "Content-Type": "application/json" } };
  if (body) opts.body = JSON.stringify(body);
  const res = await fetch(`${API}${path}`, opts);
  return res.json();
}

function showToast(msg, type = "info") {
  const t = document.createElement("div");
  t.className = `toast toast-${type}`;
  t.textContent = msg;
  document.body.appendChild(t);
  requestAnimationFrame(() => t.classList.add("show"));
  setTimeout(() => { t.classList.remove("show"); setTimeout(() => t.remove(), 300); }, 2500);
}

// Tabs
document.querySelectorAll(".nav-item").forEach(item => {
  item.addEventListener("click", () => {
    document.querySelectorAll(".nav-item").forEach(n => n.classList.remove("active"));
    document.querySelectorAll(".tab-content").forEach(c => c.classList.remove("active"));
    item.classList.add("active");
    document.getElementById(`tab-${item.dataset.tab}`).classList.add("active");
    // 启停对应功能
    stopMonitor();
    stopNews();
    if (item.dataset.tab === "monitor") startMonitor();
    if (item.dataset.tab === "news") startNews();
  });
});

// Config
async function loadConfig() {
  config = await api("/config");
  renderSlots();
  renderSettings();
}

function renderSlots() {
  const grid = document.getElementById("slot-grid");
  grid.innerHTML = "";
  for (let i = 0; i < 6; i++) {
    const slot = config.slots[i] || { id: i + 1, name: "", app_path: "" };
    const card = document.createElement("div");
    if (slot.name && slot.app_path) {
      card.className = "slot-card has-app";
      const iconSrc = slot.icon ? `data:image/png;base64,${slot.icon}` : "";
      card.innerHTML = `
        <span class="slot-id">#${slot.id}</span>
        <button class="slot-remove" data-id="${slot.id}">x</button>
        <div class="slot-icon">${iconSrc ? `<img src="${iconSrc}">` : ""}</div>
        <div class="slot-name">${esc(slot.name)}</div>
        <div class="slot-path">${esc(slot.app_path.split("/").pop())}</div>`;
    } else {
      card.className = "slot-card";
      card.innerHTML = `
        <span class="slot-id">#${slot.id}</span>
        <div class="slot-icon empty">+</div>
        <div class="slot-name" style="color:var(--text2)">点击添加应用</div>`;
    }
    card.addEventListener("click", (e) => {
      if (e.target.classList.contains("slot-remove")) { removeSlot(parseInt(e.target.dataset.id)); return; }
      selectApp(slot.id);
    });
    grid.appendChild(card);
  }
}

let currentSlotId = null;
let allApps = [];

async function selectApp(slotId) {
  currentSlotId = slotId;
  document.getElementById("app-modal").style.display = "flex";
  document.getElementById("app-search").value = "";
  document.getElementById("app-search").focus();
  if (allApps.length === 0) {
    document.getElementById("app-list").innerHTML = "<div style='text-align:center;padding:20px;color:var(--text2)'>加载中...</div>";
    allApps = await api("/list-apps");
  }
  renderAppList(allApps);
}
function closeAppModal() { document.getElementById("app-modal").style.display = "none"; }

function renderAppList(apps) {
  const list = document.getElementById("app-list");
  list.innerHTML = apps.map(app => `
    <div class="app-item" data-path="${esc(app.path)}" data-name="${esc(app.name)}">
      <div class="app-item-icon"><div class="placeholder"></div></div>
      <div><div class="app-item-name">${esc(app.name)}</div><div class="app-item-path">${esc(app.path)}</div></div>
    </div>`).join("");
  list.querySelectorAll(".app-item").forEach(item => {
    item.addEventListener("click", () => pickApp(item.dataset.path, item.dataset.name));
  });
  apps.forEach(async (app, idx) => {
    try {
      const res = await api("/extract-icon", "POST", { path: app.path });
      if (res.icon) {
        const items = list.querySelectorAll(".app-item");
        if (items[idx]) items[idx].querySelector(".app-item-icon").innerHTML = `<img src="data:image/png;base64,${res.icon}">`;
      }
    } catch (e) {}
  });
}

document.getElementById("app-search").addEventListener("input", (e) => {
  renderAppList(allApps.filter(a => a.name.toLowerCase().includes(e.target.value.toLowerCase())));
});

async function pickApp(appPath, appName) {
  const slotId = currentSlotId;
  closeAppModal();
  if (!slotId) { showToast("槽位ID丢失", "error"); return; }
  const slot = { id: slotId, name: appName, app_path: appPath, icon: "" };
  await api("/slots", "POST", slot);
  try {
    const iconRes = await api("/extract-icon", "POST", { path: appPath });
    if (iconRes.icon) { slot.icon = iconRes.icon; await api("/slots", "POST", slot); }
  } catch (err) {}
  config = await api("/config");
  renderSlots();
  showToast(`已添加: ${appName}`, "success");
  currentSlotId = null;
}

async function removeSlot(slotId) {
  await api("/slots", "POST", { id: slotId, name: "", app_path: "", icon: "" });
  config.slots[slotId - 1] = { id: slotId, name: "", app_path: "", icon: "" };
  renderSlots();
  showToast("已移除", "info");
}

// Serial
async function refreshPorts() {
  const ports = await api("/ports");
  const sel = document.getElementById("port-select");
  sel.innerHTML = '<option value="">选择端口...</option>';
  ports.forEach(p => {
    const opt = document.createElement("option");
    opt.value = p.device;
    opt.textContent = p.device;
    sel.appendChild(opt);
  });
}
document.getElementById("btn-refresh").addEventListener("click", refreshPorts);

document.getElementById("btn-connect").addEventListener("click", async () => {
  const port = document.getElementById("port-select").value;
  const status = document.getElementById("conn-status");
  const text = document.getElementById("conn-text");
  if (status.classList.contains("online")) {
    await api("/disconnect", "POST");
    status.className = "status-dot offline";
    text.textContent = "未连接";
    document.getElementById("btn-connect").textContent = "连接";
    showToast("已断开", "info");
    return;
  }
  if (!port) { showToast("请先选择端口", "error"); return; }
  showToast("正在连接...", "info");
  const res = await api("/connect", "POST", { port });
  if (res.ok) {
    status.className = "status-dot online";
    text.textContent = port;
    document.getElementById("btn-connect").textContent = "断开";
    showToast("已连接: " + port, "success");
  } else { showToast("连接失败", "error"); }
});

// Sync
document.getElementById("btn-sync").addEventListener("click", async () => {
  const btn = document.getElementById("btn-sync");
  btn.textContent = "同步中..."; btn.disabled = true;
  try {
    const res = await api("/sync", "POST");
    showToast(res.ok ? "同步完成" : (res.error || "同步失败"), res.ok ? "success" : "error");
  } catch (e) { showToast("请先连接设备", "error"); }
  btn.textContent = "同步到设备"; btn.disabled = false;
});

document.getElementById("btn-save").addEventListener("click", async () => {
  await api("/config", "POST", config);
  showToast("配置已保存", "success");
});

// Monitor
function startMonitor() {
  updateMonitor();
  loadFinance();
  monitorTimer = setInterval(updateMonitor, 2000);
  financeTimer = setInterval(loadFinance, 5000);
  // SSE 实时行情（可选，轮询兜底）
  if (!financeSSE) {
    financeSSE = new EventSource(`${API}/finance-stream`);
    financeSSE.onmessage = (e) => {
      try {
        const data = JSON.parse(e.data);
        renderFinanceData(data);
      } catch (err) {}
    };
    financeSSE.onerror = () => { financeSSE.close(); financeSSE = null; };
  }
}
function stopMonitor() {
  if (monitorTimer) clearInterval(monitorTimer);
  if (financeTimer) clearInterval(financeTimer);
  if (financeSSE) { financeSSE.close(); financeSSE = null; }
}

function startNews() {
  loadNews();
  loadCalendar();
  if (!newsSSE) {
    newsSSE = new EventSource(`${API}/news-stream`);
    newsSSE.onmessage = (e) => {
      try {
        const items = JSON.parse(e.data);
        renderNews(items);
      } catch (err) {}
    };
    newsSSE.onerror = () => { newsSSE.close(); newsSSE = null; };
  }
}
function stopNews() {
  if (newsSSE) { newsSSE.close(); newsSSE = null; }
}

async function updateMonitor() {
  const d = await api("/monitor");
  document.getElementById("cpu-percent").textContent = d.cpu_percent + "%";
  document.getElementById("cpu-bar").style.width = d.cpu_percent + "%";
  document.getElementById("cpu-temp").textContent = d.cpu_temp || "--";
  document.getElementById("mem-percent").textContent = d.mem_percent + "%";
  document.getElementById("mem-bar").style.width = d.mem_percent + "%";
  document.getElementById("mem-used").textContent = d.mem_used;
  document.getElementById("mem-total").textContent = d.mem_total;
  document.getElementById("gpu-percent").textContent = d.gpu_percent + "%";
  document.getElementById("gpu-bar").style.width = d.gpu_percent + "%";
  document.getElementById("gpu-temp").textContent = d.gpu_temp || "--";
  document.getElementById("disk-percent").textContent = d.disk_percent + "%";
  document.getElementById("disk-bar").style.width = d.disk_percent + "%";
  document.getElementById("net-up").textContent = d.net_up;
  document.getElementById("net-down").textContent = d.net_down;
}

// Finance
function openFinanceModal(type) {
  currentFinanceType = type;
  const titles = { us_stock: "添加美股", kr_stock: "添加韩股", crypto: "添加虚拟货币" };
  document.getElementById("finance-modal-title").textContent = titles[type] || "添加";
  document.getElementById("finance-search").value = "";
  document.getElementById("finance-suggestions").innerHTML = "";
  document.getElementById("finance-modal").style.display = "flex";
  document.getElementById("finance-search").focus();
}
function closeFinanceModal() { document.getElementById("finance-modal").style.display = "none"; }

document.getElementById("finance-search").addEventListener("input", (e) => {
  const q = e.target.value.trim();
  const box = document.getElementById("finance-suggestions");
  if (q.length < 1) { box.innerHTML = ""; return; }
  clearTimeout(searchDebounce);
  searchDebounce = setTimeout(async () => {
    const res = await fetch(`${API}/search-symbol?q=${encodeURIComponent(q)}&type=${currentFinanceType}`);
    const items = await res.json();
    box.innerHTML = items.length === 0
      ? '<div class="suggest-empty">无结果</div>'
      : items.map(it => `
        <div class="suggest-item" data-symbol="${it.symbol}">
          <span class="suggest-sym">${it.symbol}</span>
          <span class="suggest-name">${it.name || ""}</span>
        </div>`).join("");
    box.querySelectorAll(".suggest-item").forEach(el => {
      el.addEventListener("click", () => addFinanceFromSuggest(el.dataset.symbol));
    });
  }, 300);
});

async function addFinanceFromSuggest(symbol) {
  closeFinanceModal();
  showToast("正在添加 " + symbol + "...", "info");
  const res = await api("/watchlist/add", "POST", { type: currentFinanceType, symbol });
  if (res.ok) {
    // 先显示占位
    const gridId = currentFinanceType === "us_stock" ? "us-stock-grid" :
                   currentFinanceType === "kr_stock" ? "kr-stock-grid" : "crypto-grid";
    const grid = document.getElementById(gridId);
    if (grid) {
      const empty = grid.querySelector(".finance-empty");
      if (empty) empty.remove();
      const placeholder = document.createElement("div");
      placeholder.className = "finance-row";
      placeholder.id = "loading-" + symbol;
      placeholder.innerHTML = `<span class="finance-symbol">${symbol}</span><span class="finance-change">加载中...</span>`;
      grid.appendChild(placeholder);
    }
    showToast("已添加 " + symbol + "，正在获取数据...", "success");
    // 数据会通过 SSE 自动更新
  } else {
    showToast("添加失败", "error");
  }
}

async function removeFinance(type, symbol) {
  const res = await api("/watchlist/remove", "POST", { type, symbol });
  if (res.ok && res.data) {
    renderFinanceData(res.data);
    showToast("已移除 " + symbol, "info");
  }
}

function fmtChange(val) {
  if (val == null) return "--";
  const cls = val >= 0 ? "up" : "down";
  const sign = val >= 0 ? "+" : "";
  return `<span class="finance-change ${cls}">${sign}${val.toFixed(2)}%</span>`;
}

function renderFinanceData(data) {
  function renderList(items, gridId, type) {
    const grid = document.getElementById(gridId);
    if (!grid) return;
    if (!items || Object.keys(items).length === 0) {
      grid.innerHTML = '<div class="finance-empty">暂无数据</div>';
      return;
    }
    grid.innerHTML = Object.entries(items).map(([sym, d]) => {
      const cur = type === "crypto" ? "$" : (d.currency === "KRW" ? "\u20a9" : "$");
      const nameStr = d.name ? `<span class="finance-name">${esc(d.name)}</span>` : "";
      const session = d.session || "";
      const sessionCls = session === "盘前" ? "pre" : (session === "盘后" ? "post" : (session === "休市" ? "closed" : ""));
      const sessionLabel = session ? `<span class="session-tag ${sessionCls}">${session}</span>` : "";

      const rPrice = d.regular_price || d.price;
      const rChange = d.regular_change != null ? d.regular_change : d.change;
      const rStr = rPrice >= 1000 ? rPrice.toLocaleString() : rPrice;

      let lines = [];
      if (type === "crypto") {
        lines.push(`<div>${cur}${rStr} ${fmtChange(rChange)}</div>`);
      } else {
        lines.push(`<div><span class="price-label">盘中</span> ${cur}${rStr} ${fmtChange(rChange)}</div>`);
        if (d.pre_price) {
          const pStr = d.pre_price >= 1000 ? d.pre_price.toLocaleString() : d.pre_price;
          const pC = d.regular_price ? ((d.pre_price - d.regular_price) / d.regular_price * 100) : 0;
          lines.push(`<div><span class="price-label">盘前</span> ${cur}${pStr} ${fmtChange(pC)}</div>`);
        }
        if (d.post_price) {
          const pStr = d.post_price >= 1000 ? d.post_price.toLocaleString() : d.post_price;
          const pC = d.regular_price ? ((d.post_price - d.regular_price) / d.regular_price * 100) : 0;
          lines.push(`<div><span class="price-label">盘后</span> ${cur}${pStr} ${fmtChange(pC)}</div>`);
        }
      }

      return `<div class="finance-row">
        <button class="finance-remove" onclick="removeFinance('${esc(type)}','${esc(sym)}')">x</button>
        <div class="finance-left">
          <span class="finance-symbol">${esc(sym)}</span>${nameStr}${sessionLabel}
        </div>
        <div class="finance-prices">${lines.join("")}</div>
      </div>`;
    }).join("");
  }
  renderList(data.us_stocks, "us-stock-grid", "us_stock");
  renderList(data.kr_stocks, "kr-stock-grid", "kr_stock");
  renderList(data.crypto, "crypto-grid", "crypto");
}

async function loadFinance() {
  const data = await api("/finance");
  renderFinanceData(data);
}

// News
function parseTime(timeStr) {
  if (!timeStr) return 0;
  try {
    return new Date(timeStr).getTime() || 0;
  } catch { return 0; }
}

function fmtTime(timeStr) {
  if (!timeStr) return "刚刚";
  try {
    const d = new Date(timeStr);
    if (isNaN(d.getTime())) return timeStr || "刚刚";
    const now = new Date();
    const diff = now - d;
    if (diff < 60000) return "刚刚";
    if (diff < 3600000) return Math.floor(diff / 60000) + "分钟前";
    if (diff < 86400000) return Math.floor(diff / 3600000) + "小时前";
    return d.toLocaleDateString("zh-CN", { month: "numeric", day: "numeric" }) + " " +
           d.toLocaleTimeString("zh-CN", { hour: "2-digit", minute: "2-digit" });
  } catch { return ""; }
}

function renderNews(items) {
  const list = document.getElementById("news-list");
  if (!list) return;
  if (!items || items.length === 0) {
    list.innerHTML = '<div class="finance-empty">暂无新闻</div>';
    return;
  }
  // 按时间排序，最新在前
  items.sort((a, b) => parseTime(b.time) - parseTime(a.time));

  const useZh = document.getElementById("news-translate")?.checked !== false;
  list.innerHTML = items.map(item => {
    const tag = item.category ? `<span class="news-tag">${esc(item.category)}</span>` : "";
    const srcTag = `<span class="news-tag" style="background:#f0fdf4;color:#16a34a">${esc(item.source)}</span>`;
    const timeTag = `<span class="news-time">${esc(fmtTime(item.time))}</span>`;
    const title = useZh && item.title_zh ? item.title_zh : item.title;
    const desc = useZh && item.desc_zh ? item.desc_zh : item.desc;
    const safeLink = esc(item.link || "");
    return `
      <div class="news-item" data-link="${safeLink}">
        <div class="news-title">${esc(title)}</div>
        ${desc ? `<div class="news-desc">${esc(desc)}</div>` : ""}
        <div class="news-meta">${srcTag}${tag}${timeTag}</div>
      </div>`;
  }).join("");
  // 用事件委托替代 inline onclick，防止 XSS
  list.querySelectorAll(".news-item").forEach(el => {
    el.addEventListener("click", () => {
      const link = el.dataset.link;
      if (link) window.open(link, "_blank");
    });
  });
}

async function loadNews() {
  const items = await api("/news");
  renderNews(items);
}

document.getElementById("news-translate")?.addEventListener("change", () => loadNews());

// Calendar
async function loadCalendar() {
  const items = await api("/calendar");
  const list = document.getElementById("calendar-list");
  if (!list) return;
  if (!items || items.length === 0) {
    list.innerHTML = '<div class="finance-empty">加载中...</div>';
    return;
  }

  // 按日期分组
  const grouped = {};
  items.forEach(item => {
    const d = new Date(item.date);
    const key = d.toLocaleDateString("zh-CN", { month: "numeric", day: "numeric", weekday: "short" });
    if (!grouped[key]) grouped[key] = [];
    grouped[key].push(item);
  });

  list.innerHTML = Object.entries(grouped).map(([date, events]) => {
    return `<div class="calendar-date-header" style="font-size:12px;font-weight:700;color:var(--text);padding:8px 12px 4px;background:var(--surface2);margin-top:8px;border-radius:8px 8px 0 0;">${date}</div>` +
      events.map(item => {
        const time = new Date(item.date).toLocaleTimeString("zh-CN", { hour: "2-digit", minute: "2-digit" });
        const countryCls = item.country || "";
        const impactCls = item.impact || "";
            const nameZh = item.event_zh || item.event;
            const nameEn = item.event_zh ? `<span style="color:var(--text2);font-size:11px;margin-left:4px">${item.event}</span>` : "";
            return `
              <div class="calendar-item">
                <div class="calendar-detail">
                  <span class="calendar-country ${countryCls}">${item.country}</span>
                  <span class="calendar-impact ${impactCls}">${item.impact === "High" ? "!!!" : "!"}</span>
                  <span>${time}</span>
                </div>
                <div class="calendar-event">${nameZh}${nameEn}</div>
                <div class="calendar-detail">
                  <span>预期: ${item.forecast || "-"}</span>
                  <span>前值: ${item.previous || "-"}</span>
                  ${item.actual ? `<span style="color:var(--success)">实际: ${item.actual}</span>` : ""}
                </div>
              </div>`;
      }).join("");
  }).join("");
}

// Settings
function renderSettings() {
  document.getElementById("cfg-timeout").value = config.timeout || 30;
  document.getElementById("cfg-baud").value = config.baud_rate || 115200;
}
document.getElementById("btn-save-settings").addEventListener("click", async () => {
  config.timeout = parseInt(document.getElementById("cfg-timeout").value);
  config.baud_rate = parseInt(document.getElementById("cfg-baud").value);
  await api("/config", "POST", config);
  showToast("设置已保存", "success");
});

// Wallpaper
let wallpaperDataUrl = null;
document.getElementById('cfg-wallpaper').addEventListener('change', (e) => {
  const file = e.target.files[0];
  if (!file) return;
  const reader = new FileReader();
  reader.onload = (ev) => {
    wallpaperDataUrl = ev.target.result;
    const preview = document.getElementById('wallpaper-preview');
    preview.src = ev.target.result;
    preview.style.display = 'block';
  };
  reader.readAsDataURL(file);
});
document.getElementById('btn-send-wallpaper').addEventListener('click', async () => {
  if (!wallpaperDataUrl) { alert('请先选择壁纸文件'); return; }
  if (!confirm("即将通过串口发送壁纸，约10~30秒。是否继续？")) return;
  try {
    const res = await fetch('/api/transfer-wallpaper', {
      method: 'POST', headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({data: wallpaperDataUrl})
    });
    const data = await res.json();
    alert(data.ok ? data.message : '错误: ' + (data.error||'unknown'));
  } catch (e) { alert('请求失败'); }
});

// Init
loadConfig();
refreshPorts();

document.getElementById('btn-transfer-font').addEventListener('click', async () => {
  if (confirm("即将通过串口发送字库到单片机，耗时约1~3分钟。期间请勿断开连接。\n\n您可以在电脑终端看到详细的发送进度！\n是否继续？")) {
    try {
      const res = await fetch('/api/transfer-font', { method: 'POST' });
      const data = await res.json();
      if (data.ok) {
        alert(data.message);
      } else {
        alert('错误: ' + data.error);
      }
    } catch (e) {
      alert('请求失败');
    }
  }
});
