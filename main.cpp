#include <Arduino.h>
#include <DFMiniMp3.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>

// ----------- Pines ----------
#define PIN_VOL_UP     26
#define PIN_VOL_DOWN   25
#define PIN_NEXT       35
#define PIN_PREV       32
#define PIN_PAUSE      33

#define OLED_CLK       18
#define OLED_MOSI      23
#define OLED_DC         5
#define OLED_RESET      4

// ---------- OLED ------------
Adafruit_SSD1306 display(128, 64, &SPI, OLED_DC, OLED_RESET, 15);

// ---------- DFPlayer --------
class Mp3Notify;
HardwareSerial dfSerial(2);
DFMiniMp3<HardwareSerial, Mp3Notify> mp3(dfSerial);

// ---------- Variables ---------
uint8_t volume = 20;
uint16_t currentTrack = 1;
uint16_t totalTracks = 0;
bool isPaused = false;
bool needDisplayUpdate = true;

// ---------- Notificaciones ----
class Mp3Notify {
public:
  static void OnError(DFMiniMp3<HardwareSerial, Mp3Notify>&, uint16_t) {}

  static void OnPlayFinished(DFMiniMp3<HardwareSerial, Mp3Notify>& mp3, DfMp3_PlaySources, uint16_t) {
    currentTrack++;
    if (currentTrack > totalTracks) currentTrack = 1;
    mp3.playMp3FolderTrack(currentTrack);
    needDisplayUpdate = true;
  }

  static void OnPlaySourceOnline(DFMiniMp3<HardwareSerial, Mp3Notify>&, DfMp3_PlaySources) {}
  static void OnPlaySourceInserted(DFMiniMp3<HardwareSerial, Mp3Notify>&, DfMp3_PlaySources) {}
  static void OnPlaySourceRemoved(DFMiniMp3<HardwareSerial, Mp3Notify>&, DfMp3_PlaySources) {}
};

// ---------- Función para mostrar OLED ---------
void updateDisplay() {
  display.clearDisplay();

  // TRACK
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print("Track: ");
  display.print(currentTrack);
  display.print("/");
  display.println(totalTracks);

  // VOLUMEN
  display.setCursor(0, 32);
  display.print("Vol: ");
  display.println(volume);

  // ESTADO ("Play" o "Pause") centrado y pequeño
  display.setTextSize(1);
  const char* estado = isPaused ? "Pause" : "Play";

  int16_t x, y;
  uint16_t w, h;
  display.getTextBounds(estado, 0, 0, &x, &y, &w, &h);
  int posX = (128 - w) / 2;  // centrar horizontalmente
  int posY = 54;             // posición vertical (puedes ajustar)

  display.setCursor(posX, posY);
  display.print(estado);

  display.display();
}

// ---------- Tareas FreeRTOS ------------

void taskButtons(void *param) {
  bool lastNext = HIGH, lastPrev = HIGH, lastPause = HIGH;
  for (;;) {
    bool nowNext = digitalRead(PIN_NEXT);
    bool nowPrev = digitalRead(PIN_PREV);
    bool nowPause = digitalRead(PIN_PAUSE);

    if (lastNext == HIGH && nowNext == LOW) {
      currentTrack++;
      if (currentTrack > totalTracks) currentTrack = 1;
      mp3.playMp3FolderTrack(currentTrack);
      needDisplayUpdate = true;
    }
    lastNext = nowNext;

    if (lastPrev == HIGH && nowPrev == LOW) {
      currentTrack = (currentTrack <= 1) ? totalTracks : currentTrack - 1;
      mp3.playMp3FolderTrack(currentTrack);
      needDisplayUpdate = true;
    }
    lastPrev = nowPrev;

    if (lastPause == HIGH && nowPause == LOW) {
      isPaused = !isPaused;
      isPaused ? mp3.pause() : mp3.start();
      needDisplayUpdate = true;
    }
    lastPause = nowPause;

    vTaskDelay(50 / portTICK_PERIOD_MS);  
  }
}

void taskVolume(void *param) {
  bool lastUp = HIGH, lastDown = HIGH;
  for (;;) {
    bool nowUp = digitalRead(PIN_VOL_UP);
    bool nowDown = digitalRead(PIN_VOL_DOWN);

    if (lastUp == HIGH && nowUp == LOW && volume < 30) {
      volume++;
      mp3.setVolume(volume);
      needDisplayUpdate = true;
    }
    lastUp = nowUp;

    if (lastDown == HIGH && nowDown == LOW && volume > 0) {
      volume--;
      mp3.setVolume(volume);
      needDisplayUpdate = true;
    }
    lastDown = nowDown;

    vTaskDelay(100 / portTICK_PERIOD_MS); 
  }
}

void taskDisplayUpdate(void *param) {
  for (;;) {
    if (needDisplayUpdate) {
      updateDisplay();
      needDisplayUpdate = false;
    }
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}

void taskDFPlayerLoop(void *param) {
  for (;;) {
    mp3.loop();
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

void taskSerialControl(void *param) {
  String input = "";
  for (;;) {
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        input.trim();
        if (input.equalsIgnoreCase("on")) {
          if (isPaused) {
            isPaused = false;
            mp3.start();
            needDisplayUpdate = true;
          }
        } else if (input.equalsIgnoreCase("off")) {
          if (!isPaused) {
            isPaused = true;
            mp3.pause();
            needDisplayUpdate = true;
          }
        }
        input = "";
      } else {
        input += c;
      }
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}


// ------------ Setup -------------
void setup() {
  Serial.begin(115200);

  pinMode(PIN_NEXT, INPUT_PULLUP);
  pinMode(PIN_PREV, INPUT_PULLUP);
  pinMode(PIN_PAUSE, INPUT_PULLUP);
  pinMode(PIN_VOL_UP, INPUT_PULLUP);
  pinMode(PIN_VOL_DOWN, INPUT_PULLUP);

 if (!display.begin(SSD1306_SWITCHCAPVCC)) {
  while (1);  
  }
  display.setTextColor(WHITE);

  dfSerial.begin(9600, SERIAL_8N1, 16, 17);
  mp3.begin();
  delay(500);

  totalTracks = mp3.getTotalTrackCount();
  if (totalTracks == 0) totalTracks = 1;

  mp3.setVolume(volume);
  mp3.playMp3FolderTrack(currentTrack);

  updateDisplay();

  xTaskCreatePinnedToCore(taskSerialControl, "SerialCtrl", 2048, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(taskButtons, "Buttons", 2048, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(taskVolume, "Volume", 2048, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(taskDisplayUpdate, "Display", 2048, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(taskDFPlayerLoop, "DFPlayer", 2048, NULL, 1, NULL, 1);
}

void loop() {
  
}
