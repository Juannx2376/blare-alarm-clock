/*
  Blare - Despertador DIY  (XIAO ESP32-C3 + ST7789 170x320 + 4 botones + buzzer pasivo)
  Libs (Library Manager): Adafruit GFX Library, Adafruit ST7789 Library
  Placa: "XIAO_ESP32C3"  (core esp32 de Espressif 3.x)

  Botones (a GND, INPUT_PULLUP):
    SW1 = ARRIBA (+)   SW2 = ABAJO (-)   SW3 = OK   SW4 = ATRAS / detener alarma
  Sin RTC: la hora se pierde al desconectar la energia (hay que ponerla de nuevo).
  La alarma, el sonido y el estado ON/OFF si se guardan en memoria flash.
*/
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>

// ---------- Pines (GPIO del ESP32-C3 segun tu PCB) ----------
#define PIN_SW_UP    2   // D0
#define PIN_SW_DOWN  3   // D1
#define PIN_SW_OK    4   // D2
#define PIN_SW_BACK  5   // D3
#define PIN_DC       6   // D4
#define PIN_CS       7   // D5
#define PIN_BL      21   // D6
#define PIN_BUZZER  20   // D7
#define PIN_RST      8   // D8
#define PIN_SCK      9   // D9
#define PIN_MOSI    10   // D10

#define BL_ON  HIGH      // si la pantalla queda negra pero conectada, prueba LOW

Adafruit_ST7789 tft(&SPI, PIN_CS, PIN_DC, PIN_RST);
GFXcanvas16 canvas(320, 170);   // dibujamos fuera de pantalla -> sin parpadeo
Preferences prefs;

// ---------- Colores ----------
#define C_BG     0x0000
#define C_FG     0xFFFF
#define C_ACC    0x07FF
#define C_DIM    0x7BEF
#define C_ALARM  0xFD20
#define C_OK     0x07E0

// ---------- Melodias ----------
struct Note { uint16_t f; uint16_t d; };   // frecuencia Hz (0 = silencio), duracion ms
const Note MEL_BEEP[] = { {2400,150},{0,100},{2400,150},{0,100},{2400,150},{0,600} };
const Note MEL_ARP[]  = { {523,120},{659,120},{784,120},{1047,120},{784,120},{659,120},{523,120},{0,500} };
const Note MEL_ODE[]  = { {659,250},{659,250},{698,250},{784,250},{784,250},{698,250},{659,250},{587,250},
                          {523,250},{523,250},{587,250},{659,250},{659,375},{587,125},{587,500},{0,500} };
struct Melody { const char* name; const Note* n; uint8_t len; };
const Melody MELODIES[] = {
  {"Beep",      MEL_BEEP, sizeof(MEL_BEEP)/sizeof(Note)},
  {"Arpegio",   MEL_ARP,  sizeof(MEL_ARP)/sizeof(Note)},
  {"Oda alegria", MEL_ODE, sizeof(MEL_ODE)/sizeof(Note)},
};
const uint8_t N_MEL = 3;

bool     melPlaying = false;
uint8_t  melId = 0, melIdx = 0;
uint32_t melNext = 0;

void melodyStart(uint8_t id) { melId = id; melIdx = 0; melNext = 0; melPlaying = true; }
void melodyStop() { melPlaying = false; noTone(PIN_BUZZER); }
void melodyUpdate() {
  if (!melPlaying) return;
  uint32_t now = millis();
  if ((int32_t)(now - melNext) < 0) return;
  const Melody& m = MELODIES[melId];
  const Note& n = m.n[melIdx];
  if (n.f) tone(PIN_BUZZER, n.f); else noTone(PIN_BUZZER);
  melNext = now + n.d;
  melIdx = (melIdx + 1) % m.len;      // se repite hasta que lo detengas
}

// ---------- Estado ----------
enum State { S_CLOCK, S_MENU, S_SET_TIME, S_SET_ALARM, S_SOUND, S_RINGING };
State state = S_CLOCK;

