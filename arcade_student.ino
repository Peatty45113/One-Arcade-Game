// =====================================================================
//  One-Button Arcade: Runner และ Reef Swim  ·  ESP32 + จอ OLED 1.3" (SH1106)
//  ฉบับนักเรียน: บรรทัด // MISSION 1 ตั้งใจทำให้ผิดไว้ แก้เฉพาะเกมที่เลือก
// =====================================================================
// Board: ESP32 DevKit (30-pin). Arduino IDE board setting: "ESP32 Dev Module".
// Libraries (Library Manager): "Adafruit SH110X" and "Adafruit GFX Library".
// The 1.3" OLED uses the SH1106 chip. A 0.96" OLED (SSD1306 chip) needs the older SSD1306 version of this file.
// Wiring (the same as kit_test)
//   OLED VCC -> 3V3      OLED GND -> GND
//   OLED SCL -> D22      OLED SDA -> D21
//   Button: one leg -> D23, the diagonally opposite leg -> GND
//   No vibration motor is needed: a crash flashes the screen instead.
//
// Controls
//   Menu:       tap = next game, hold = play
//   Game over:  tap = play again, hold = back to the menu
//   No press for 60 s: the screen turns off to save power. Press once to wake it.
//
// แผนที่ไฟล์ (กด Ctrl + F แล้วพิมพ์คำในวงเล็บ เพื่อกระโดดไปส่วนนั้น)
//   1. SETTINGS   การตั้งค่าบอร์ดและจอ                   ไม่ต้องแก้
//   2. SPRITES    ภาพตัวละคร RUN_A RUN_B RUN_J FISH       ภารกิจทอง
//   3. TOOLS      ปุ่ม จอกะพริบ การหลับ ตัวช่วยวาด          ไม่ต้องแก้
//   4. RUNNER     TUNE RUNNER + runnerStart/Step/Draw     ภารกิจ 1 + ปรับเกม
//   5. REEF       TUNE REEF + reefStart/Step/Draw         ภารกิจ 1 + ปรับเกม
//   6. MAIN       เมนู จบเกม setup() และ loop()          ภารกิจ 0

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Preferences.h>
#include "esp_sleep.h"
#include "driver/gpio.h"

// =====================================================================
// 1. SETTINGS: การตั้งค่าบอร์ดและจอ (ไม่ต้องแก้)
// =====================================================================
const int BUTTON_PIN = 23;
const int VIB_PIN = 18;
const int SDA_PIN = 21;
const int SCL_PIN = 22;
const uint8_t OLED_ADDRESS = 0x3C;
const bool USE_MOTOR = false;        // true only if a vibration motor is wired to D18
const uint32_t CPU_MHZ = 80;         // 80 saves battery; 160 or 240 if a game ever feels slow
const unsigned long IDLE_MS = 60000; // sleep after 60 s without a press

const int SCREEN_W = 128;            // จอกว้าง 128 จุด
const int SCREEN_H = 64;             // จอสูง 64 จุด
const int FIELD = 16;                // แถว 0-15 = แถบคะแนน, แถว 16-63 = สนามเกม
const unsigned long TICK = 33;       // 1 รอบเกมทุก 33 มิลลิวินาที = ประมาณ 30 รอบต่อวินาที
const unsigned long HOLD_MS = 600;   // กดนานเท่านี้ = กดค้าง

Adafruit_SH1106G display(SCREEN_W, SCREEN_H, &Wire, -1);
Preferences prefs;                   // keeps the best scores after power-off

void gameOver(int score);

// =====================================================================
// 2. SPRITES: ภาพตัวละคร  '#' = จุดติด  '.' = จุดดับ  (ทุกแถวต้องยาวเท่ากัน)
// =====================================================================
const char* const RUN_A[8] = { "..###..", "..###..", "...#...", ".#####.", "...#...", "..#.#..", ".#...#.", ".#...#." };
const char* const RUN_B[8] = { "..###..", "..###..", "...#...", ".#####.", "...#...", "...#...", "..#.#..", "..#.#.." };
const char* const RUN_J[8] = { "..###..", "#.###.#", ".#.#.#.", "..###..", "...#...", "..#.#..", ".#...#.", "......." };
const char* const FISH[5]  = { "#...###..", "##.##.##.", "########.", "##.######", "#...###.." };

