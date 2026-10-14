/**
 * =====================================================================
 *  CYD INA228 Power Lab - W pelni przetestowany kod dla ESP32-2432S028
 * =====================================================================
 *  Funkcje:
 *  1. Interaktywny kreator kalibracji dotyku (2 celowniki na ekranie)
 *  2. Trwaly zapis kalibracji w pamieci Flash (Preferences.h)
 *  3. Automatyczny skaner sieci 2.4 GHz wypisujacy sygnaly do Serial
 *  4. Prawdziwe ciemne tlo (tft.invertDisplay(false))
 *  5. Bezpieczny buzzer bez bledow sterownika LEDC
 *  6. Bezpieczny WebServer z API JSON (R"raw literals)
 */

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SD.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "ina228.h"

// =====================================================================
// 📡 1. KONFIGURACJA WI-FI & PAMIEC FLASH (NVS)
// =====================================================================
// Wartosci domyslne (uzywane tylko jesli we Flashu nie wybrano jeszcze sieci):
const char *WIFI_SSID_DEFAULT = "AP";
const char *WIFI_PASSWORD_DEFAULT = "Haslo";

// Wlasny Access Point awaryjny / konfiguracyjny (192.168.4.1):
const char *AP_SSID = "CYD-INA228-LAB";
const char *AP_PASSWORD = "12345678";

String activeSsid = "";
String activePass = "";
bool hasSavedWifi = false;

// =====================================================================
// 🔌 2. PINY SPRZETOWE CYD (ESP32-2432S028)
// =====================================================================
#define I2C_SDA_PIN 27
#define I2C_SCL_PIN 22
#define SD_CS_PIN 5
#define BUZZER_PIN 26
#define LED_RED_PIN 4
#define LED_GRN_PIN 16
#define LED_BLU_PIN 17

#define XPT_CLK_PIN 25
#define XPT_MISO_PIN 39
#define XPT_MOSI_PIN 32
#define XPT_CS_PIN 33
#define XPT_IRQ_PIN 36

#define SHUNT_RESISTOR 0.015f
#define MAX_CURRENT 5.0f

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSpi = SPIClass(HSPI);
XPT2046_Touchscreen touch(XPT_CS_PIN, XPT_IRQ_PIN);
INA228_Sensor ina(INA228_I2C_ADDR_DEFAULT);
WebServer server(80);
Preferences prefs;

// =====================================================================
// 🎯 3. ZMIENNE KALIBRACJI DOTYKU (PAMIEC FLASH)
// =====================================================================
int cal_p1_x = 3800; // Raw X dla punktu (x=30)
int cal_p1_y = 3800; // Raw Y dla punktu (y=30)
int cal_p2_x = 300;  // Raw X dla punktu (x=290)
int cal_p2_y = 300;  // Raw Y dla punktu (y=210)
bool hasCalibration = false;
bool displayInverted = true; // Domyślnie TRUE = Ciemne tło na matrycach IPS/CYD!

// =====================================================================
// 🎨 4. KOLORYSTYKA DARK MODE (RGB565)
// =====================================================================
#define COLOR_DARK_BG 0x0000  // Prawdziwa gleboka czern
#define COLOR_CARD_BG 0x0000  // Ciemniejsze (czarne) tlo kafelkow pomiarowych (czern OLED)
#define COLOR_HEADER 0x10C4   // Ciemny grafit naglowka
#define COLOR_SKY 0x3E1F      // Blekitny
#define COLOR_GREEN 0x36E8    // Szmaragdowy
#define COLOR_AMBER 0xFDE0    // Bursztynowy
#define COLOR_PURPLE 0xB43F   // Fioletowy
#define COLOR_DARK_RED 0x7800 // Ciemno czerwone tlo alarmu braku czujnika