uint8_t  alH = 7, alM = 0, alSound = 0;
bool     alOn = false;
uint8_t  menuSel = 0;
uint8_t  editField = 0;          // 0 = hora, 1 = minuto
int      editH = 0, editM = 0;
time_t   snoozeUntil = 0;
int      lastTrigMinute = -1;
uint32_t ringStart = 0;
bool     redraw = true;
const char* MENU_ITEMS[] = {"Ajustar hora", "Ajustar alarma", "Alarma ON/OFF", "Sonido"};
const uint8_t N_MENU = 4;

// ---------- Botones ----------
struct Btn { uint8_t pin; bool last; uint32_t t; uint32_t pressedAt; uint32_t rep; };
Btn btns[4] = {
  {PIN_SW_UP,   true, 0, 0, 0}, {PIN_SW_DOWN, true, 0, 0, 0},
  {PIN_SW_OK,   true, 0, 0, 0}, {PIN_SW_BACK, true, 0, 0, 0}
};
enum { B_UP, B_DOWN, B_OK, B_BACK };
bool ev[4];                       // evento "pulsado" (con repeticion en UP/DOWN)

void readButtons() {
  uint32_t now = millis();
  for (int i = 0; i < 4; i++) {
    ev[i] = false;
    bool lvl = digitalRead(btns[i].pin);          // LOW = pulsado
    if (lvl != btns[i].last && now - btns[i].t > 25) {
      btns[i].t = now; btns[i].last = lvl;
      if (!lvl) { ev[i] = true; btns[i].pressedAt = now; btns[i].rep = now + 450; }
    } else if (!lvl && (i == B_UP || i == B_DOWN) && now >= btns[i].rep &&
               now - btns[i].pressedAt > 450) {
      ev[i] = true; btns[i].rep = now + 90;       // mantener = repetir
    }
  }
}

// ---------- Hora ----------
void setClock(int h, int m) {
  time_t t = time(nullptr);
  struct tm tmv; localtime_r(&t, &tmv);
  if (tmv.tm_year < 100) { tmv.tm_year = 126; tmv.tm_mon = 0; tmv.tm_mday = 1; }  // 1-ene-2026 si no hay fecha
  tmv.tm_hour = h; tmv.tm_min = m; tmv.tm_sec = 0;
  struct timeval tv = { mktime(&tmv), 0 };
  settimeofday(&tv, nullptr);
  lastTrigMinute = -1;
}

void saveAlarm() {
  prefs.putUChar("h", alH); prefs.putUChar("m", alM);
  prefs.putUChar("snd", alSound); prefs.putBool("on", alOn);
}

// ---------- Dibujo ----------
void drawCenter(const char* s, int y, int size, uint16_t col) {
  int16_t x1, y1; uint16_t w, h;
  canvas.setTextSize(size);
  canvas.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  canvas.setCursor((320 - w) / 2, y);
  canvas.setTextColor(col);
  canvas.print(s);
}
void hint(const char* s) {
  canvas.setTextSize(1); canvas.setTextColor(C_DIM);
  canvas.setCursor(6, 158); canvas.print(s);
}