// ---------------- Menu and scores ----------------
const int GAME_COUNT = 2;
const char* const GAME_NAMES[GAME_COUNT] = { "RUNNER", "REEF SWIM" };
const char* const GAME_KEYS[GAME_COUNT]  = { "runner", "reef" };
int best[GAME_COUNT];

const int MODE_MENU = 0, MODE_PLAY = 1, MODE_OVER = 2;
int mode = MODE_MENU;
int game = 0;                        // 0 = Runner, 1 = Reef Swim
int lastScore = 0;
bool newBest = false;
unsigned long overAt = 0;

// =====================================================================
// 3. TOOLS: ปุ่ม จอกะพริบ การหลับ ตัวช่วยวาด (ไม่ต้องแก้)
// =====================================================================
bool btnDown = false;                // กำลังกดอยู่ตอนนี้
bool btnPressed = false;             // เพิ่งถูกกด (ตั้งแต่รอบที่แล้ว)
bool btnTapped = false;              // แตะสั้น ๆ แล้วปล่อย
bool holdUsed = false;               // this press already counted as a hold
unsigned long btnDownAt = 0;
bool rawLast = false;
unsigned long rawChangedAt = 0;
unsigned long lastActivity = 0;      // time of the last press, for the idle sleep
unsigned long nextStep = 0;          // time of the next game step

void readButton(unsigned long now) {
  bool raw = digitalRead(BUTTON_PIN) == LOW;                 // LOW = pressed (INPUT_PULLUP)
  if (raw == rawLast || now - rawChangedAt < 15) return;     // ignore 15 ms of contact bounce
  rawLast = raw;
  rawChangedAt = now;
  if (raw) {
    btnDown = true;
    btnPressed = true;
    btnDownAt = now;
    holdUsed = false;
    lastActivity = now;
  } else {
    btnDown = false;
    if (!holdUsed && now - btnDownAt < HOLD_MS) btnTapped = true;
  }
}

// True once, when the button has been held for HOLD_MS.
bool holdReached(unsigned long now) {
  if (btnDown && !holdUsed && now - btnDownAt >= HOLD_MS) {
    holdUsed = true;
    return true;
  }
  return false;
}

// A crash flashes the screen (or buzzes, if a motor is fitted).
unsigned long feedbackUntil = 0;

void feedback(unsigned long ms) {
  if (USE_MOTOR) {
    digitalWrite(VIB_PIN, HIGH);
    feedbackUntil = millis() + ms;
  } else if (ms >= 300) {            // only crashes flash; points don't
    display.invertDisplay(true);
    feedbackUntil = millis() + 150;
  }
}

void updateFeedback(unsigned long now) {
  if (feedbackUntil != 0 && now >= feedbackUntil) {
    if (USE_MOTOR) digitalWrite(VIB_PIN, LOW);
    else display.invertDisplay(false);
    feedbackUntil = 0;
  }
}