// =====================================================================
// 🌐 5. STRONA WWW (PROGMEM)
// =====================================================================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pl">
<head>
  <meta charset="UTF-8">
  <title>CYD INA228 Power Lab</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { background: #090d16; color: #f1f5f9; font-family: -apple-system, sans-serif; padding: 20px; }
    .container { max-width: 1000px; margin: 0 auto; }
    .header { display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid #1e293b; padding-bottom: 16px; margin-bottom: 20px; flex-wrap: wrap; gap: 10px; }
    .brand h1 { font-size: 1.3rem; font-weight: 800; color: #fff; }
    .badge { display: inline-flex; align-items: center; gap: 6px; background: rgba(16,185,129,0.15); color: #34d399; border: 1px solid rgba(16,185,129,0.3); padding: 4px 12px; border-radius: 9999px; font-size: 0.8rem; font-family: monospace; }
    .dot { width: 8px; height: 8px; border-radius: 50%; background: #10b981; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(210px, 1fr)); gap: 16px; margin-bottom: 20px; }
    .card { background: #0f172a; border: 1px solid #1e293b; border-radius: 12px; padding: 18px; box-shadow: 0 4px 12px rgba(0,0,0,0.4); display: flex; flex-direction: column; justify-content: space-between; }
    .card .label { font-size: 0.75rem; text-transform: uppercase; color: #94a3b8; font-family: monospace; display: flex; justify-content: space-between; margin-bottom: 8px; }
    .card .val { font-size: 2rem; font-weight: 800; font-family: monospace; margin-bottom: 8px; }
    .card .sub { font-size: 0.75rem; color: #64748b; font-family: monospace; border-top: 1px solid #1e293b; padding-top: 8px; display: flex; justify-content: space-between; }
    .volt { color: #38bdf8; } .curr { color: #4ade80; } .power { color: #fbbf24; } .temp { color: #fb923c; }
    .chart-section { background: #0f172a; border: 1px solid #1e293b; border-radius: 12px; padding: 18px; margin-bottom: 20px; }
    .chart-box { position: relative; width: 100%; height: 160px; background: #060913; border: 1px solid #1e293b; border-radius: 8px; overflow: hidden; }
    svg { width: 100%; height: 100%; }
    .bottom-row { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 16px; }
    .stats-card { background: #0f172a; border: 1px solid #1e293b; border-radius: 12px; padding: 18px; }
    .stats-card h3 { font-size: 0.85rem; text-transform: uppercase; font-family: monospace; color: #94a3b8; border-bottom: 1px solid #1e293b; padding-bottom: 8px; margin-bottom: 12px; }
    .stat-row { display: flex; justify-content: space-between; font-family: monospace; font-size: 0.9rem; padding: 6px 0; border-bottom: 1px solid rgba(30,41,59,0.5); }
    .actions { display: flex; flex-direction: column; gap: 10px; margin-top: 16px; }
    button, a.btn { display: block; text-align: center; text-decoration: none; padding: 10px 16px; border-radius: 8px; font-weight: bold; font-size: 0.85rem; cursor: pointer; border: none; }
    .btn-reset { background: rgba(225,29,72,0.2); color: #fb7185; border: 1px solid rgba(225,29,72,0.4); }
    .btn-download { background: rgba(14,165,233,0.2); color: #38bdf8; border: 1px solid rgba(14,165,233,0.4); }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <div><h1>⚡ CYD INA228 Power Lab</h1><span style="font-size: 0.8rem; color: #94a3b8; font-family: monospace;">ESP32-2432S028 @ TI INA228 20-Bit</span></div>
      <div class="badge" id="conn-badge"><div class="dot" id="conn-dot"></div><span id="conn-text">Polaczono z ESP32</span></div>
    </div>
    <div class="grid">
      <div class="card"><div class="label"><span>Napiecie VBUS</span><span style="color: #38bdf8;">195uV LSB</span></div><div class="val volt" id="v">--.---- V</div><div class="sub"><span>Zakres: 0..85V</span><span>20-bit</span></div></div>
      <div class="card"><div class="label"><span>Prad Obciazenia</span><span id="dir-badge" style="color: #4ade80;">Pobor</span></div><div class="val curr" id="i">--.---- A</div><div class="sub"><span id="i-ma">--.- mA</span><span id="vshunt-uv">Shunt: -- uV</span></div></div>
      <div class="card"><div class="label"><span>Moc Czynna</span><span style="color: #fbbf24;">POWER</span></div><div class="val power" id="p">--.--- W</div><div class="sub"><span>Rezystancja:</span><span id="res">--.-- Ohm</span></div></div>
      <div class="card"><div class="label"><span>Temp. INA228</span><span style="color: #fb923c;">TEMPCO WL</span></div><div class="val temp" id="t">--.-- C</div><div class="sub"><span>Krok: 7.8 mC</span><span>Bocznik: 15mOhm</span></div></div>
    </div>
    <div class="chart-section">
      <div style="display:flex; justify-content:space-between; font-family:monospace; margin-bottom:10px;"><span style="color:#94a3b8;">PRZEBIEG PRADU NA ZYWO I(t)</span><span style="color:#38bdf8;" id="chart-curr">0.0000 A</span></div>
      <div class="chart-box">
        <svg preserveAspectRatio="none" viewBox="0 0 100 100">
          <line x1="0" y1="25" x2="100" y2="25" stroke="#1e293b" stroke-width="0.8" />
          <line x1="0" y1="50" x2="100" y2="50" stroke="#1e293b" stroke-width="0.8" />
          <line x1="0" y1="75" x2="100" y2="75" stroke="#1e293b" stroke-width="0.8" />
          <polyline id="wave" fill="none" stroke="#38bdf8" stroke-width="2.5" stroke-linecap="round" points="0,90 100,90" />
        </svg>
      </div>
    </div>
    <div class="bottom-row">
      <div class="stats-card">
        <h3>Licznik Energii i Baterii</h3>
        <div class="stat-row"><span>Pojemnosc:</span><strong style="color: #4ade80;" id="mah">0.00 mAh</strong></div>
        <div class="stat-row"><span>Energia:</span><strong style="color: #fbbf24;" id="wh">0.000 Wh</strong></div>
        <div class="stat-row"><span>MicroSD:</span><span id="sd-status" style="color: #38bdf8;">Status</span></div>
        <div class="actions" style="margin-top: 10px;">
          <button class="btn-reset" onclick="resetCounter()">Zresetuj licznik</button>
          <a class="btn btn-download" href="/data.csv" download="data.csv">Pobierz plik CSV</a>
        </div>
      </div>
      <div class="stats-card">
        <h3>📶 Wybor Sieci Wi-Fi (Pamiec Flash)</h3>
        <div class="stat-row" style="margin-bottom: 8px;">
          <span>Aktywna siec:</span>
          <strong style="color: #38bdf8;" id="cur-ssid">Wczytywanie...</strong>
        </div>
        <div style="display:flex; gap:6px; margin-bottom:8px;">
          <select id="wifi-select" style="flex:1; background:#0f172a; color:#f8fafc; border:1px solid #334155; padding:8px; border-radius:6px; font-family:monospace; font-size:12px;">
            <option value="">-- Kliknij [Skanuj] aby wykryc sieci --</option>
          </select>
          <button id="btn-scan" style="background:#0284c7; padding:8px 12px; font-size:12px;" onclick="scanWifi()">Skanuj</button>
        </div>
        <div style="margin-bottom:8px;">
          <input type="password" id="wifi-pass" placeholder="Wpisz haslo do wybranej sieci" style="width:100%; box-sizing:border-box; background:#0f172a; color:#f8fafc; border:1px solid #334155; padding:8px; border-radius:6px; font-family:monospace; font-size:12px;">
        </div>
        <button style="width:100%; background:#10b981; font-weight:bold; padding:10px;" onclick="saveWifi()">Zapisz we Flashu i Polacz</button>
      </div>
    </div>
  </div>
  <script>
    const historyPoints = []; const MAX_POINTS = 30;
    async function updateData() {
      try {
        const res = await fetch('/api/data'); if (!res.ok) throw new Error(); const d = await res.json();
        if (d.sensor === false) {
          document.getElementById('conn-text').innerText = 'Brak czujnika INA228 !!!';
          document.getElementById('conn-dot').style.background = '#ef4444';
          document.getElementById('conn-badge').style.borderColor = '#ef4444';
          document.getElementById('conn-badge').style.color = '#f87171';
        } else {
          document.getElementById('conn-text').innerText = 'Polaczono z ESP32 & INA228';
          document.getElementById('conn-dot').style.background = '#10b981';
          document.getElementById('conn-badge').style.borderColor = 'rgba(16,185,129,0.3)';
          document.getElementById('conn-badge').style.color = '#34d399';
        }
        document.getElementById('v').innerText = d.v.toFixed(4) + ' V';
        document.getElementById('i').innerText = (d.i >= 0 ? '+' : '') + d.i.toFixed(4) + ' A';
        document.getElementById('p').innerText = d.p.toFixed(3) + ' W';
        document.getElementById('t').innerText = d.t.toFixed(2) + ' C';
        document.getElementById('mah').innerText = d.mah.toFixed(2) + ' mAh';
        document.getElementById('wh').innerText = d.wh.toFixed(4) + ' Wh';
        document.getElementById('dir-badge').innerText = d.i >= 0 ? 'Pobor' : 'Ladowanie';
        document.getElementById('i-ma').innerText = (d.i * 1000).toFixed(1) + ' mA';
        document.getElementById('vshunt-uv').innerText = 'Shunt: ' + (d.shunt_uv ? d.shunt_uv.toFixed(1) : (d.i * 15000).toFixed(1)) + ' uV';
        document.getElementById('res').innerText = Math.abs(d.i) > 0.001 ? (d.v / Math.abs(d.i)).toFixed(2) + ' Ohm' : '--- Ohm';
        document.getElementById('chart-curr').innerText = d.i.toFixed(4) + ' A';
        if (d.ssid) document.getElementById('cur-ssid').innerText = d.ssid;
        historyPoints.push(Math.abs(d.i));
        if (historyPoints.length > MAX_POINTS) historyPoints.shift();
        const pts = historyPoints.map((val, idx) => {
          const x = (idx / (MAX_POINTS - 1)) * 100;
          const y = Math.max(5, Math.min(95, 95 - (val / 5.0) * 85));
          return x.toFixed(1) + ',' + y.toFixed(1);
        }).join(' ');
        document.getElementById('wave').setAttribute('points', pts);
      } catch (e) {
        document.getElementById('conn-text').innerText = 'Brak polaczenia';
        document.getElementById('conn-dot').style.background = '#ef4444';
      }
    }
    async function resetCounter() {
      if (confirm('Czy na pewno zresetowac licznik?')) {
        await fetch('/api/reset', { method: 'POST' }); updateData();
      }
    }
    async function scanWifi() {
      const btn = document.getElementById('btn-scan');
      btn.innerText = '...'; btn.disabled = true;
      try {
        const res = await fetch('/api/wifi/scan');
        const list = await res.json();
        const sel = document.getElementById('wifi-select');
        sel.innerHTML = '';
        if (list.length === 0) {
          sel.innerHTML = '<option value="">Brak wykrytych sieci 2.4 GHz</option>';
        } else {
          list.forEach(net => {
            const opt = document.createElement('option');
            opt.value = net.ssid;
            opt.text = (net.auth ? '[ZABEZPIECZONA] ' : '[OTWARTA] ') + net.ssid + ' (' + net.rssi + ' dBm)';
            sel.appendChild(opt);
          });
        }
      } catch (e) {
        alert('Blad skanowania sieci');
      } finally {
        btn.innerText = 'Skanuj'; btn.disabled = false;
      }
    }
    async function saveWifi() {
      const sel = document.getElementById('wifi-select');
      const ssid = sel.value;
      const pass = document.getElementById('wifi-pass').value;
      if (!ssid) { alert('Wybierz najpierw siec z listy lub wpisz ja!'); return; }
      if (!confirm('Czy zapisac siec ' + ssid + ' na stale we Flashu i polaczyc?')) return;
      try {
        const res = await fetch('/api/wifi/save?ssid=' + encodeURIComponent(ssid) + '&pass=' + encodeURIComponent(pass));
        const d = await res.json();
        if (d.status === 'saved') {
          alert('Zapisano w pamieci Flash! ESP32 uruchamia sie ponownie i laczy z: ' + ssid + '. Sprawdz adres IP na ekranie plytki!');
        }
      } catch (e) {
        alert('Wyslano konfiguracje. Sprawdz ekran ESP32.');
      }
    }
    setInterval(updateData, 500); updateData();
  </script>
</body>
</html>
)rawliteral";

// =====================================================================
// 📊 6. ZMIENNE APLIKACJI
// =====================================================================
enum ScreenTab
{
  TAB_DASHBOARD = 0,
  TAB_CHART,
  TAB_BATTERY,
  TAB_PRECISION
};

ScreenTab currentTab = TAB_DASHBOARD;
ScreenTab lastDrawnTab = (ScreenTab)99;
bool ultraPrecisionMode = true;

double vBus = 0.0;
double currentA = 0.0;
double powerW = 0.0;
double vShunt_uV = 0.0;
float tempC = 0.0f;
double resistanceOhm = 0.0;

double total_mAh = 0.0;
double total_Wh = 0.0;
uint32_t sessionStartTime = 0;
bool isLoggingSD = false;

#define GRAPH_POINTS 40
float currentHistory[GRAPH_POINTS] = {0};
int graphIndex = 0;

bool sensorConnected = false;
unsigned long lastSensorCheck = 0;
unsigned long lastBlinkToggle = 0;
bool errorTextVisible = true;
bool wasSensorErrorDrawn = false;

unsigned long lastSampleTime = 0;
unsigned long lastUiRefresh = 0;
unsigned long lastSdLogTime = 0;
String activeIpAddress = "192.168.4.1";
bool isApMode = true;

void setRgbLed(bool r, bool g, bool b)
{
  digitalWrite(LED_RED_PIN, r ? LOW : HIGH);
  digitalWrite(LED_GRN_PIN, g ? LOW : HIGH);
  digitalWrite(LED_BLU_PIN, b ? LOW : HIGH);
}

// Czysta obsluga buzzera bez bledow LEDC:
void beep(int freq = 2000, int durationMs = 35)
{
  ledcSetup(0, freq, 8);
  ledcAttachPin(BUZZER_PIN, 0);
  ledcWrite(0, 128);
  delay(durationMs);
  ledcWrite(0, 0);
  ledcDetachPin(BUZZER_PIN);
  digitalWrite(BUZZER_PIN, LOW);
}

// Bezpośrednie, samowystarczalne sprawdzenie obecności układu INA228 na magistrali I2C (niezależne od wersji pliku ina228.h)
bool checkIna228Present()
{
  Wire.beginTransmission(0x40);
  if (Wire.endTransmission() != 0)
  {
    return false;
  }
  // Weryfikacja rejestru MANUF_ID (0x3E == 0x5449 Texas Instruments)
  Wire.beginTransmission(0x40);
  Wire.write(0x3E);
  if (Wire.endTransmission() != 0)
    return false;
  if (Wire.requestFrom((uint8_t)0x40, (uint8_t)2) != 2)
    return false;
  uint16_t id = ((uint16_t)Wire.read() << 8) | Wire.read();
  return (id == 0x5449);
}

void runTouchCalibration();

// =====================================================================
// 🎯 7. ODCZYT I ZAPIS KALIBRACJI Z FLASH
// =====================================================================
void loadTouchCalibration()
{
  prefs.begin("touch_cal", false);
  hasCalibration = prefs.getBool("valid", false);
  displayInverted = prefs.getBool("disp_inv", true);
  tft.invertDisplay(displayInverted);
  if (hasCalibration)
  {
    cal_p1_x = prefs.getInt("p1_x", 3800);
    cal_p1_y = prefs.getInt("p1_y", 3800);
    cal_p2_x = prefs.getInt("p2_x", 300);
    cal_p2_y = prefs.getInt("p2_y", 300);
    Serial.print("[KALIBRACJA] Zaladowano z Flash: P1=(");
    Serial.print(cal_p1_x);
    Serial.print(",");
    Serial.print(cal_p1_y);
    Serial.print(") P2=(");
    Serial.print(cal_p2_x);
    Serial.print(",");
    Serial.print(cal_p2_y);
    Serial.println(")");
  }
  else
  {
    Serial.println("[KALIBRACJA] Brak trwalej kalibracji we Flash.");
  }
  prefs.end();
}

void saveTouchCalibration()
{
  prefs.begin("touch_cal", false);
  prefs.putInt("p1_x", cal_p1_x);
  prefs.putInt("p1_y", cal_p1_y);
  prefs.putInt("p2_x", cal_p2_x);
  prefs.putInt("p2_y", cal_p2_y);
  prefs.putBool("valid", true);
  prefs.putBool("disp_inv", displayInverted);
  prefs.end();
  hasCalibration = true;
  Serial.print("[KALIBRACJA] Zapisano trwale we Flash: P1=(");
  Serial.print(cal_p1_x);
  Serial.print(",");
  Serial.print(cal_p1_y);
  Serial.print(") P2=(");
  Serial.print(cal_p2_x);
  Serial.print(",");
  Serial.print(cal_p2_y);
  Serial.println(")");
}

// =====================================================================
// 🎯 8. KREATOR KALIBRACJI DOTYKU (CELOWNIKI)
// =====================================================================
void drawCrosshair(int x, int y, uint16_t color)
{
  tft.drawCircle(x, y, 12, color);
  tft.drawCircle(x, y, 6, color);
  tft.drawLine(x - 16, y, x + 16, y, color);
  tft.drawLine(x, y - 16, x, y + 16, color);
}

void runTouchCalibration()
{
  Serial.println();
  Serial.println("--- Start kalibracji dotyku CYD ---");
  tft.fillScreen(COLOR_DARK_BG);

  // KROK 1: Lewy gorny punkt (30, 30)
  tft.setTextColor(TFT_WHITE, COLOR_DARK_BG);
  tft.drawCentreString("KREATOR KALIBRACJI DOTYKU", 160, 70, 2);
  tft.setTextColor(COLOR_SKY, COLOR_DARK_BG);
  tft.drawCentreString("1/2: Dotknij celownik", 160, 100, 2);
  tft.setTextColor(TFT_YELLOW, COLOR_DARK_BG);
  tft.drawCentreString("w LEWYM GORNYM ROGU", 160, 125, 2);

  drawCrosshair(30, 30, TFT_CYAN);

  while (!touch.touched())
    delay(10);
  long sumX = 0, sumY = 0;
  int count = 0;
  while (touch.touched() && count < 15)
  {
    TS_Point p = touch.getPoint();
    if (p.z > 200)
    {
      sumX += p.x;
      sumY += p.y;
      count++;
    }
    delay(20);
  }
  if (count > 0)
  {
    cal_p1_x = sumX / count;
    cal_p1_y = sumY / count;
  }
  beep(2400, 60);
  while (touch.touched())
    delay(10);
  delay(300);

  // KROK 2: Prawy dolny punkt (290, 210)
  tft.fillScreen(COLOR_DARK_BG);
  tft.setTextColor(TFT_WHITE, COLOR_DARK_BG);
  tft.drawCentreString("KREATOR KALIBRACJI DOTYKU", 160, 70, 2);
  tft.setTextColor(COLOR_GREEN, COLOR_DARK_BG);
  tft.drawCentreString("2/2: Dotknij celownik", 160, 100, 2);
  tft.setTextColor(TFT_YELLOW, COLOR_DARK_BG);
  tft.drawCentreString("w PRAWYM DOLNYM ROGU", 160, 125, 2);

  drawCrosshair(290, 210, TFT_GREEN);

  while (!touch.touched())
    delay(10);
  sumX = 0;
  sumY = 0;
  count = 0;
  while (touch.touched() && count < 15)
  {
    TS_Point p = touch.getPoint();
    if (p.z > 200)
    {
      sumX += p.x;
      sumY += p.y;
      count++;
    }
    delay(20);
  }
  if (count > 0)
  {
    cal_p2_x = sumX / count;
    cal_p2_y = sumY / count;
  }
  beep(3000, 80);
  while (touch.touched())
    delay(10);
  delay(300);

  saveTouchCalibration();

  tft.fillScreen(COLOR_DARK_BG);
  tft.setTextColor(COLOR_GREEN, COLOR_DARK_BG);
  tft.drawCentreString("KALIBRACJA ZAKONCZONA!", 160, 90, 4);
  tft.setTextColor(TFT_WHITE, COLOR_DARK_BG);
  tft.drawCentreString("Zapisano trwale w pamieci Flash.", 160, 130, 2);
  delay(1500);
}

// =====================================================================
// 🌐 9. OBSŁUGA SERWERA WWW
// =====================================================================
void handleRoot()
{
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleApiData()
{
  char jsonBuf[360];
  snprintf(jsonBuf, sizeof(jsonBuf),
           R"raw({"v":%.4f,"i":%.4f,"p":%.3f,"t":%.2f,"shunt_uv":%.1f,"mah":%.2f,"wh":%.4f,"sd":%s,"sensor":%s,"ssid":"%s"})raw",
           vBus, currentA, powerW, tempC, vShunt_uV, total_mAh, total_Wh,
           isLoggingSD ? "true" : "false",
           sensorConnected ? "true" : "false",
           activeSsid.c_str());
  server.send(200, "application/json", jsonBuf);
}

void handleApiReset()
{
  total_mAh = 0;
  total_Wh = 0;
  sessionStartTime = millis();
  beep(1600, 100);
  server.send(200, "application/json", R"raw({"status":"ok"})raw");
}

void handleWifiScan()
{
  int n = WiFi.scanNetworks(false, false);
  String json = "[";
  char itemBuf[128];
  for (int i = 0; i < n; i++)
  {
    String s = WiFi.SSID(i);
    if (s.length() == 0)
      continue;
    if (json.length() > 1)
      json += ",";
    snprintf(itemBuf, sizeof(itemBuf),
             R"raw({"ssid":"%s","rssi":%d,"auth":%s})raw",
             s.c_str(), WiFi.RSSI(i), (WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false"));
    json += itemBuf;
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleWifiSave()
{
  String newSsid = server.arg("ssid");
  String newPass = server.arg("pass");
  if (newSsid.length() == 0)
  {
    server.send(400, "application/json", R"raw({"status":"error","msg":"Pusta nazwa SSID"})raw");
    return;
  }
  prefs.begin("cyd_wifi", false);
  prefs.putString("ssid", newSsid);
  prefs.putString("pass", newPass);
  prefs.putBool("saved", true);
  prefs.end();

  Serial.print("[Wi-Fi Flash] Zapisano we Flash: ");
  Serial.println(newSsid);

  char resBuf[128];
  snprintf(resBuf, sizeof(resBuf), R"raw({"status":"saved","ssid":"%s"})raw", newSsid.c_str());
  server.send(200, "application/json", resBuf);
  delay(1000);
  ESP.restart();
}

void handleWifiReset()
{
  prefs.begin("cyd_wifi", false);
  prefs.clear();
  prefs.end();
  Serial.println("[Wi-Fi Flash] Zresetowano siec we Flash.");
  server.send(200, "application/json", R"raw({"status":"reset"})raw");
  delay(1000);
  ESP.restart();
}

void handleDownloadCsv()
{
  if (SD.exists("/data.csv"))
  {
    File f = SD.open("/data.csv", FILE_READ);
    server.streamFile(f, "text/csv");
    f.close();
  }
  else
  {
    server.send(404, "text/plain", "Brak pliku /data.csv na karcie SD");
  }
}

// =====================================================================
// 🖥️ 10. RYSOWANIE EKRANU TFT
// =====================================================================
void drawHeader()
{
  tft.fillRect(0, 0, 320, 22, COLOR_HEADER);
  tft.setTextColor(TFT_WHITE, COLOR_HEADER);
  tft.drawString("INA228", 5, 4, 2);

  tft.setTextColor(COLOR_SKY, COLOR_HEADER);
  String ipLabel = (isApMode ? "AP:" : "IP:") + activeIpAddress;
  tft.drawString(ipLabel, 68, 4, 2);

  tft.setTextColor(COLOR_AMBER, COLOR_HEADER);
  char tBuf[16];
  sprintf(tBuf, "%5.2fC", tempC);
  tft.drawRightString(tBuf, 316, 4, 2);
}

void updateHeaderLive()
{
  tft.fillRect(245, 1, 74, 20, COLOR_HEADER);
  tft.setTextColor(COLOR_AMBER, COLOR_HEADER);
  char tBuf[16];
  sprintf(tBuf, "%5.2fC", tempC);
  tft.drawRightString(tBuf, 316, 4, 2);
}

void drawBottomTabs()
{
  const char *tabs[4] = {"Pulpit", "Wykres", "Bateria", "Precyzja"};
  int w = 320 / 4;
  for (int i = 0; i < 4; i++)
  {
    uint16_t bg = (currentTab == i) ? 0x037F : 0x10A2;
    uint16_t fg = (currentTab == i) ? TFT_WHITE : TFT_DARKGREY;
    tft.fillRect(i * w, 208, w - 1, 32, bg);
    tft.setTextColor(fg, bg);
    tft.drawCentreString(tabs[i], i * w + (w / 2), 216, 2);
  }
}

void renderDashboard(bool fullRedraw)
{
  if (fullRedraw)
  {
    tft.fillRect(0, 22, 320, 186, COLOR_DARK_BG);

    tft.fillRoundRect(6, 26, 150, 86, 6, COLOR_CARD_BG);
    tft.drawRoundRect(6, 26, 150, 86, 6, 0x1A2F);
    tft.setTextColor(COLOR_SKY, COLOR_CARD_BG);
    tft.drawString(ultraPrecisionMode ? "VBUS (20B / 4D)" : "VBUS (STD / 3D)", 14, 32, 2);

    tft.fillRoundRect(164, 26, 150, 86, 6, COLOR_CARD_BG);
    tft.drawRoundRect(164, 26, 150, 86, 6, 0x1A85);
    tft.setTextColor(COLOR_GREEN, COLOR_CARD_BG);
    tft.drawString(ultraPrecisionMode ? "PRAD (20B / 4D)" : "PRAD (STD / 3D)", 172, 32, 2);

    tft.fillRoundRect(6, 116, 150, 86, 6, COLOR_CARD_BG);
    tft.drawRoundRect(6, 116, 150, 86, 6, 0x42E2);
    tft.setTextColor(COLOR_AMBER, COLOR_CARD_BG);
    tft.drawString("MOC CZYNNA", 14, 122, 2);

    tft.fillRoundRect(164, 116, 150, 86, 6, COLOR_CARD_BG);
    tft.drawRoundRect(164, 116, 150, 86, 6, 0x41E9);
    tft.setTextColor(COLOR_PURPLE, COLOR_CARD_BG);
    tft.drawString("REZYSTANCJA / RES", 172, 122, 2);
  }

  tft.setTextColor(COLOR_SKY, COLOR_CARD_BG);
  char vStr[16];
  if (ultraPrecisionMode)
  {
    sprintf(vStr, "%7.4f V", vBus); // 4 miejsca po przecinku: 0.0000 V
  }
  else
  {
    sprintf(vStr, "%6.3f V ", vBus); // 3 miejsca po przecinku: 0.000 V
  }
  tft.drawString(vStr, 12, 54, 4);
  tft.drawString(vStr, 13, 54, 4); // Bold

  tft.setTextColor(COLOR_GREEN, COLOR_CARD_BG);
  char iStr[16];
  if (ultraPrecisionMode)
  {
    sprintf(iStr, "%+7.4f A", currentA); // 4 miejsca po przecinku: +0.0000 A
  }
  else
  {
    sprintf(iStr, "%+6.3f A ", currentA); // 3 miejsca po przecinku: +0.000 A
  }
  tft.drawString(iStr, 170, 54, 4);
  tft.drawString(iStr, 171, 54, 4); // Bold

  tft.setTextColor(COLOR_AMBER, COLOR_CARD_BG);
  char pStr[16];
  if (ultraPrecisionMode)
  {
    sprintf(pStr, "%6.3f W ", powerW);
  }
  else
  {
    sprintf(pStr, "%5.2f W  ", powerW);
  }
  tft.drawString(pStr, 12, 144, 4);
  tft.drawString(pStr, 13, 144, 4); // Bold

  int barW = constrain((int)((powerW / 60.0) * 130), 2, 134);
  tft.fillRect(14, 178, barW, 4, COLOR_AMBER);
  tft.fillRect(14 + barW, 178, 134 - barW, 4, 0x10A2);

  tft.setTextColor(TFT_WHITE, COLOR_CARD_BG);
  if (abs(currentA) > 0.001)
  {
    char rStr[16];
    sprintf(rStr, "%6.2f R ", resistanceOhm);
    tft.drawString(rStr, 170, 144, 4);
    tft.drawString(rStr, 171, 144, 4); // Bold
  }
  else
  {
    tft.drawString("---.-- R ", 170, 144, 4);
    tft.drawString("---.-- R ", 171, 144, 4); // Bold
  }

  tft.setTextColor(TFT_DARKGREY, COLOR_CARD_BG);
  char shStr[20];
  sprintf(shStr, "Shunt:%5.1fuV ", vShunt_uV);
  tft.drawString(shStr, 14, 90, 1);

  char maStr[20];
  if (ultraPrecisionMode)
  {
    sprintf(maStr, "%6.2f mA ", currentA * 1000.0);
  }
  else
  {
    sprintf(maStr, "%6.1f mA  ", currentA * 1000.0);
  }
  tft.drawString(maStr, 172, 90, 1);

  tft.setTextColor(COLOR_AMBER, COLOR_CARD_BG);
  char tempSub[20];
  sprintf(tempSub, "Temp: %5.2f C", tempC);
  tft.drawString(tempSub, 172, 178, 1);
}

void renderChart(bool fullRedraw)
{
  if (fullRedraw)
  {
    tft.fillRect(0, 22, 320, 186, COLOR_DARK_BG);
    tft.setTextColor(TFT_WHITE, COLOR_DARK_BG);
    tft.drawString("PRZEBIEG PRADU I(t) [0..5A]", 10, 28, 2);
  }

  int gx = 20, gy = 50, gw = 280, gh = 140;
  tft.fillRect(gx + 1, gy + 1, gw - 2, gh - 2, 0x0821);
  tft.drawRect(gx, gy, gw, gh, 0x4208);

  float maxI = 5.0f;
  for (int i = 0; i < GRAPH_POINTS - 1; i++)
  {
    int idx1 = (graphIndex + i) % GRAPH_POINTS;
    int idx2 = (graphIndex + i + 1) % GRAPH_POINTS;
    int x1 = gx + (i * gw) / (GRAPH_POINTS - 1);
    int x2 = gx + ((i + 1) * gw) / (GRAPH_POINTS - 1);
    int y1 = gy + gh - (int)((currentHistory[idx1] / maxI) * gh);
    int y2 = gy + gh - (int)((currentHistory[idx2] / maxI) * gh);
    y1 = constrain(y1, gy, gy + gh);
    y2 = constrain(y2, gy, gy + gh);
    tft.drawLine(x1, y1, x2, y2, COLOR_GREEN);
  }
}

void renderBattery(bool fullRedraw)
{
  if (fullRedraw)
  {
    tft.fillRect(0, 22, 320, 186, COLOR_DARK_BG);
    tft.setTextColor(COLOR_SKY, COLOR_DARK_BG);
    tft.drawString("LICZNIK ENERGII I BATERII", 10, 28, 2);

    tft.setTextColor(TFT_WHITE, COLOR_DARK_BG);
    tft.drawString("Pojemnosc:", 20, 60, 2);
    tft.drawString("Energia:", 20, 100, 2);

    tft.fillRoundRect(80, 155, 160, 36, 6, 0x9800);
    tft.setTextColor(TFT_WHITE, 0x9800);
    tft.drawCentreString("ZERUJ LICZNIK", 160, 165, 2);
  }

  tft.setTextColor(COLOR_GREEN, COLOR_DARK_BG);
  char mahStr[20];
  sprintf(mahStr, "%8.2f mAh ", total_mAh);
  tft.drawString(mahStr, 120, 56, 4);

  tft.setTextColor(COLOR_AMBER, COLOR_DARK_BG);
  char whStr[20];
  sprintf(whStr, "%8.3f Wh  ", total_Wh);
  tft.drawString(whStr, 120, 96, 4);
}

void renderPrecision(bool fullRedraw)
{
  if (fullRedraw)
  {
    tft.fillRect(0, 22, 320, 186, COLOR_DARK_BG);
    tft.setTextColor(COLOR_SKY, COLOR_DARK_BG);
    tft.drawString("STATUS SIECI I KALIBRACJA", 10, 28, 2);
    tft.setTextColor(TFT_WHITE, COLOR_DARK_BG);
    tft.drawString("Spadek V_SHUNT:", 15, 54, 2);
    tft.drawString("Strona WWW (IP):", 15, 76, 2);
    tft.drawString("Siec Wi-Fi:", 15, 98, 2);

    tft.fillRoundRect(15, 126, 290, 32, 6, 0x7800);
    tft.setTextColor(TFT_WHITE, 0x7800);
    tft.drawCentreString("KALIBRUJ DOTYK PONOWNIE", 160, 134, 2);

    tft.fillRoundRect(15, 164, 290, 32, 6, ultraPrecisionMode ? 0x037F : 0x52AA);
    tft.setTextColor(TFT_WHITE, ultraPrecisionMode ? 0x037F : 0x52AA);
    if (ultraPrecisionMode)
    {
      tft.drawCentreString("TRYB: ULTRA 20-BIT (4D / 0.0000V)", 160, 172, 2);
    }
    else
    {
      tft.drawCentreString("TRYB: STANDARD 16-BIT (3D / 0.000V)", 160, 172, 2);
    }
  }

  tft.setTextColor(COLOR_GREEN, COLOR_DARK_BG);
  char shStr[20];
  sprintf(shStr, "%8.2f uV  ", vShunt_uV);
  tft.drawString(shStr, 155, 54, 2);

  tft.setTextColor(COLOR_SKY, COLOR_DARK_BG);
  String url = "http://" + activeIpAddress;
  tft.drawString(url, 155, 76, 2);

  tft.setTextColor(TFT_YELLOW, COLOR_DARK_BG);
  String wifiStatusText = isApMode ? "Tryb AP (192.168.4.1)" : (activeSsid + " [Flash]");
  tft.drawString(wifiStatusText, 155, 98, 2);
}

// =====================================================================
// 👆 11. MATEMATYCZNE MAPOWANIE DOTYKU Z KALIBRACJI
// =====================================================================
void checkTouch()
{
  if (!touch.touched())
    return;

  TS_Point p = touch.getPoint();
  if (p.z < 250)
    return;

  int screenX = map(p.x, cal_p1_x, cal_p2_x, 30, 290);
  int screenY = map(p.y, cal_p1_y, cal_p2_y, 30, 210);

  screenX = constrain(screenX, 0, 319);
  screenY = constrain(screenY, 0, 239);

  Serial.print("[DOTYK] Raw X=");
  Serial.print(p.x);
  Serial.print(" Y=");
  Serial.print(p.y);
  Serial.print(" -> Ekran X=");
  Serial.print(screenX);
  Serial.print(" Y=");
  Serial.println(screenY);
  beep(2800, 15);

  // 0. Dotknięcie górnego paska (Header Y < 24) przełącza tryb Ciemny / Jasny (i zapisuje we Flash)
  if (screenY < 24)
  {
    displayInverted = !displayInverted;
    tft.invertDisplay(displayInverted);
    prefs.begin("touch_cal", false);
    prefs.putBool("disp_inv", displayInverted);
    prefs.end();
    Serial.print("[EKRAN] Przelaczono inwersje: ");
    Serial.println(displayInverted ? "CIEMNY TRYB (True)" : "JASNY TRYB (False)");
    beep(2200, 40);
    delay(250);
    return;
  }

  // 1. Dolne przyciski zakladek
  if (screenY >= 200)
  {
    int clickedTab = screenX / 80;
    if (clickedTab >= 0 && clickedTab < 4)
    {
      currentTab = (ScreenTab)clickedTab;
      Serial.print("Wybrano zakladke: ");
      Serial.println(clickedTab);
    }
    delay(160);
    return;
  }

  // 2. Przycisk zerowania licznika
  if (currentTab == TAB_BATTERY && screenY >= 140 && screenY <= 200 && screenX >= 70 && screenX <= 250)
  {
    total_mAh = 0;
    total_Wh = 0;
    sessionStartTime = millis();
    beep(1500, 120);
    delay(200);
    return;
  }

  // 3. Opcje w zakladce Precyzja
  if (currentTab == TAB_PRECISION)
  {
    if (screenY >= 125 && screenY <= 165 && screenX >= 15 && screenX <= 305)
    {
      beep(2000, 80);
      runTouchCalibration();
      tft.fillScreen(COLOR_DARK_BG);
      drawHeader();
      renderPrecision(true);
      drawBottomTabs();
      return;
    }

    if (screenY >= 165 && screenY <= 200 && screenX >= 15 && screenX <= 305)
    {
      ultraPrecisionMode = !ultraPrecisionMode;
      if (ultraPrecisionMode)
      {
        ina.setPrecisionMode(INA228_AVG_256, INA228_TIME_2074_US);
      }
      else
      {
        ina.setPrecisionMode(INA228_AVG_16, INA228_TIME_540_US);
      }
      beep(2200, 80);
      renderPrecision(true);
      delay(200);
    }
  }
}

// =====================================================================
// 🚀 12. SETUP & LOOP
// =====================================================================
void setup()
{
  Serial.begin(115200);
  Serial.println();
  Serial.println("========================================");
  Serial.println("  Start CYD INA228 Power Lab");
  Serial.println("========================================");

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_GRN_PIN, OUTPUT);
  pinMode(LED_BLU_PIN, OUTPUT);
  setRgbLed(false, false, true);

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(true); // <--- KLUCZOWA ZMIANA: TRUE daje gleboka czern na ekranach CYD!
  tft.fillScreen(COLOR_DARK_BG);

  touchSpi.begin(XPT_CLK_PIN, XPT_MISO_PIN, XPT_MOSI_PIN, XPT_CS_PIN);
  touch.begin(touchSpi);
  touch.setRotation(1);

  loadTouchCalibration();

  tft.setTextColor(COLOR_SKY, COLOR_DARK_BG);
  tft.drawCentreString("CYD INA228 POWER LAB", 160, 40, 4);

  tft.fillRoundRect(20, 95, 280, 50, 8, 0x1A2F);
  tft.drawRoundRect(20, 95, 280, 50, 8, COLOR_SKY);
  tft.setTextColor(TFT_WHITE, 0x1A2F);
  tft.drawCentreString("Dotknij ekran teraz,", 160, 103, 2);
  tft.setTextColor(TFT_YELLOW, 0x1A2F);
  tft.drawCentreString("aby SKALIBROWAC DOTYK (3s)", 160, 122, 2);

  unsigned long startWait = millis();
  bool requestCalibration = false;
  while (millis() - startWait < 3000)
  {
    int barW = (millis() - startWait) * 280 / 3000;
    tft.fillRect(20, 160, barW, 4, COLOR_GREEN);
    tft.fillRect(20 + barW, 160, 280 - barW, 4, 0x1927);

    if (touch.touched())
    {
      beep(2500, 50);
      requestCalibration = true;
      break;
    }
    delay(50);
  }

  if (requestCalibration || !hasCalibration)
  {
    runTouchCalibration();
  }

  pinMode(I2C_SDA_PIN, INPUT_PULLUP);
  pinMode(I2C_SCL_PIN, INPUT_PULLUP);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000);

  if (checkIna228Present())
  {
    sensorConnected = ina.begin(&Wire, SHUNT_RESISTOR, MAX_CURRENT, false);
    if (sensorConnected)
    {
      ina.setShuntTempCo(20);
      tempC = ina.getDieTemperature();
      Serial.println("[INA228] Czujnik wykryty i poprawnie skonfigurowany.");
    }
  }
  else
  {
    sensorConnected = false;
    Serial.println("[INA228] OSTRZEZENIE: Brak czujnika INA228 przy starcie!");
  }

  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);
  if (SD.begin(SD_CS_PIN))
  {
    File f = SD.open("/data.csv", FILE_APPEND);
    if (f)
    {
      f.println("Timestamp_ms,VBUS_V,CURRENT_A,POWER_W,TEMP_C");
      f.close();
      isLoggingSD = true;
      Serial.println("[SD] Karta zainicjalizowana pomyslnie.");
    }
  }
  else
  {
    Serial.println("[SD] Karta nie jest wlozona (datalogger wylaczony).");
  }

  // Wczytanie konfiguracji Wi-Fi z pamieci Flash
  prefs.begin("cyd_wifi", false);
  hasSavedWifi = prefs.getBool("saved", false);
  if (hasSavedWifi)
  {
    activeSsid = prefs.getString("ssid", "");
    activePass = prefs.getString("pass", "");
    Serial.print("[Wi-Fi Flash] Wczytano zapisana siec we Flash: ");
    Serial.println(activeSsid);
  }
  else
  {
    activeSsid = String(WIFI_SSID_DEFAULT);
    activePass = String(WIFI_PASSWORD_DEFAULT);
    Serial.println("[Wi-Fi Flash] Brak zapisanej sieci we Flash. Uzyto domyslnej.");
  }
  prefs.end();

  tft.fillScreen(COLOR_DARK_BG);
  tft.setTextColor(COLOR_SKY, COLOR_DARK_BG);
  tft.drawCentreString("Skanowanie sieci Wi-Fi...", 160, 90, 4);

  WiFi.persistent(false);
  WiFi.disconnect(true);
  delay(150);

  Serial.println();
  Serial.println("[Wi-Fi] Skanowanie sieci 2.4 GHz...");
  int scanCount = WiFi.scanNetworks();
  Serial.print("[Wi-Fi] Znaleziono sieci: ");
  Serial.println(scanCount);
  for (int i = 0; i < scanCount; i++)
  {
    Serial.print("  - ");
    Serial.print(WiFi.SSID(i));
    Serial.print(" (");
    Serial.print(WiFi.RSSI(i));
    Serial.println(" dBm)");
  }

  bool connectedSta = false;

  if (activeSsid.length() > 0 && activeSsid != "TwojaNazwaSieci")
  {
    tft.fillScreen(COLOR_DARK_BG);
    tft.setTextColor(COLOR_SKY, COLOR_DARK_BG);
    tft.drawCentreString(hasSavedWifi ? "Laczenie z zapisana siecia..." : "Laczenie z domowym Wi-Fi...", 160, 60, 2);
    tft.setTextColor(TFT_WHITE, COLOR_DARK_BG);
    tft.drawCentreString(activeSsid, 160, 90, 4);

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(activeSsid.c_str(), activePass.c_str());
    Serial.print("[Wi-Fi] Laczenie z: ");
    Serial.println(activeSsid);

    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 30)
    {
      delay(350);
      Serial.print(".");
      int pWidth = (tries * 260) / 30;
      tft.fillRect(30, 140, pWidth, 6, COLOR_SKY);
      tries++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
      connectedSta = true;
      activeIpAddress = WiFi.localIP().toString();
      isApMode = false;
      Serial.print("[Wi-Fi] SUKCES! IP: ");
      Serial.println(activeIpAddress);
    }
    else
    {
      Serial.print("[Wi-Fi] Blad polaczenia z siecia: ");
      Serial.println(activeSsid);
    }
  }

  if (!connectedSta)
  {
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD, 1, 0, 4);
    delay(100);
    activeIpAddress = WiFi.softAPIP().toString();
    isApMode = true;
    Serial.print("[Wi-Fi] Uruchomiono Access Point: ");
    Serial.print(AP_SSID);
    Serial.print(" IP: ");
    Serial.println(activeIpAddress);
  }

  tft.fillScreen(COLOR_DARK_BG);
  tft.fillRoundRect(10, 15, 300, 210, 8, COLOR_CARD_BG);
  tft.drawRoundRect(10, 15, 300, 210, 8, COLOR_SKY);

  tft.setTextColor(COLOR_GREEN, COLOR_CARD_BG);
  tft.drawCentreString(connectedSta ? "Polaczono z Routerem!" : "Tryb Wyboru Wi-Fi (AP)", 160, 34, 2);

  tft.setTextColor(TFT_WHITE, COLOR_CARD_BG);
  if (isApMode)
  {
    tft.drawString("1. Polacz z Wi-Fi: " + String(AP_SSID), 20, 68, 2);
    tft.drawString("2. Haslo do AP:    " + String(AP_PASSWORD), 20, 88, 2);
    tft.setTextColor(COLOR_AMBER, COLOR_CARD_BG);
    tft.drawString("3. Wejdz i wybierz swoja siec:", 20, 114, 2);
  }
  else
  {
    tft.drawString("Siec Wi-Fi: " + activeSsid, 25, 75, 2);
    tft.setTextColor(COLOR_AMBER, COLOR_CARD_BG);
    tft.drawString("Adres IP do przegladarki:", 25, 110, 2);
  }

  tft.setTextColor(COLOR_SKY, COLOR_CARD_BG);
  tft.drawCentreString("http://" + activeIpAddress, 160, 145, 4);

  tft.setTextColor(TFT_DARKGREY, COLOR_CARD_BG);
  tft.drawCentreString("(Dotknij ekran lub odczekaj 3s)", 160, 195, 2);

  unsigned long splashWait = millis();
  while (millis() - splashWait < 3000)
  {
    if (touch.touched())
      break;
    delay(50);
  }

  server.on("/", handleRoot);
  server.on("/api/data", handleApiData);
  server.on("/api/reset", HTTP_POST, handleApiReset);
  server.on("/api/wifi/scan", handleWifiScan);
  server.on("/api/wifi/save", handleWifiSave);
  server.on("/api/wifi/reset", handleWifiReset);
  server.on("/data.csv", handleDownloadCsv);
  server.begin();

  sessionStartTime = millis();
  setRgbLed(false, true, false);
  beep(2500, 60);

  tft.fillScreen(COLOR_DARK_BG);
  drawHeader();
  renderDashboard(true);
  drawBottomTabs();
}

void loop()
{
  server.handleClient();
  unsigned long now = millis();

  // 1. Sprawdzanie obecności czujnika INA228 co 500 ms
  if (now - lastSensorCheck >= 500)
  {
    lastSensorCheck = now;
    bool isOk = checkIna228Present();
    if (isOk && !sensorConnected)
    {
      // Czujnik został ponownie wpięty! Re-inicjalizacja
      sensorConnected = ina.begin(&Wire, SHUNT_RESISTOR, MAX_CURRENT, false);
      if (sensorConnected)
      {
        ina.setShuntTempCo(20);
        beep(2500, 80);
        wasSensorErrorDrawn = false;
        tft.fillScreen(COLOR_DARK_BG);
        drawHeader();
        renderDashboard(true);
        drawBottomTabs();
      }
    }
    else
    {
      sensorConnected = isOk;
    }
  }

  // 2. Jeśli czujnik jest WYPIĘTY / NIEODPOWIADA - wyświetl ciemnoczerwony ekran z czarnym migającym co 1s napisem i migającą na czerwono diodą LED
  if (!sensorConnected)
  {
    if (!wasSensorErrorDrawn)
    {
      tft.fillScreen(COLOR_DARK_RED);
      wasSensorErrorDrawn = true;
      errorTextVisible = true;
      setRgbLed(true, false, false); // Włącz czerwoną diodę LED
      tft.setTextColor(TFT_BLACK, COLOR_DARK_RED);
      // Pogrubiony napis (efekt bold: podwójny render z przesunięciem o 1px)
      tft.drawCentreString("Brak czujnika INA228  !!!", 160, 105, 4);
      tft.drawCentreString("Brak czujnika INA228  !!!", 161, 105, 4);
      lastBlinkToggle = now;
      beep(1200, 40);
    }

    if (now - lastBlinkToggle >= 1000)
    {
      lastBlinkToggle = now;
      errorTextVisible = !errorTextVisible;

      if (errorTextVisible)
      {
        setRgbLed(true, false, false); // Dioda miga: WŁĄCZ CZERWONĄ
        tft.setTextColor(TFT_BLACK, COLOR_DARK_RED);
        // Pogrubiony napis bold (przesunięcie w poziomie o 1 px)
        tft.drawCentreString("Brak czujnika INA228  !!!", 160, 105, 4);
        tft.drawCentreString("Brak czujnika INA228  !!!", 161, 105, 4);
        beep(1200, 40); // Krótki dźwięk ostrzegawczy co cykl
      }
      else
      {
        setRgbLed(false, false, false); // Dioda miga: WYŁĄCZ DIODĘ (OFF)
        // Gdy tekst znika w cyklu migania - zamalowujemy go kolorem ciemnoczerwonego tła
        tft.fillRect(10, 95, 300, 50, COLOR_DARK_RED);
      }
    }
    delay(20);
    return; // Nie wykonujemy dalszych pomiarów dopóki czujnik nie zostanie wpięty
  }

  // 3. Normalna praca z czujnikiem
  if (wasSensorErrorDrawn)
  {
    wasSensorErrorDrawn = false;
    tft.fillScreen(COLOR_DARK_BG);
    drawHeader();
    renderDashboard(true);
    drawBottomTabs();
  }

  checkTouch();

  if (now - lastSampleTime >= (ultraPrecisionMode ? 80 : 40))
  {
    double dt_hours = (double)(now - lastSampleTime) / 3600000.0;
    lastSampleTime = now;

    vBus = ina.getBusVoltage();
    currentA = ina.getCurrent();
    powerW = ina.getPower();
    vShunt_uV = ina.getShuntVoltage_uV();
    tempC = ina.getDieTemperature();

    if (abs(currentA) > 0.0001)
    {
      resistanceOhm = vBus / currentA;
    }

    total_mAh += abs(currentA) * 1000.0 * dt_hours;
    total_Wh += powerW * dt_hours;

    if (vBus > 30.0 || abs(currentA) > 5.0)
    {
      setRgbLed(true, false, false);
      beep(3500, 20);
    }
    else if (abs(currentA) < 0.05)
    {
      setRgbLed(false, false, true);
    }
    else if (powerW > 40.0)
    {
      setRgbLed(true, true, false);
    }
    else
    {
      setRgbLed(false, true, false);
    }

    currentHistory[graphIndex] = abs(currentA);
    graphIndex = (graphIndex + 1) % GRAPH_POINTS;
  }

  if (now - lastUiRefresh >= 200)
  {
    lastUiRefresh = now;
    bool tabChanged = (currentTab != lastDrawnTab);
    if (tabChanged)
    {
      lastDrawnTab = currentTab;
      drawHeader();
      drawBottomTabs();
    }
    else
    {
      updateHeaderLive();
    }

    if (currentTab == TAB_DASHBOARD)
      renderDashboard(tabChanged);
    else if (currentTab == TAB_CHART)
      renderChart(tabChanged);
    else if (currentTab == TAB_BATTERY)
      renderBattery(tabChanged);
    else if (currentTab == TAB_PRECISION)
      renderPrecision(tabChanged);
  }

  if (isLoggingSD && (now - lastSdLogTime >= 1000))
  {
    lastSdLogTime = now;
    File f = SD.open("/data.csv", FILE_APPEND);
    if (f)
    {
      f.print(now);
      f.print(",");
      f.print(vBus, 4);
      f.print(",");
      f.print(currentA, 4);
      f.print(",");
      f.print(powerW, 3);
      f.print(",");
      f.println(tempC, 2);
      f.close();
    }
  }
}
