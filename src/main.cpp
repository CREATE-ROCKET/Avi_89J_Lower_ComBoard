#include <Arduino.h>
#include <stdlib.h>
#include <CANCREATE.h>
#include <SPICREATE.h>
#include <SPIflash.h>

#define CAN_RX 19
#define CAN_TX 18

#define TWELITE_RX_front 16
#define TWELITE_TX_front 17
#define TWELITE_RX_back 25 // S2M
#define TWELITE_TX_back 26 // M2S

#define GPS_RXD_TX 14
#define GPS_TXD_RX 4
#define GPS_SW 32

#define MISO 15
#define MOSI 21
#define SCK 27
#define FLASH_CS 2
#define SPI_FREQUENCY 5000000

#define LED 13

uint8_t tx_gps[256] = {};
uint8_t tx_pitot[256] = {};
int elapsed_time = 0;

typedef struct
{
  char data[100] = {}; // flashに保存するために1センテンスを保存するのに使う
  int index = 0;       //
  // int count = 0;
  uint32_t flash_address = 0x1000000; // 128Mbitの半分　0x2000000
  bool flash_ok = false;              // flash書き込み開始・終了に用いる
} PITOT;                              // 送られてくるピトー管のデータを保管するための構造体

typedef struct
{
  char data[100] = {}; // flashに保存するためにgpggaの１センテンスを保存するのに使う
  int index = 0;       // 1回のループでのGPGGA表示の一文字を保存する位置
  // int count = 0;
  uint32_t flash_address = 0x00;
  bool flash_ok = false; // flash書き込み開始・終了に用いる
  // char parsed[100] = {}; // チェックサムの計算のためNMEAフォーマットの$と*を除外
} GPS; // 取得したGPSデータを扱う構造体

typedef struct
{
  char data[10] = {}; // UTC時間なので後で+9時間する
  int index = 0;      // gpggaのセンテンスを時間のところだけパースするために使う変数
  int hour = 0;
  int min = 0;
  int sec = 0;
} TIME; // gnggaで受け取れる時間についての構造体

typedef struct
{
  bool status = false; // 機体が離床した・頂点に来たなどを判別する変数
  int count = 0;       // 冗長性のため
  int hour = 0;
  int min = 0;
  int sec = 0;
} ST; // 機体が離床した・頂点に来たなどを判別する構造体

// int test_count = 0; // 通信試験用
PITOT pitot;
GPS gps;
TIME time;
ST liftoff;
ST top;
CAN_CREATE CAN(true);
SPICREATE::SPICreate SPIC1; // platformio.iniで@4.2.0に固定しないと動かない
Flash flash1;               // platformio.iniで@4.2.0に固定しないと動かない

void setup()
{
  Serial1.begin(115200, SERIAL_8N1, TWELITE_RX_back, TWELITE_TX_back);   // 本部 18ch
  Serial2.begin(115200, SERIAL_8N1, TWELITE_RX_front, TWELITE_TX_front); // 上部基板 26ch
  Serial.begin(9600, SERIAL_8N1, GPS_RXD_TX, GPS_TXD_RX);
  // Serial.begin(115200); //通信試験用
  // while (!Serial);

  Serial1.println("COMBOARD");

  SPIC1.begin(VSPI, SCK, MISO, MOSI);
  flash1.begin(&SPIC1, FLASH_CS, SPI_FREQUENCY);
  pinMode(FLASH_CS, OUTPUT);
  digitalWrite(FLASH_CS, HIGH);

  pinMode(LED, OUTPUT); /* 通電確認用のLED */
  digitalWrite(LED, HIGH);

  pinMode(GPS_SW, OUTPUT);
  digitalWrite(GPS_SW, LOW);

  // start the CAN bus at 100 kbps
  if (CAN.begin(100E03, CAN_RX, CAN_TX, 10)) // 新しいcanライブラリ
  {
    Serial1.println("Starting CAN failed !!!");
    while (1)
      ;
  }

  // switch (CAN.test()) //CANデバッグ用
  // {
  // case CAN_SUCCESS:
  //   Serial1.println("Success!!!");
  //   break;
  // case CAN_UNKNOWN_ERROR:
  //   Serial1.println("Unknown error occurred");
  //   break;
  // case CAN_NO_RESPONSE_ERROR:
  //   Serial1.println("No response error");
  //   break;
  // case CAN_CONTROLLER_ERROR:
  //   Serial1.println("CAN CONTROLLER ERROR");
  //   break;
  // default:
  //   break;
  // }
}

