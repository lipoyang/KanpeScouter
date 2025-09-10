#include <Arduino.h>

// ピン割り当て
#define TFT_MISO      -1 // 接続しない
#define TFT_MOSI      D10
#define TFT_SCLK      D8
#define TFT_CS        D7
#define TFT_DC        D6
#define TFT_RST       D5
#define TFT_BL        D4
#define UART1_RX      D3  // P29
#define UART1_TX      D2  // P28

// 画面デバイスの定義
#include <LovyanGFX.hpp>
class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ST7789  _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
  lgfx::Light_PWM     _light_instance;

public:
  LGFX(void)
  {
    { // SPIバスの設定
      auto cfg = _bus_instance.config();

      cfg.spi_host = 0;          // 使用するSPIを選択
      cfg.spi_mode = 3;          // SPI通信モード (0 ~ 3)
      cfg.freq_write = 40000000; // 送信時のSPIクロック
      cfg.freq_read = 20000000;  // 受信時のSPIクロック

      cfg.pin_sclk = TFT_SCLK;
      cfg.pin_mosi = TFT_MOSI;
      cfg.pin_miso = TFT_MISO;
      cfg.pin_dc   = TFT_DC;

      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    { // 表示パネルの設定
      auto cfg = _panel_instance.config();

      cfg.pin_cs   = TFT_CS;
      cfg.pin_rst  = TFT_RST;
      cfg.pin_busy = -1; // 接続しない

      cfg.panel_width  = 240;   // 幅
      cfg.panel_height = 280;   // 高さ
      cfg.offset_x = 0;         // X方向オフセット
      cfg.offset_y = 20;        // Y方向オフセット (なぜか20ドットずれる)

      cfg.invert = true;        // 明暗が反転する場合 true

      _panel_instance.config(cfg);
    }
    { // バックライトの設定
      auto cfg = _light_instance.config();

      cfg.pin_bl = TFT_BL;
      cfg.invert = false;       // 輝度が反転する場合 true
      cfg.freq   = 44100;       // バックライトのPWM周波数
      cfg.pwm_channel = 0;      // 使用するPWMのチャンネル番号 (注意!)

      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }
    setPanel(&_panel_instance);
  }
};
#include <LGFX_TFT_eSPI.hpp>
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft); // スプライト

// シリアル受信バッファと状態
enum {
  RX_IDLE = 0, // 受信待ち
  RX_RECV,     // 受信中
};
char rx_buff[512];
int rx_index = 0;
int rx_state = RX_IDLE;

// PowerPointの状態を返す応答データの構造体
struct PptResponse {
  uint8_t isActive;     // プレゼンテーションがアクティブかどうか
  uint8_t isRunning;    // スライドショーが実行中かどうか
  uint8_t isBlackout;   // ブラックアウトモードかどうか
  uint16_t currentPage; // 現在のスライド番号（1から始まる）
  uint16_t totalPages;  // 総スライド数
  char note[300];       // 現在のスライドのノート(UTF-8, NULL終端)
};
PptResponse ppt_status;

// 初期化
void setup()
{
  Serial.begin(115200);
  Serial.println(F("Scouter for PowerPoint"));

  // Serial1のTXをGPIO4, RXをGPIO5に割り当て
  Serial1.setTX(UART1_TX);
  Serial1.setRX(UART1_RX);
  Serial1.begin(115200);

  // 画面の初期化
  tft.init();
  tft.setRotation(1);     // 画面回転(横向き)
  tft.setBrightness(255); // バックライト100%(全点灯)
  tft.fillScreen(TFT_BLACK);

  // スプライトの初期化
  int screenWidth = tft.width();
  int screenHeight = tft.height();
  sprite.createSprite(screenWidth, screenHeight);
}

// プレゼンターから受信したデータの処理
void on_recv_data(const char* data)
{
  Serial.print("RX:");
  Serial.println(rx_buff);

  // メッセージの解釈
  int result = sscanf(rx_buff,
    "%1hhx%1hhx%1hhx%4hhx%4hhx",
    &ppt_status.isActive,     // [0]
    &ppt_status.isRunning,    // [1]
    &ppt_status.isBlackout,   // [2]
    &ppt_status.currentPage,  // [3]-[6]
    &ppt_status.totalPages    // [7]-[10]
  );
  if(result != 5){
    Serial.println("ERROR: sscanf");
    return;
  }
  memcpy(ppt_status.note, &rx_buff[11], sizeof(ppt_status.note)-1);
  ppt_status.note[sizeof(ppt_status.note)-1] = '\0'; // 念のためNULL終端

  const lgfx::v1::IFont *font = &fonts::lgfxJapanGothic_24;
  sprite.setFont(font);
  sprite.setTextColor(TFT_GREEN, TFT_BLACK);
  sprite.fillScreen(TFT_BLACK);
  sprite.setCursor(0, 24);
  sprite.setTextWrap(true);
  sprite.println(ppt_status.note);
  sprite.pushSprite(0, 0);
}

// プレゼンターからのシリアル受信処理
void serial_com()
{
  while(Serial1.available() > 0){
    char c = Serial1.read();
    switch(rx_state){
      // 受信待ち
      case RX_IDLE:
        if(c == 0x02){ // STX
          rx_state = RX_RECV;
          rx_index = 0;
        }
        break;
      // 受信中
      case RX_RECV:
        if(c == 0x02){ // STX (途中でSTXが来たら最初から)
          rx_index = 0;
        }
        else if(c == 0x03){ // ETX
          rx_state = RX_IDLE;
          // 受信したデータの処理
          rx_buff[rx_index] = '\0';
          on_recv_data(rx_buff);
        }
        else{
          rx_buff[rx_index] = c;
          rx_index++;
          if(rx_index >= sizeof(rx_buff)-1){
            rx_index = sizeof(rx_buff)-1;
          }
        }
        break;
      default:
        rx_state = RX_IDLE;
        break;
    }
  }
}

// メインループ
void loop()
{
  // シリアル受信処理
  serial_com();
}