// Sleep when nobody is playing; the button wakes it.
void sleepUntilPressed() {
  display.oled_command(SH110X_DISPLAYOFF);                        // screen off
  gpio_wakeup_enable((gpio_num_t)BUTTON_PIN, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_light_sleep_start();                                      // the ESP32 pauses here until the button is pressed
  gpio_wakeup_disable((gpio_num_t)BUTTON_PIN);
  display.oled_command(SH110X_DISPLAYON);                         // screen back on, game where it was
  while (digitalRead(BUTTON_PIN) == LOW) delay(10);             // the wake-up press is not used in the game
  rawLast = false; btnDown = false; btnPressed = false; btnTapped = false; holdUsed = true;
  lastActivity = millis();
  nextStep = millis();
}

int textWidth(const char* s) {
  int n = strlen(s);
  return n > 0 ? n * 6 - 1 : 0;      // each letter is 5 pixels wide plus a 1-pixel gap
}

void text(int x, int y, const char* s) {
  display.setCursor(x, y);
  display.print(s);
}

void textCenter(int y, const char* s) { text((SCREEN_W - textWidth(s)) / 2, y, s); }
void textRight(int y, const char* s) { text(SCREEN_W - textWidth(s), y, s); }

void sprite(int x, int y, const char* const rows[], int rowCount) {
  for (int j = 0; j < rowCount; j++)
    for (int i = 0; rows[j][i] != '\0'; i++)
      if (rows[j][i] == '#') display.drawPixel(x + i, y + j, SH110X_WHITE);
}

int px(float v) { return (int)lroundf(v); }   // round a position to a whole pixel

// =====================================================================
// 4. RUNNER: วิ่งแล้วกระโดดข้ามกล่อง
// =====================================================================

// ---------- TUNE RUNNER: ปรับเกมตรงนี้ เปลี่ยนแค่ตัวเลขหลังเครื่องหมาย = ----------
const float RUN_START_SPEED  = 2.0;   // ความเร็วตอนเริ่ม
const float RUN_MAX_SPEED    = 4.5;   // เร็วสุดได้แค่ไหน
const float RUN_GRAVITY      = 0.38;  // แรงดึงลง ยิ่งมากยิ่งตกเร็ว
const float RUN_GRAVITY_HOLD = 0.2;   // แรงดึงลงตอนกดค้าง (เบากว่า = กดค้างแล้วกระโดดสูงขึ้น)
// --------------------------------------------------------------------------

struct Block { float x; int w; int h; };
const int MAX_BLOCKS = 6;
Block blocks[MAX_BLOCKS];
int blockCount;
float runY;          // ความสูงของตัววิ่ง: 0 = อยู่บนพื้น, ติดลบ = ลอยอยู่
float runVy;         // ความเร็วขึ้นลง: ติดลบ = กำลังขึ้น, บวก = กำลังตก
float runSpeed, runDistance, runGap;
int runFrame;

// ทำครั้งเดียว ตอนเริ่มเกม
void runnerStart() {
  runY = 0;
  runVy = 0;
  runSpeed = RUN_START_SPEED;
  runDistance = 0;
  runGap = 60;
  runFrame = 0;
  blockCount = 0;
}

// ทำทุกรอบ (30 ครั้งต่อวินาที): อ่านปุ่ม ขยับ เช็กการชน
void runnerStep() {
  // MISSION 1: กดปุ่ม และยืนอยู่บนพื้น = กระโดด   (ขึ้นข้างบน = ตัวเลขติดลบ)
  if (btnPressed && runY == 0) runVy = 0;

  // แรงโน้มถ่วงดึงลงทุกรอบ (ถ้ากดค้างตอนกำลังขึ้น ดึงเบาลง)
  if (btnDown && runVy < 0) runVy += RUN_GRAVITY_HOLD;
  else runVy += RUN_GRAVITY;

  runY += runVy;                              // ตำแหน่งเปลี่ยนตามความเร็ว
  if (runY >= 0) { runY = 0; runVy = 0; }     // ถึงพื้นแล้ว หยุด

  if (runSpeed < RUN_MAX_SPEED) runSpeed += 0.0015;   // ค่อย ๆ เร็วขึ้นจนถึงเร็วสุด
  runDistance += runSpeed;                    // คะแนน = ระยะทางที่วิ่ง
  runFrame++;

  // move the blocks left and forget the ones that left the screen
  int kept = 0;
  for (int i = 0; i < blockCount; i++) {
    blocks[i].x -= runSpeed;
    if (blocks[i].x + blocks[i].w > 0) blocks[kept++] = blocks[i];
  }
  blockCount = kept;

  // add a new block after a random gap
  runGap -= runSpeed;
  if (runGap <= 0 && blockCount < MAX_BLOCKS) {
    blocks[blockCount++] = { (float)SCREEN_W, (int)random(4, 8), (int)random(6, 13) };
    runGap = runSpeed * 21 + 16 + random(0, 61);
  }

  // ชน = ตัววิ่ง (x 17-23) ทับกล่อง ทั้งแนวนอน และ แนวตั้ง
  float runnerTop = 54 + runY;
  for (int i = 0; i < blockCount; i++) {
    bool overlapX = blocks[i].x < 23 && blocks[i].x + blocks[i].w > 17;
    bool overlapY = runnerTop + 7 >= 62 - blocks[i].h;
    if (overlapX && overlapY) {
      feedback(300);
      gameOver((int)(runDistance / 10));
      return;
    }
  }
}

// ทำทุกรอบ: วาดจอ
void runnerDraw() {
  char buf[20];
  snprintf(buf, sizeof buf, "SCORE %d", (int)(runDistance / 10));
  text(0, 4, buf);
  snprintf(buf, sizeof buf, "HI %d", best[0]);
  textRight(4, buf);

  display.drawFastHLine(0, 62, SCREEN_W, SH110X_WHITE);                         // the ground
  int offset = (int)runDistance % 16;
  for (int x = -offset; x < SCREEN_W; x += 16) display.drawPixel(x + 5, 63, SH110X_WHITE);  // dots scroll by
  for (int i = 0; i < blockCount; i++)
    display.fillRect(px(blocks[i].x), 62 - blocks[i].h, blocks[i].w, blocks[i].h, SH110X_WHITE);

  const char* const* pose = runY < 0 ? RUN_J : ((runFrame / 4) % 2 ? RUN_A : RUN_B);
  sprite(16, px(54 + runY), pose, 8);
}

// =====================================================================
// 5. REEF: ปลาว่ายลอดช่องระหว่างสาหร่ายกับหิน
// =====================================================================

// ---------- TUNE REEF: ปรับเกมตรงนี้ เปลี่ยนแค่ตัวเลขหลังเครื่องหมาย = ----------
const float REEF_START_SPEED = 1.4;   // ความเร็วตอนเริ่ม
const float REEF_MAX_SPEED   = 2.0;   // เร็วสุดได้แค่ไหน
const float REEF_SINK        = 0.28;  // ปลาจมเร็วแค่ไหน
const int   REEF_GAP         = 26;    // ความกว้างของช่องตอนเริ่ม (จุด)
// -------------------------------------------------------------------------

struct Column { float x; int gapY; int gapH; bool passed; };
struct Bubble { float x; float y; int life; };
const int MAX_COLUMNS = 5;
const int MAX_BUBBLES = 6;
Column columns[MAX_COLUMNS];
int columnCount;
Bubble bubbles[MAX_BUBBLES];
int bubbleCount;
float fishY;         // ตำแหน่งปลา: 16 = บนสุดของสนาม, 63 = พื้นทะเล
float fishVy;        // ความเร็วขึ้นลง: ติดลบ = ว่ายขึ้น, บวก = จม
float reefSpeed, reefNext;
int reefScore, reefFrame;
bool reefStarted;    // true = เริ่มว่ายแล้ว (หลังแตะครั้งแรก)

// ทำครั้งเดียว ตอนเริ่มเกม
void reefStart() {
  fishY = 34;
  fishVy = 0;
  reefSpeed = REEF_START_SPEED;
  reefNext = 30;
  reefScore = 0;
  reefFrame = 0;
  reefStarted = false;
  columnCount = 0;
  bubbleCount = 0;
}

// ทำทุกรอบ (30 ครั้งต่อวินาที): อ่านปุ่ม ขยับ เช็กการชน นับคะแนน
void reefStep() {
  reefFrame++;

  // MISSION 1: กดปุ่ม = ว่ายขึ้น   (ขึ้นข้างบน = ตัวเลขติดลบ)
  if (btnPressed) {
    reefStarted = true;
    fishVy = 0;
    if (bubbleCount < MAX_BUBBLES) bubbles[bubbleCount++] = { 33, fishY + 1, 20 };
  }

  // bubbles float up and drift back
  int kept = 0;
  for (int i = 0; i < bubbleCount; i++) {
    bubbles[i].y -= 0.5;
    bubbles[i].x -= reefSpeed * 0.5;
    bubbles[i].life--;
    if (bubbles[i].life > 0) bubbles[kept++] = bubbles[i];
  }
  bubbleCount = kept;

  if (!reefStarted) {                              // ลอยรอ จนกว่าจะแตะครั้งแรก
    fishY = 34 + sin(reefFrame / 6.0) * 2;
    return;
  }

  fishVy += REEF_SINK;                             // ปลาจมเร็วขึ้นทีละนิด
  if (fishVy > 4) fishVy = 4;                      // แต่ไม่เร็วเกิน 4
  fishY += fishVy;                                 // ตำแหน่งเปลี่ยนตามความเร็ว
  if (fishY < FIELD) { fishY = FIELD; fishVy = 0; }   // ชนเพดาน หยุด
  if (reefSpeed < REEF_MAX_SPEED) reefSpeed += 0.0008;

  kept = 0;
  for (int i = 0; i < columnCount; i++) {
    columns[i].x -= reefSpeed;
    if (columns[i].x + 8 > 0) columns[kept++] = columns[i];
  }
  columnCount = kept;

  reefNext -= reefSpeed;
  if (reefNext <= 0 && columnCount < MAX_COLUMNS) {
    int gapH = REEF_GAP - reefScore / 2;           // ยิ่งได้คะแนน ช่องยิ่งแคบ
    if (gapH < 18) gapH = 18;                      // แต่ไม่แคบกว่า 18 จุด
    int gapY = random(FIELD + 3, SCREEN_H - 4 - gapH + 1);
    columns[columnCount++] = { (float)SCREEN_W, gapY, gapH, false };
    reefNext = 46;
  }

  float fishTop = fishY;                           // the fish is 9 x 5 pixels, at x 24-32
  float fishBottom = fishY + 4;
  if (fishBottom >= SCREEN_H - 1) {                // แตะพื้นทะเล = จบเกม
    feedback(300);
    gameOver(reefScore);
    return;
  }
  for (int i = 0; i < columnCount; i++) {
    Column &c = columns[i];
    bool overlapX = 32 > c.x && 24 < c.x + 8;
    // ชน = อยู่ตรงเสา และ (สูงกว่าช่อง หรือ ต่ำกว่าช่อง)
    if (overlapX && (fishTop < c.gapY || fishBottom > c.gapY + c.gapH)) {
      feedback(300);
      gameOver(reefScore);
      return;
    }
    // ลอดผ่านช่องได้ 1 ช่อง = 1 คะแนน
    if (!c.passed && c.x + 8 < 24) {
      c.passed = true;
      reefScore++;
      feedback(40);
    }
  }
}

// ทำทุกรอบ: วาดจอ
void reefDraw() {
  char buf[20];
  snprintf(buf, sizeof buf, "SCORE %d", reefScore);
  text(0, 4, buf);
  snprintf(buf, sizeof buf, "HI %d", best[1]);
  textRight(4, buf);

  for (int i = 0; i < columnCount; i++) {
    int x = px(columns[i].x);
    for (int k = 0; k < 8; k += 2)                                  // kelp hanging from the top
      for (int y = FIELD; y < columns[i].gapY; y++)
        display.drawPixel(x + k + (((y + k) >> 2) & 1), y, SH110X_WHITE);
    int rockTop = columns[i].gapY + columns[i].gapH;               // a rock rising from the bottom
    display.drawFastHLine(x + 2, rockTop, 4, SH110X_WHITE);
    display.drawFastHLine(x + 1, rockTop + 1, 6, SH110X_WHITE);
    display.fillRect(x, rockTop + 2, 8, SCREEN_H - rockTop - 2, SH110X_WHITE);
  }
  sprite(24, px(fishY), FISH, 5);
  for (int i = 0; i < bubbleCount; i++)
    display.drawCircle(px(bubbles[i].x), px(bubbles[i].y), 1, SH110X_WHITE);
  if (!reefStarted) textCenter(54, "TAP TO SWIM");
}

// =====================================================================
// 6. MAIN: เมนู จบเกม setup() และ loop()
// =====================================================================
void startGame() {
  mode = MODE_PLAY;
  if (game == 0) runnerStart();
  else reefStart();
}

void gameOver(int score) {
  lastScore = score;
  newBest = score > best[game];
  if (newBest) {
    best[game] = score;
    prefs.putInt(GAME_KEYS[game], score);  // saved in flash, kept after power-off
  }
  mode = MODE_OVER;
  overAt = millis();
}

void menuDraw(unsigned long now) {
  // MISSION 0: ใส่ชื่อเราแทน ONE-BUTTON ARCADE (ภาษาอังกฤษ ไม่เกิน 21 ตัว อยู่ใน " " เสมอ)
  textCenter(3, "ONE-BUTTON ARCADE");
  for (int i = 0; i < GAME_COUNT; i++) {
    int y = 21 + i * 14;
    char buf[8];
    snprintf(buf, sizeof buf, "%d", best[i]);
    if (i == game) {                       // the selected game is shown inverted
      display.fillRect(0, y - 2, SCREEN_W, 11, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    }
    text(4, y, GAME_NAMES[i]);
    text(SCREEN_W - 4 - textWidth(buf), y, buf);
    display.setTextColor(SH110X_WHITE);
  }
  if (btnDown && !holdUsed) {              // a bar fills while the button is held
    display.drawRect(0, 56, SCREEN_W, 8, SH110X_WHITE);
    float done = min(1.0f, (now - btnDownAt) / (float)HOLD_MS);
    display.fillRect(2, 58, px((SCREEN_W - 4) * done), 4, SH110X_WHITE);
  } else {
    textCenter(56, "TAP:NEXT  HOLD:PLAY");
  }
}

void overDraw(unsigned long now) {
  char buf[24];
  textCenter(4, "GAME OVER");
  textCenter(20, GAME_NAMES[game]);
  snprintf(buf, sizeof buf, "SCORE %d", lastScore);
  textCenter(31, buf);
  if (newBest) {
    textCenter(41, "NEW BEST!");
  } else {
    snprintf(buf, sizeof buf, "BEST %d", best[game]);
    textCenter(41, buf);
  }
  if (now - overAt > 700) textCenter(55, "TAP:AGAIN HOLD:MENU");
}

// 1 รอบเกม: ขยับและเช็ก ตามหน้าจอที่อยู่
void step(unsigned long now) {
  if (mode == MODE_MENU) {
    if (holdReached(now)) startGame();
    else if (btnTapped) game = (game + 1) % GAME_COUNT;
  } else if (mode == MODE_PLAY) {
    if (game == 0) runnerStep();
    else reefStep();
  } else {
    // ignore presses that started in the first 0.7 s, so a crash doesn't skip this screen
    bool freshPress = btnDownAt >= overAt + 700;
    if (freshPress && holdReached(now)) mode = MODE_MENU;
    else if (freshPress && btnTapped) startGame();
  }
}

// 1 รอบเกม: ลบจอ แล้ววาดใหม่
void drawScreen(unsigned long now) {
  display.clearDisplay();
  if (mode == MODE_MENU) menuDraw(now);
  else if (mode == MODE_OVER) overDraw(now);
  else if (game == 0) runnerDraw();
  else reefDraw();
  display.display();
}

// setup(): ทำครั้งเดียวตอนเปิดเครื่อง
void setup() {
  setCpuFrequencyMhz(CPU_MHZ);
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  if (USE_MOTOR) {
    pinMode(VIB_PIN, OUTPUT);
    digitalWrite(VIB_PIN, LOW);
  }

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!display.begin(OLED_ADDRESS, true)) {
    Serial.println("OLED not found. Check VCC, GND, SDA (D21) and SCL (D22).");
    while (true) delay(100);
  }
  display.setTextColor(SH110X_WHITE);
  display.setTextWrap(false);

  prefs.begin("arcade", false);
  for (int i = 0; i < GAME_COUNT; i++) best[i] = prefs.getInt(GAME_KEYS[i], 0);

  nextStep = millis();
  lastActivity = millis();
}

// loop(): ทำซ้ำตลอดเวลา  อ่านปุ่ม → ทุก 33 ms: step() ขยับและเช็ก → drawScreen() วาดจอ
void loop() {
  unsigned long now = millis();
  readButton(now);
  updateFeedback(now);
  if (!btnDown && now - lastActivity > IDLE_MS) {           // nobody has pressed for a minute
    sleepUntilPressed();
    return;
  }

  if ((long)(now - nextStep) < 0) return;               // not time for the next step yet
  nextStep += TICK;
  if ((long)(now - nextStep) > 200) nextStep = now + TICK;   // fell far behind: skip ahead

  step(now);
  btnPressed = false;
  btnTapped = false;
  drawScreen(now);
}
