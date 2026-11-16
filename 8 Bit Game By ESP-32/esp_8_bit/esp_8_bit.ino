/* Copyright (c) 2020, Peter Barrett
** ESP_8_BIT NES emulator with resistor-ladder controllers (2 players)
*/

#include "esp_system.h"
#include "esp_int_wdt.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include <SPI.h>
#include <SD.h>

#define PERF
#include "src/emu.h"
#include "src/video_out.h"

#define VIDEO_STANDARD NTSC //NTSC For Morden TV , PAL For Old CRT TV
#define EMULATOR EMU_NES

// ---------------- Player 1 pins (ADC) ----------------
const int P1_LR   = 34;   // left / right
const int P1_UD   = 14;   // up / down
const int P1_SEL  = 15;   // left select / fire(left)
const int P1_FIRE = 32;   // right select / jump(right)

// ---------------- Player 2 pins (ADC) ----------------
const int P2_UD = 35;
const int P2_LR = 33;
const int P2_FJ = 4;

// SD card (CS pin; SCK 18, MISO 19, MOSI 23 are the default SPI pins)
const int SD_CS = 5;
static bool _sd_ok = false;
static const char* _folder = "/nofrendo";   // internal fallback games, only used if the SD card has no games

// number of .nes files in /NES on the SD card
extern int g_nes_skip;     // from src/emu_nofrendo.cpp
extern int g_nes_frames;
extern int g_menu_visible;   // from src/gui.cpp

static int count_nes() {
  int n = 0;
  File d = SD.open("/NES");
  if (!d) return 0;
  if (d.isDirectory()) {
    File f = d.openNextFile();
    while (f) {
      String nm = f.name();
      nm.toLowerCase();
      if (!f.isDirectory() && nm.endsWith(".nes") && !nm.startsWith(".") && nm.indexOf("/.") < 0) n++;
      f.close();
      f = d.openNextFile();
    }
  }
  d.close();
  return n;
}

// Basic joystick bits (same values the emulator uses)
#define G_START  0x0080
#define G_SELECT 0x0040
#define G_RIGHT  0x0010
#define G_LEFT   0x0008
#define G_DOWN   0x0004
#define G_UP     0x0002
#define G_A      0x0800   // NES A
#define G_B      0x0400   // NES B

// Generic joystick bits understood by the emulator / GUI
#define G_MENU   0x0100   // open / close the game menu
#define G_SAVE   0x4000   // save the game (both select buttons)
#define G_SAVES  0x2000   // open the saved games page (left select + left)

// ---- button decoding ------------------------------------------------------------------
// Each ladder pin has 1 to 3 "windows" of ADC values, one per button. A button that is already
// pressed stays pressed while the reading drifts a little outside its window (margin), so a
// noisy reading no longer cuts a jump short or drops one of two buttons pressed together.
static volatile int _odd_pin = 0, _odd_val = 0;   // last reading that matched no button (shown in the speed report)

// w holds n windows as pairs: low value, high value, low value, high value, ...
static int classify(int pin, int v, int prev, const int* w, int n, int margin) {
  for (int i = 0; i < n; i++)
    if (v >= w[2 * i] && v <= w[2 * i + 1]) return i + 1;
  if (prev > 0) {
    int lo = w[2 * (prev - 1)], hi = w[2 * (prev - 1) + 1];
    if (v >= lo - margin && v <= hi + margin) return prev;
  }
  if (v > 140) { _odd_pin = pin; _odd_val = v; }
  return 0;
}

static const int W_P1[]  = { 150, 270,  700, 1100 };          // player 1: low button / high button
static const int W_UD2[] = { 700, 1100, 150, 270 };           // player 2 up-down:  down / up
static const int W_LR2[] = { 700, 1100, 150, 270  };           // player 2 left-right: right / left
static const int W_FJ2[] = { 150, 270,  750, 850,  950, 1100 };   // player 2: jump / fire / both

static int st1[4], st2[3];

static uint16_t read_p1() {
  uint16_t j = 0;
  st1[0] = classify(P1_LR,   analogRead(P1_LR),   st1[0], W_P1, 2, 80);
  st1[1] = classify(P1_UD,   analogRead(P1_UD),   st1[1], W_P1, 2, 80);
  st1[2] = classify(P1_SEL,  analogRead(P1_SEL),  st1[2], W_P1, 2, 80);
  st1[3] = classify(P1_FIRE, analogRead(P1_FIRE), st1[3], W_P1, 2, 80);

  if (st1[0] == 1) j |= G_RIGHT;   else if (st1[0] == 2) j |= G_LEFT;
  if (st1[1] == 1) j |= G_UP;      else if (st1[1] == 2) j |= G_DOWN;
  if (st1[2] == 1) j |= G_SELECT;  else if (st1[2] == 2) j |= G_B;   // left select / fire (left)
  if (st1[3] == 1) j |= G_START;   else if (st1[3] == 2) j |= G_A;   // right select / jump (right)

  if (g_menu_visible) {
    // inside the menu: right select works like A (choose); left select goes to the menu itself
    if (j & G_START) j = (j & ~G_START) | G_A;
    return j;
  }
  // in a game:
  static bool save_latch = false;     // after a save, ignore both select buttons until both are released
  static uint8_t sel_age = 0, start_age = 0;
  bool sel_down = j & G_SELECT, start_down = j & G_START;
  if (sel_down && start_down) { save_latch = true; return G_SAVE; }   // both select buttons = save the game
  if (save_latch) {
    if (sel_down || start_down) return j & ~(G_SELECT | G_START);     // still releasing: keep them away from the game
    save_latch = false;
  }
  if (sel_down && (j & G_LEFT))  return G_SAVES;                      // left select + left = saved games page
  if (sel_down && (j & G_A) && (j & G_B)) return G_MENU;              // left select + A + B = home menu
  // a lone select/start press reaches the game one update late, so a two-button press
  // is never seen by the game as a single press (no accidental pause)
  if (start_down) { if (start_age < 1) { start_age++; j &= ~G_START; } } else start_age = 0;
  if (sel_down)   { if (sel_age < 1)   { sel_age++;   j &= ~G_SELECT; } } else sel_age = 0;
  return j;
}

