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

void setup()
{
  Serial.begin(115200);
  Serial.println(F("Hello! ST7789 LCD Test"));

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

enum {
  RX_IDLE = 0,
  RX_STX,
  RX_MSG,
  RX_ETX
};
char rx_buff[256];
int rx_index = 0;
int rx_state = RX_IDLE;

void serial_com()
{
  while(Serial1.available() > 0){
    char c = Serial1.read();
    switch(rx_state){
      case RX_IDLE:
        if(c == '$'){
          rx_state = RX_STX;
        }
        break;
      case RX_STX:
        if(c == '$'){
          rx_state = RX_MSG;
          rx_index = 0;
        } else {
          rx_state = RX_IDLE;
        }
        break;
      case RX_MSG:
        rx_buff[rx_index] = c;
        rx_index++;
        if(rx_index >= sizeof(rx_buff)-1){
          rx_index = sizeof(rx_buff)-1;
        }
        if(c == '&'){
          rx_state = RX_ETX;
        }
        break;
      case RX_ETX:
        if(c == '&'){
          rx_buff[rx_index - 1] = 0x00;
          // メッセージ受信完了
          rx_state = RX_IDLE;
          Serial.print("RX:");
          Serial.println(rx_buff);

          const lgfx::v1::IFont *font = &fonts::lgfxJapanGothic_24;
          sprite.setFont(font);
          sprite.setTextColor(TFT_GREEN, TFT_BLACK);
          sprite.fillScreen(TFT_BLACK);
          sprite.setCursor(0, 24);
          sprite.setTextWrap(true);
          sprite.println(rx_buff);
          sprite.pushSprite(0, 0);

        }else{
          rx_buff[rx_index] = c;
          rx_index++;
          if(rx_index >= sizeof(rx_buff)-1){
            rx_index = sizeof(rx_buff)-1;
          }
          rx_state = RX_MSG;
        }
        break;
      default:
        rx_state = RX_IDLE;
        break;
    }
  }
}

void loop()
{
  serial_com();

#if 0
  // USBシリアルでスペース ' ' を送ると文字サイズが変わる
  static bool update = true;
  static bool baikaku = false;
  while(Serial.available() > 0){
    char c = Serial.read();
    if(c == ' '){
      update = true;
      baikaku = !baikaku;
    }
  }

  if(update){
    update = false;
    if(baikaku == false){
      // 16ドットフォント, 緑色
      const lgfx::v1::IFont *font = &fonts::lgfxJapanGothic_16;
      sprite.setFont(font);
      // sprite.setTextSize(2);
      sprite.setTextColor(TFT_GREEN, TFT_BLACK);
      sprite.fillScreen(TFT_BLACK);
      sprite.setCursor(0, 0);
      sprite.setTextWrap(true);
      const char* str = text2.c_str();
      sprite.println(str);
      sprite.pushSprite(0, 0);
    }else{
      // 24ドットフォント, 水色
      const lgfx::v1::IFont *font = &fonts::lgfxJapanGothic_24;
      sprite.setFont(font);
      sprite.setTextColor(TFT_CYAN, TFT_BLACK);
      sprite.fillScreen(TFT_BLACK);
      sprite.setCursor(0, 0);
      sprite.setTextWrap(true);
      const char* str = text1.c_str();
      sprite.println(str);
      sprite.pushSprite(0, 0);
    }
  }
#endif
}