void loop()
{
  // Serial.println(test_count); //通信試験用
  // Serial1.println(test_count);
  // test_count++;
  // delay(100);
  // if(test_count > 100)
  // {
  //   test_count = 0;
  // }
  if (Serial1.available())
  {
    char cmd = Serial1.read();
    Serial1.print("Serial1");
    Serial1.print(" Sending packet ... ");
    Serial1.println(cmd);

    /* CAN send */
    if (cmd == 'g')
    {
      /* GPSロギング、計測開始 */
      digitalWrite(GPS_SW, HIGH);
      Serial1.println("GPS START !!!");
    }
    else if (cmd == 'q')
    {
      /* GPSロギング、計測終了 */
      digitalWrite(GPS_SW, LOW);
      Serial1.println("GPS STOP !!!");
      /*flash書き込み終了*/
      gps.flash_ok = false; // flashの書き込み終了
      pitot.flash_ok = false;
    }
    else if (cmd == 'e')
    {
      /* メモリデータ消去 */
      flash1.erase();
      Serial1.println("ERASED !!!");
      gps.flash_address = 0x00;
      pitot.flash_address = 0x1000000;
    }
    else if (cmd == 's')
    {
      /*シーケンス開始(flashの書き込み開始)*/
      gps.flash_ok = true; // flashの書き込み開始
      pitot.flash_ok = true;
    }
    else
    {
      if (CAN.sendChar(cmd))
      {
        Serial1.println("failed to send CAN data");
      }
    }
  }

  if (CAN.available())
  {
    can_return_t Data;
    if (CAN.readWithDetail(&Data))
    {
      Serial.println("failed to get CAN data");
    }
    else
    {
      switch (Data.size) // CANで受け取る文字数で場合分け
      {
      case 1:
        switch (Data.data[0])
        {
        case 'l': // liftoff
          liftoff.status = true;
          break;
        case 't': // 頂点検知・開傘
          top.status = true;
          break;
        default:
          Serial1.printf("Can received!!!: %c\n\r", Data.data[0]);
          break;
        }
        break;
      case 2:
        Serial1.printf("Can received!!!: ");
        for (int i = 0; i < 2; i++)
        {
          Serial1.printf("%c", Data.data[i]);
        }
        Serial1.printf("\r\n");
        break;
      case 3:
        Serial1.printf("Can received!!!: ");
        for (int i = 0; i < 3; i++)
        {
          Serial1.printf("%c", Data.data[i]);
        }
        Serial1.printf("\r\n");
        break;
      case 4:
        Serial1.printf("Can received!!!: ");
        for (int i = 0; i < 4; i++)
        {
          Serial1.printf("%c", Data.data[i]);
        }
        Serial1.printf("\r\n");
        break;
      case 5:
        Serial1.printf("Can received!!!: ");
        for (int i = 0; i < 5; i++)
        {
          Serial1.printf("%c", Data.data[i]);
        }
        Serial1.printf("\r\n");
        break;
      case 6:
        Serial1.printf("Can received!!!: ");
        for (int i = 0; i < 6; i++)
        {
          Serial1.printf("%c", Data.data[i]);
        }
        Serial1.printf("\r\n");
        break;
      case 7:
        Serial1.printf("Can received!!!: ");
        for (int i = 0; i < 7; i++)
        {
          Serial1.printf("%c", Data.data[i]);
        }
        Serial1.printf("\r\n");
        break;
      case 8:
        Serial1.printf("Can received!!!: ");
        for (int i = 0; i < 8; i++)
        {
          Serial1.printf("%c", Data.data[i]);
        }
        Serial1.printf("\r\n");
        break;
      default:
        Serial1.printf("Unexpected can data!\r\n");
      }
    }
  }

  if (Serial2.available())
  {
    char cmd_2 = Serial2.read();
    switch (cmd_2)
    {
    case 'q': // 上段のコマンドは無視
    case 'e': // 上段のコマンドは無視
    case 's': // 上段のコマンドは無視
    default:
      char pitot_read = cmd_2;
      pitot.data[pitot.index] = pitot_read;
      pitot.index++;
      if (pitot_read == '\n') // 終了
      {
        for (int i = 0; i < pitot.index; i++)
        {
          tx_pitot[i] = pitot.data[pitot.index];
        }
        if (pitot.flash_ok) // flashの書き込み開始
        {
          flash1.write(pitot.flash_address, tx_pitot);
          pitot.flash_address += 0x1000100;
        }
        pitot.index = 0;
      }
    }
  }

  if (liftoff.status)
  {
    Serial1.println();
    Serial1.println("LIFTOFF !!!");
    Serial1.printf("離床時刻: %d時%d分%d秒\r\n", liftoff.hour, liftoff.min, liftoff.sec);
    Serial2.printf("離床時刻: %d時%d分%d秒\r\n", liftoff.hour, liftoff.min, liftoff.sec);
    Serial1.println();
    Serial2.print('l');
    liftoff.count++;
    if (liftoff.count > 5) // 冗長性の確保
    {
      liftoff.status = false;
    }
  }

  if (top.status)
  {
    Serial1.println();
    Serial1.println("PARACHUTE OPENED !!!");
    Serial1.printf("開傘時刻: %d時%d分%d秒\r\n", top.hour, top.min, top.sec);
    Serial2.printf("開傘時刻: %d時%d分%d秒\r\n", top.hour, top.min, top.sec);
    Serial1.println();
    Serial2.print('t');
    top.count++;
    if (top.count > 5) // 冗長性の確保
    {
      elapsed_time = 3600 * (top.hour - liftoff.hour) + 60 * (top.min - liftoff.min) + top.sec - liftoff.sec;
      Serial1.printf("離床検知から開傘まで: %d秒\r\n", elapsed_time);
      top.status = false;
    }
  }

  if (Serial.available())
  {
    // GPS
    char gps_read = Serial.read();
    Serial1.write(gps_read);

    gps.data[gps.index] = gps_read;
    gps.index++;
    if (gps_read == 0x0A) // 終端文字、またはGPGGAの表示列が一個分終わったとき(nmeaフォーマットでgpggaの最大文字数は改行文字を含めて82)
    {
      Serial1.print("GPS: ");
      for (int i = 0; i < 100; i++)
      {
        Serial1.printf("%c", gps.data[i]);
        Serial2.printf("%c", gps.data[i]);
        if (i == gps.index)
        {
          Serial1.println();
          Serial2.println();
        }
      }
      // gpggaの時間に関する部分だけ保存
      while (gps.data[time.index] != ',')
      {
        time.index++;
      }
      time.index++;
      int tmp = 0;
      while (gps.data[time.index] != '.')
      {
        time.data[tmp] = gps.data[time.index];
        tmp++;
        time.index++;
      }
      time.hour = (int)time.data[0] * 10 + (int)time.data[1] + 9;
      time.min = (int)time.data[2] * 10 + (int)time.data[3];
      time.sec = (int)time.data[4] * 10 + (int)time.data[5];
      if (liftoff.status && liftoff.count == 0)
      {
        liftoff.hour = time.hour;
        liftoff.min = time.min;
        liftoff.sec = time.sec;
      }
      if (top.status && top.count == 0)
      {
        top.hour = time.hour;
        top.min = time.min;
        top.sec = time.sec;
      }

      gps.index = 0;
      // Serial.println("hoge hoge");//デバッグ用
      if (gps.flash_ok)
      {
        for (int i = 0; i < 100; i++)
        {
          tx_gps[i] = gps.data[i];
        }
        flash1.write(gps.flash_address, tx_gps);
        gps.flash_address += 0x100;
      }
      if (gps.flash_address >= 0x1000000)
      {
        gps.flash_ok = false;
      }

      // Serial.println("hoge");//デバッグ用
    }
  }
}
