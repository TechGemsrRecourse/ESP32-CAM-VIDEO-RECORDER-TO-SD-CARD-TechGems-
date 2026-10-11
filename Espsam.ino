/*
 * ESP32-CAM One-Button Video Recorder  (AI-Thinker pinout)
 * - Button between GPIO 12 and GND (internal pull-up used)
 * - Press once: start recording to SD (1-bit mode), red LED ON
 * - Press again: stop, finalize AVI file, red LED OFF
 * - VGA 640x480 Motion-JPEG in an AVI container (plays in VLC, many players)
 */

#include "esp_camera.h"
#include "FS.h"
#include "SD_MMC.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ---------- Pins ----------
#define BUTTON_PIN   12   // push-button to GND
#define RED_LED_PIN  33   // small red LED on back (ACTIVE LOW)
#define FLASH_PIN     4   // bright white flash LED (kept OFF)

// ---------- AI-Thinker camera pins ----------
#define PWDN_GPIO_NUM   32
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM    0
#define SIOD_GPIO_NUM   26
#define SIOC_GPIO_NUM   27
#define Y9_GPIO_NUM     35
#define Y8_GPIO_NUM     34
#define Y7_GPIO_NUM     39
#define Y6_GPIO_NUM     36
#define Y5_GPIO_NUM     21
#define Y4_GPIO_NUM     19
#define Y3_GPIO_NUM     18
#define Y2_GPIO_NUM      5
#define VSYNC_GPIO_NUM  25
#define HREF_GPIO_NUM   23
#define PCLK_GPIO_NUM   22

// ---------- Settings ----------
#define FRAME_W        640
#define FRAME_H        480
#define JPEG_QUALITY   12        // 10 = better/larger, 15 = smaller. 12 is a stable middle.
#define MAX_FRAMES     30000     // safety limit (~30+ min); auto-stops when reached
#define HEADER_SIZE    224

// ---------- State ----------
bool recording = false;
File aviFile;
uint32_t frameCount = 0;
uint32_t moviBytes = 0;
uint32_t startMs = 0;
uint32_t *idxTable = nullptr;    // pairs: offset, size (stored in PSRAM)

// ---------- Button debounce ----------
bool lastBtn = HIGH;
uint32_t lastChangeMs = 0;

// ---------- Little-endian helpers ----------
static void put32(uint8_t *b, int off, uint32_t v) {
  b[off] = v & 0xFF; b[off + 1] = (v >> 8) & 0xFF;
  b[off + 2] = (v >> 16) & 0xFF; b[off + 3] = (v >> 24) & 0xFF;
}
static void put16(uint8_t *b, int off, uint16_t v) {
  b[off] = v & 0xFF; b[off + 1] = (v >> 8) & 0xFF;
}
static void putTag(uint8_t *b, int off, const char *t) { memcpy(b + off, t, 4); }

static void buildHeader(uint8_t *b) {
  memset(b, 0, HEADER_SIZE);
  putTag(b, 0, "RIFF");  put32(b, 4, 0);          putTag(b, 8, "AVI ");
  putTag(b, 12, "LIST"); put32(b, 16, 192);       putTag(b, 20, "hdrl");
  putTag(b, 24, "avih"); put32(b, 28, 56);
  put32(b, 32, 66666);                            // us per frame (patched later)
  put32(b, 44, 0x10);                             // has index
  put32(b, 48, 0);                                // total frames (patched later)
  put32(b, 56, 1);                                // streams
  put32(b, 60, 100000);                           // suggested buffer
  put32(b, 64, FRAME_W); put32(b, 68, FRAME_H);
  putTag(b, 88, "LIST"); put32(b, 92, 116);       putTag(b, 96, "strl");
  putTag(b, 100, "strh"); put32(b, 104, 56);
  putTag(b, 108, "vids"); putTag(b, 112, "MJPG");
  put32(b, 128, 1000);                            // scale (patched later)
  put32(b, 132, 15000);                           // rate  (patched later)
  put32(b, 140, 0);                               // length (patched later)
  put32(b, 144, 100000);
  put32(b, 148, 0xFFFFFFFF);                      // quality
  put16(b, 160, FRAME_W); put16(b, 162, FRAME_H);
  putTag(b, 164, "strf"); put32(b, 168, 40);
  put32(b, 172, 40); put32(b, 176, FRAME_W); put32(b, 180, FRAME_H);
  put16(b, 184, 1);  put16(b, 186, 24);
  putTag(b, 188, "MJPG");
  put32(b, 192, FRAME_W * FRAME_H * 3);
  putTag(b, 212, "LIST"); put32(b, 216, 4);      putTag(b, 220, "movi");
}

static void patch32(uint32_t pos, uint32_t v) {
  uint8_t t[4]; put32(t, 0, v);
  aviFile.seek(pos);
  aviFile.write(t, 4);
}

// ---------- LED ----------
static void redLed(bool on) { digitalWrite(RED_LED_PIN, on ? LOW : HIGH); }

static void errorBlink() {         // endless fast blink = something failed
  while (true) { redLed(true); delay(150); redLed(false); delay(150); }
}

