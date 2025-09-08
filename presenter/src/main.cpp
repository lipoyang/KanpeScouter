#include <ArduinoBLE.h>

// PowerPointの状態を返す応答データの構造体
struct PptResponse {
  uint8_t isActive;     // プレゼンテーションがアクティブかどうか
  uint8_t isRunning;    // スライドショーが実行中かどうか
  uint8_t isBlackout;   // ブラックアウトモードかどうか
  uint16_t currentPage; // 現在のスライド番号（1から始まる）
  uint16_t totalPages;  // 総スライド数
  char note[300];       // 現在のスライドのノート(UTF-8, NULL終端)
}__attribute__((packed)); // パディングを防ぐ

// bool型の代わりに明示的に1バイトの整数型を使う
const uint8_t TRUE = 1;
const uint8_t FALSE = 0;

// BLEサービスとキャラクタリスティックの定義
BLEService        svcPptCtrl ("ba21ce66-9974-4ecd-b2e5-ab6d1497a7f0");
BLECharacteristic chrCommand ("ba21ce66-9974-4ecd-b2e5-ab6d1497a7f1",
                         BLENotify, 20);
BLECharacteristic chrResponse("ba21ce66-9974-4ecd-b2e5-ab6d1497a7f2",
                         BLEWrite, sizeof(PptResponse));

// 初期化
void setup()
{
  Serial.begin(115200);
  Serial.println("initializing...");

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

  Serial.println("KanpeScouter Peripheral ready");
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

    // 接続が切れるまで
    while (central.connected())
    {
      // コマンド送信
      if(Serial.available()) {
        char c = Serial.read();
        char* command = (char*)"";
        bool isCommand = true;
        switch(c) {
          case 's': command = (char*)"start"; break;
          case 'n': command = (char*)"next";  break;
          case 'p': command = (char*)"prev";  break;
          case 'b': command = (char*)"black"; break;
          case 'c': command = (char*)"check"; break;
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
      // 応答受信
      if (chrResponse.written()) {
        PptResponse *val = (PptResponse*)chrResponse.value();
        Serial.println("Written:");
        Serial.println(val->isActive);
        Serial.println(val->isRunning);
        Serial.println(val->isBlackout);
        Serial.println(val->currentPage); 
        Serial.println(val->totalPages);
        Serial.println(val->note);
      }
    }
    Serial.print("Disconnected from central: ");
    Serial.println(central.address());
  }
}