static uint16_t read_p2() {
  uint16_t j = 0;
  st2[0] = classify(P2_UD, analogRead(P2_UD), st2[0], W_UD2, 2, 25);
  st2[1] = classify(P2_LR, analogRead(P2_LR), st2[1], W_LR2, 2, 25);
  st2[2] = classify(P2_FJ, analogRead(P2_FJ), st2[2], W_FJ2, 3, 25);

  if (st2[0] == 1) j |= G_DOWN;    else if (st2[0] == 2) j |= G_UP;
  if (st2[1] == 1) j |= G_RIGHT;   else if (st2[1] == 2) j |= G_LEFT;
  if (st2[2] == 1) j |= G_A;                                          // jump
  else if (st2[2] == 2) j |= G_B;                                     // fire
  else if (st2[2] == 3) j |= (G_A | G_B);                             // fire + jump
  return j;
}

static volatile uint32_t _btn_us = 0;    // time spent reading buttons (for the speed report)

// Called once per frame from the emulator (through get_hid_ir in src/ir_input.h).
// Builds a fake "IR joystick" packet: A1 42 <p1 lo> <p1 hi> <p2 lo> <p2 hi>
int get_hid_buttons(uint8_t* dst) {
  uint32_t t0 = micros();
  uint16_t a = read_p1(), b = read_p2();
  _btn_us = micros() - t0;
  dst[0] = 0xA1;
  dst[1] = 0x42;
  dst[2] = a & 0xFF;
  dst[3] = a >> 8;
  dst[4] = b & 0xFF;
  dst[5] = b >> 8;
  return 6;
}

Emu* NewEmulator() {
  return NewNofrendo(VIDEO_STANDARD);
}

Emu* _emu = 0;
uint32_t _frame_time = 0;
uint32_t _drawn = 1;
bool _inited = false;

void emu_init() {
  gui_start(_emu, _folder);
  if (!_sd_ok) gui_msg("NO SD CARD");
  else if (strcmp(_folder, "/sd/NES") != 0) gui_msg("NO GAMES IN SD NES");
  _drawn = _frame_counter;
}

void emu_loop() {
  video_sync();
  int64_t t_start = esp_timer_get_time();
  uint32_t t = xthal_get_ccount();
  g_nes_frames = 0;
  gui_update();                       // also reads the buttons
  _frame_time = xthal_get_ccount() - t;
  _lines = _emu->video_buffer();
  _drawn++;

  // Keep the game at normal speed: if several game frames were emulated in this update,
  // wait until the same number of screen refreshes has passed.
  int target = g_nes_frames > 0 ? g_nes_frames : 1;
  int64_t due = t_start + (int64_t)target * (_emu->standard ? 16667 : 20000) - 4000;
  while (esp_timer_get_time() < due)
    vTaskDelay(1);
}

void emu_task(void* arg) {
  emu_init();
  for (;;) emu_loop();
}

esp_err_t mount_filesystem() {
  esp_vfs_spiffs_conf_t conf = {
    .base_path = "",
    .partition_label = NULL,
    .max_files = 5,
    .format_if_mount_failed = true
  };
  return esp_vfs_spiffs_register(&conf);
}

void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(240);
  mount_filesystem();
  _sd_ok = SD.begin(SD_CS);                  // mounts at /sd
  int games = _sd_ok ? count_nes() : 0;
  if (games > 0) _folder = "/sd/NES";        // otherwise stay on the built-in fallback so video still starts
  Serial.printf("SD %s, %d games in /NES, free heap %d\n", _sd_ok ? "OK" : "FAILED", games, ESP.getFreeHeap());
  _emu = NewEmulator();

#ifdef SINGLE_CORE
  emu_init();
  video_init(_emu->cc_width, _emu->flavor, _emu->composite_palette(), _emu->standard);
#else
  xTaskCreatePinnedToCore(emu_task, "emu_task", 24 * 1024, NULL, 0, NULL, 0);
#endif
}

// Speed report over Serial (115200) every 2 seconds, printed from core 1 so the game core is not slowed
static void speed_report() {
  static uint32_t t_last = 0, drawn_last = 0;
  uint32_t now = millis();
  if (now - t_last < 2000) return;
  uint32_t d = _drawn - drawn_last;
  if (t_last && d > 0)
    Serial.printf("screen FPS:%.1f  emu_frame:%u us  skip:%d  buttons:%u us\n",
                  d * 1000.0f / (now - t_last), (unsigned)(_frame_time / 240), g_nes_skip, (unsigned)_btn_us);
  if (_odd_pin) {   // a button reading outside every window: tell me these numbers if a button misbehaves
    Serial.printf("ADC odd reading: pin %d = %d\n", _odd_pin, _odd_val);
    _odd_pin = 0;
  }
  t_last = now;
  drawn_last = _drawn;
}

void loop() {
#ifdef SINGLE_CORE
  emu_loop();
#else
  if (!_inited) {
    if (_lines) {
      video_init(_emu->cc_width, _emu->flavor, _emu->composite_palette(), _emu->standard);
      _inited = true;
    } else {
      vTaskDelay(1);
    }
  }
  speed_report();
  vTaskDelay(pdMS_TO_TICKS(20));   // let core 1 rest, the video runs from an interrupt
#endif
}