// ---------- Camera ----------
static bool startCamera() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM; c.pin_d1 = Y3_GPIO_NUM; c.pin_d2 = Y4_GPIO_NUM; c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM; c.pin_d5 = Y7_GPIO_NUM; c.pin_d6 = Y8_GPIO_NUM; c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM; c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM; c.pin_href = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM; c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM; c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size   = FRAMESIZE_VGA;          // 640x480
  c.jpeg_quality = JPEG_QUALITY;
  c.fb_count     = 2;
  c.fb_location  = CAMERA_FB_IN_PSRAM;
  c.grab_mode    = CAMERA_GRAB_LATEST;
  return esp_camera_init(&c) == ESP_OK;
}

// ---------- Find next free filename ----------
static String nextFilename() {
  char name[32];
  for (int i = 1; i < 10000; i++) {
    snprintf(name, sizeof(name), "/video_%04d.avi", i);
    if (!SD_MMC.exists(name)) return String(name);
  }
  return String("/video_9999.avi");
}

// ---------- Start / Stop ----------
static void startRecording() {
  // SD card in 1-bit mode (frees GPIO 4, 12, 13)
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("SD card mount failed");
    errorBlink();
  }
  pinMode(FLASH_PIN, OUTPUT);
  digitalWrite(FLASH_PIN, LOW);            // keep white flash off

  if (SD_MMC.cardType() == CARD_NONE) { Serial.println("No SD card"); errorBlink(); }

  if (!startCamera()) { Serial.println("Camera init failed"); errorBlink(); }

  if (!idxTable) idxTable = (uint32_t *)ps_malloc((size_t)MAX_FRAMES * 8);
  if (!idxTable) { Serial.println("Index alloc failed"); errorBlink(); }

  String fn = nextFilename();
  aviFile = SD_MMC.open(fn.c_str(), FILE_WRITE);
  if (!aviFile) { Serial.println("Cannot open file"); errorBlink(); }

  uint8_t hdr[HEADER_SIZE];
  buildHeader(hdr);
  aviFile.write(hdr, HEADER_SIZE);

  frameCount = 0;
  moviBytes = 0;

  // let auto-exposure settle: discard a few frames
  for (int i = 0; i < 3; i++) {
    camera_fb_t *f = esp_camera_fb_get();
    if (f) esp_camera_fb_return(f);
  }

  startMs = millis();
  recording = true;
  redLed(true);
  Serial.printf("Recording to %s\n", fn.c_str());
}

static void stopRecording() {
  recording = false;                       // halt frame stream first
  uint32_t elapsed = millis() - startMs;
  if (elapsed == 0) elapsed = 1;

  // write idx1 index
  uint8_t chunk[16 * 64];
  uint32_t done = 0;
  uint8_t tag[8];
  putTag(tag, 0, "idx1"); put32(tag, 4, frameCount * 16);
  aviFile.write(tag, 8);
  while (done < frameCount) {
    uint32_t n = min((uint32_t)64, frameCount - done);
    for (uint32_t i = 0; i < n; i++) {
      uint8_t *e = chunk + i * 16;
      putTag(e, 0, "00dc");
      put32(e, 4, 0x10);
      put32(e, 8, idxTable[(done + i) * 2]);
      put32(e, 12, idxTable[(done + i) * 2 + 1]);
    }
    aviFile.write(chunk, n * 16);
    done += n;
  }

  uint32_t total = aviFile.position();
  uint32_t fpsX1000 = (uint32_t)((uint64_t)frameCount * 1000000ULL / elapsed);
  if (fpsX1000 == 0) fpsX1000 = 1000;
  uint32_t usPerFrame = (uint32_t)(1000000000ULL / fpsX1000);

  // patch header fields
  patch32(4,   total - 8);
  patch32(32,  usPerFrame);
  patch32(48,  frameCount);
  patch32(128, 1000);
  patch32(132, fpsX1000);
  patch32(140, frameCount);
  patch32(216, 4 + moviBytes);

  aviFile.flush();
  aviFile.close();

  esp_camera_deinit();                     // camera fully idle again
  SD_MMC.end();                            // safe to remove card now
  redLed(false);
  Serial.printf("Saved %u frames, %.1f fps\n", frameCount, fpsX1000 / 1000.0);
}

static void recordOneFrame() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return;

  uint32_t len = fb->len;
  uint32_t padded = (len + 1) & ~1U;       // AVI chunks must be even-sized

  uint8_t h[8];
  putTag(h, 0, "00dc"); put32(h, 4, len);

  idxTable[frameCount * 2]     = 4 + moviBytes;   // offset relative to 'movi'
  idxTable[frameCount * 2 + 1] = len;

  aviFile.write(h, 8);
  aviFile.write(fb->buf, len);
  if (padded != len) { uint8_t z = 0; aviFile.write(&z, 1); }

  moviBytes += 8 + padded;
  frameCount++;
  esp_camera_fb_return(fb);

  if ((frameCount % 100) == 0) aviFile.flush();   // limit data loss on power cut
  if (frameCount >= MAX_FRAMES) stopRecording();  // safety stop
}

// ---------- Arduino ----------
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);      // avoid brownout resets on USB power dips
  Serial.begin(115200);
  pinMode(RED_LED_PIN, OUTPUT);
  redLed(false);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.println("Ready. Press the button to start/stop recording.");
}

void loop() {
  // button handling with 50 ms debounce (falling edge = press)
  bool btn = digitalRead(BUTTON_PIN);
  if (btn != lastBtn && millis() - lastChangeMs > 50) {
    lastChangeMs = millis();
    lastBtn = btn;
    if (btn == LOW) {
      if (!recording) startRecording();
      else            stopRecording();
    }
  }

  if (recording) recordOneFrame();
}