void drawClock() {
  time_t t = time(nullptr); struct tm tmv; localtime_r(&t, &tmv);
  char b[16];
  canvas.fillScreen(C_BG);
  snprintf(b, sizeof b, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
  drawCenter(b, 30, 7, C_FG);
  snprintf(b, sizeof b, ":%02d", tmv.tm_sec);
  drawCenter(b, 100, 3, C_DIM);
  canvas.setTextSize(2);
  if (alOn) { snprintf(b, sizeof b, "Alarma %02d:%02d", alH, alM); canvas.setTextColor(C_ALARM); }
  else      { snprintf(b, sizeof b, "Alarma OFF");               canvas.setTextColor(C_DIM); }
  canvas.setCursor(6, 6); canvas.print(b);
  hint("OK = menu");
}

void drawMenu() {
  canvas.fillScreen(C_BG);
  canvas.setTextSize(2); canvas.setTextColor(C_ACC); canvas.setCursor(6, 6); canvas.print("MENU");
  for (int i = 0; i < N_MENU; i++) {
    int y = 36 + i * 28;
    if (i == menuSel) { canvas.fillRect(0, y - 4, 320, 26, C_ACC); canvas.setTextColor(C_BG); }
    else canvas.setTextColor(C_FG);
    canvas.setTextSize(2); canvas.setCursor(12, y); canvas.print(MENU_ITEMS[i]);
    if (i == 2) { canvas.setCursor(230, y); canvas.print(alOn ? "ON" : "OFF"); }
  }
  hint("UP/DOWN mover  OK entrar  BACK salir");
}

void drawTimeEdit(const char* title, uint16_t col) {
  char b[8];
  canvas.fillScreen(C_BG);
  canvas.setTextSize(2); canvas.setTextColor(col); canvas.setCursor(6, 6); canvas.print(title);
  snprintf(b, sizeof b, "%02d", editH);
  canvas.setTextSize(7);
  canvas.setTextColor(editField == 0 ? C_ACC : C_FG); canvas.setCursor(50, 50); canvas.print(b);
  canvas.setTextColor(C_FG); canvas.setCursor(145, 50); canvas.print(":");
  snprintf(b, sizeof b, "%02d", editM);
  canvas.setTextColor(editField == 1 ? C_ACC : C_FG); canvas.setCursor(190, 50); canvas.print(b);
  hint("UP/DOWN cambiar  OK siguiente/guardar  BACK cancelar");
}

void drawSound() {
  canvas.fillScreen(C_BG);
  canvas.setTextSize(2); canvas.setTextColor(C_ACC); canvas.setCursor(6, 6); canvas.print("SONIDO");
  drawCenter(MELODIES[alSound].name, 70, 3, C_FG);
  hint("UP/DOWN elegir (suena)  OK guardar  BACK cancelar");
}

void drawRinging() {
  canvas.fillScreen(((millis() / 400) & 1) ? C_ALARM : C_BG);
  char b[8]; snprintf(b, sizeof b, "%02d:%02d", alH, alM);
  drawCenter("DESPIERTA!", 20, 4, C_FG);
  drawCenter(b, 75, 4, C_FG);
  canvas.setTextSize(2); canvas.setTextColor(C_FG);
  canvas.setCursor(6, 140); canvas.print("OK = +5 min   BACK = parar");
}

void render() {
  switch (state) {
    case S_CLOCK:     drawClock(); break;
    case S_MENU:      drawMenu(); break;
    case S_SET_TIME:  drawTimeEdit("AJUSTAR HORA", C_ACC); break;
    case S_SET_ALARM: drawTimeEdit("AJUSTAR ALARMA", C_ALARM); break;
    case S_SOUND:     drawSound(); break;
    case S_RINGING:   drawRinging(); break;
  }
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), 320, 170);
}

// ---------- Logica ----------
void startRinging() {
  state = S_RINGING; ringStart = millis(); melodyStart(alSound); redraw = true;
}

void checkAlarm() {
  if (state == S_RINGING) return;
  time_t t = time(nullptr);
  if (snoozeUntil && t >= snoozeUntil) { snoozeUntil = 0; startRinging(); return; }
  if (!alOn || t < 100000) return;                  // sin hora valida no suena
  struct tm tmv; localtime_r(&t, &tmv);
  int key = tmv.tm_hour * 60 + tmv.tm_min;
  if (tmv.tm_hour == alH && tmv.tm_min == alM && key != lastTrigMinute) {
    lastTrigMinute = key; startRinging();
  }
}

