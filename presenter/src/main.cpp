#include <Arduino.h>
#include <ArduinoBLE.h>
#include <Adafruit_NeoPixel.h>
#include "PollingTimer.h"

// ピン割り当て
#define PIN_BTN_NEXT  D1  // Nextボタン
#define PIN_BTN_PREV  D2  // Prevボタン
#define PIN_BTN_BLACK D3  // Blackout/Resumeボタン
#define PIN_BTN_START D4  // Start/Endボタン
#define PIN_NEOPIXEL  D8  // フルカラーLED

// ボタン入力の値
enum ButtonInput {
  BTN_NONE = 0, // 押されていない
  BTN_NEXT,     // Nextボタン
  BTN_PREV,     // Prevボタン
  BTN_BLACK,    // Blackout/Resumeボタン
  BTN_START     // Start/Endボタン
};

// ポーリングタイマー
IntervalTimer buttonTimer;
IntervalTimer statusTimer;

// PowerPointの状態
const uint8_t PPT_OFFLINE  = 0; // 未接続状態
const uint8_t PPT_NO_SLIDE = 1; // スライドショーが無い
const uint8_t PPT_STOPPED  = 2; // スライドショー停止中
const uint8_t PPT_RUNNING  = 3; // スライドショー実行中
const uint8_t PPT_BLACKOUT = 4; // ブラックアウト中

// PowerPointの状態を返す応答データの構造体
struct PptResponse {
  uint8_t status;       // PowerPointの状態
  uint16_t currentPage; // 現在のスライド番号（1から始まる）
  uint16_t totalPages;  // 総スライド数
  char note[300];       // 現在のスライドのノート(UTF-8, NULL終端)
}__attribute__((packed)); // パディングを防ぐ
PptResponse ppt;

// bool型の代わりに明示的に1バイトの整数型を使う
const uint8_t TRUE = 1;
const uint8_t FALSE = 0;

// BLEサービスとキャラクタリスティックの定義
BLEService        svcPptCtrl ("ba21ce66-9974-4ecd-b2e5-ab6d1497a7f0");
BLECharacteristic chrCommand ("ba21ce66-9974-4ecd-b2e5-ab6d1497a7f1",
                         BLENotify, 20);
BLECharacteristic chrResponse("ba21ce66-9974-4ecd-b2e5-ab6d1497a7f2",
                         BLEWrite, sizeof(PptResponse));

// フルカラーLED
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
const int LED_BRIGHTNESS = 32; // 明るさ(0-255)

// ボタン入力の取得
ButtonInput get_button_input()
{
  const  int buttonPin [4] = {PIN_BTN_NEXT, PIN_BTN_PREV, PIN_BTN_BLACK, PIN_BTN_START};
  static int lastState [4] = {HIGH, HIGH, HIGH, HIGH};
  int nowState[4];
  ButtonInput ret = BTN_NONE;
  for(int i=0; i<4; i++) {
    nowState[i] = digitalRead(buttonPin[i]);
    if(lastState[i] == HIGH && nowState[i] == LOW) {
      if(ret == BTN_NONE) ret = (ButtonInput)(i + 1); // 若い番号のボタンを優先
    }
    lastState[i] = nowState[i];
  }
  return ret;
}

// スカウターに送信
void send_to_scouter()
{
  static char tx_buff[512];

  sprintf(tx_buff, "%c%X%04X%04X%s%c",
          0x02, // STX
          ppt.status      & 0x0F,
          ppt.currentPage & 0xFFFF,
          ppt.totalPages  & 0xFFFF,
          ppt.note,
          0x03 // ETX
        );
  Serial1.print(tx_buff);
}

// フルカラーLEDの制御
void set_led_color()
{
  const uint32_t colorTable[] = {
    0xFF0000, // PPT_OFFLINE  赤
    0xC04800, // PPT_NO_SLIDE 橙
    0x808000, // PPT_STOPPED  黄
    0x00FF00, // PPT_RUNNING  緑
    0x0000FF  // PPT_BLACKOUT 青
  };

  pixels.setPixelColor(0, colorTable[ppt.status]);
  pixels.show();
}