void handleInput() {
  switch (state) {
    case S_CLOCK:
      if (ev[B_OK]) { state = S_MENU; menuSel = 0; redraw = true; }
      break;
    case S_MENU:
      if (ev[B_UP])   { menuSel = (menuSel + N_MENU - 1) % N_MENU; redraw = true; }
      if (ev[B_DOWN]) { menuSel = (menuSel + 1) % N_MENU; redraw = true; }
      if (ev[B_BACK]) { state = S_CLOCK; redraw = true; }
      if (ev[B_OK]) {
        redraw = true;
        if (menuSel == 0) { time_t t = time(nullptr); struct tm x; localtime_r(&t, &x);
                            editH = x.tm_hour; editM = x.tm_min; editField = 0; state = S_SET_TIME; }
        else if (menuSel == 1) { editH = alH; editM = alM; editField = 0; state = S_SET_ALARM; }
        else if (menuSel == 2) { alOn = !alOn; saveAlarm(); }
        else if (menuSel == 3) { state = S_SOUND; melodyStart(alSound); }
      }
      break;
    case S_SET_TIME:
    case S_SET_ALARM:
      if (ev[B_UP] || ev[B_DOWN]) {
        int d = ev[B_UP] ? 1 : -1;
        if (editField == 0) editH = (editH + 24 + d) % 24; else editM = (editM + 60 + d) % 60;
        redraw = true;
      }
      if (ev[B_BACK]) { state = S_MENU; redraw = true; }
      if (ev[B_OK]) {
        if (editField == 0) { editField = 1; }
        else {
          if (state == S_SET_TIME) setClock(editH, editM);
          else { alH = editH; alM = editM; alOn = true; saveAlarm(); }
          state = S_CLOCK;
        }
        redraw = true;
      }
      break;
    case S_SOUND:
      if (ev[B_UP] || ev[B_DOWN]) {
        alSound = (alSound + N_MEL + (ev[B_UP] ? 1 : -1)) % N_MEL;
        melodyStart(alSound); redraw = true;
      }
      if (ev[B_OK])   { saveAlarm(); melodyStop(); state = S_MENU; redraw = true; }
      if (ev[B_BACK]) { alSound = prefs.getUChar("snd", 0); melodyStop(); state = S_MENU; redraw = true; }
      break;
    case S_RINGING:
      if (ev[B_OK])   { melodyStop(); snoozeUntil = time(nullptr) + 5 * 60; state = S_CLOCK; redraw = true; }
      if (ev[B_BACK]) { melodyStop(); snoozeUntil = 0; state = S_CLOCK; redraw = true; }
      if (millis() - ringStart > 120000UL) { melodyStop(); state = S_CLOCK; redraw = true; }
      break;
  }
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 4; i++) pinMode(btns[i].pin, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT); digitalWrite(PIN_BUZZER, LOW);
  pinMode(PIN_BL, OUTPUT);     digitalWrite(PIN_BL, BL_ON);

  SPI.begin(PIN_SCK, -1, PIN_MOSI, -1);      // SPI por hardware en pines elegidos
  tft.init(170, 320);                         // 170x320
  tft.setSPISpeed(40000000);
  tft.setRotation(1);                         // horizontal 320x170
  tft.fillScreen(C_BG);
  canvas.setTextWrap(false);

  prefs.begin("blare", false);
  alH = prefs.getUChar("h", 7); alM = prefs.getUChar("m", 0);
  alSound = prefs.getUChar("snd", 0) % N_MEL; alOn = prefs.getBool("on", false);

  setenv("TZ", "<-05>5", 1); tzset();         
  render();
}

void loop() {
  static uint32_t lastSec = 0;
  readButtons();
  handleInput();
  checkAlarm();
  melodyUpdate();

  uint32_t now = millis();
  bool tick = (state == S_CLOCK || state == S_RINGING) && now - lastSec >= (state == S_RINGING ? 200 : 1000);
  if (redraw || tick) {
    lastSec = now; redraw = false; render();
  }
}