// 初期化
void setup()
{
  Serial.begin(115200);
  Serial.println("initializing...");

  // スカウターとの通信用のシリアルポートを初期化
  Serial1.begin(115200);
  while (!Serial1){ delay(10); }

  // ボタンピンの設定
  pinMode(PIN_BTN_NEXT,  INPUT_PULLUP);
  pinMode(PIN_BTN_PREV,  INPUT_PULLUP);
  pinMode(PIN_BTN_BLACK, INPUT_PULLUP);
  pinMode(PIN_BTN_START, INPUT_PULLUP);

  // BLEの初期化
  if (!BLE.begin()) {
    Serial.println("starting BLE failed!");
    while (1);
  }

  // MACアドレスを取得して表示
  String addr = BLE.address();
  Serial.print("Device MAC Address: ");
  Serial.println(addr);

  // BLEデバイスの設定
  //BLE.setConnectionInterval(6, 80); // 7.25ms - 100ms
  BLE.setLocalName("KanpeScouter");
  BLE.setAdvertisedService(svcPptCtrl);
  svcPptCtrl.addCharacteristic(chrCommand);   // コマンド送信用
  svcPptCtrl.addCharacteristic(chrResponse);  // 応答受信用
  BLE.addService(svcPptCtrl);
  BLE.advertise();

  // NeoPixelの初期化
  pixels.begin();
  pixels.setBrightness(LED_BRIGHTNESS);

  Serial.println("KanpeScouter ready");
}

// メインループ
void loop()
{
  // セントラルから接続されたら
  BLEDevice central = BLE.central();
  if (central)
  {
    Serial.print("Connected to central: ");
    Serial.println(central.address());

    // ポーリングタイマーの設定
    buttonTimer.set(10);
    statusTimer.set(1000);
    bool toGetStatus = true;

    // 接続が切れるまで
    while (central.connected())
    {
      // ボタン入力の監視
      if (buttonTimer.elapsed()) {
        ButtonInput btn = get_button_input();
        char* command = (char*)"";
        bool isCommand = true;
        switch(btn) {
          case BTN_NEXT:  command = (char*)"next";  break;
          case BTN_PREV:  command = (char*)"prev";  break;
          case BTN_BLACK: command = (char*)"black"; break;
          case BTN_START: command = (char*)"start"; break;
          default:
            isCommand = false;
            break;
        }
        if(isCommand) {
          chrCommand.writeValue(command);
          Serial.print("Notify: ");
          Serial.println(command);
        }
      }
      // 接続直後には状態取得コマンドを送る
      if (toGetStatus && statusTimer.elapsed()) {
        char *command = (char*)"check";
        chrCommand.writeValue(command);
        Serial.print("Notify: ");
        Serial.println(command);
      }
      // 応答受信
      if (chrResponse.written()) {
        ppt = *(PptResponse*)chrResponse.value();
        ppt.note[sizeof(ppt.note)-1] = '\0'; // 念のためNULL終端を保証
        Serial.println("Written:");
        Serial.println(ppt.status);
        Serial.println(ppt.currentPage); 
        Serial.println(ppt.totalPages);
        Serial.println(ppt.note);

        // スカウターに送信
        send_to_scouter();
        // フルカラーLEDの制御
        set_led_color();

        toGetStatus = false; // 状態取得コマンドを送るフラグをクリア
      }
    }
    Serial.print("Disconnected from central: ");
    Serial.println(central.address());
  } // if (central) ココマデ

  // 切断状態
  ppt.status = PPT_OFFLINE;
  ppt.currentPage = 0;
  ppt.totalPages = 0;
  ppt.note[0] = '\0';

  // スカウターに送信
  send_to_scouter();
  // フルカラーLEDの制御
  set_led_color();

  delay(500);
}
