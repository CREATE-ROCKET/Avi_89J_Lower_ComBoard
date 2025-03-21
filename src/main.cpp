#include <Arduino.h>
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
uint8_t rx[256]{};
uint32_t read_flash_address = 0;
bool top = false;
bool liftoff = false;
// int liftoff_count = 5; // 冗長性のため
int top_count = 0; // 冗長性のため
int last_liftoff_time = 0;
// int last_top_time = 0;

typedef struct
{
  char data[100] = {}; // 100は80以上の適当な数字
  int index = 0;
  int count = 0;
  uint32_t flash_address = 0x1800000; // 128Mbitの半分　最大:0x2000000 (= 16*6)
  bool flash_ok = false;
} PITOT;

typedef struct
{
  char data[100] = {}; // 100は80以上の適当な数字
  int index = 0;       // 1回のループでのGPGGA表示の一文字を保存する位置
  int count = 0;
  uint32_t flash_address = 0x00;
  bool flash_ok = false;
} GPS;

// int test_count = 0; // 通信試験用
PITOT pitot;
GPS gps;
CAN_CREATE CAN(true);
SPICREATE::SPICreate SPIC1;
Flash flash1;

void setup()
{
  Serial1.begin(115200, SERIAL_8N1, TWELITE_RX_back, TWELITE_TX_back); // 本部 26ch
  // Serial2.begin(115200, SERIAL_8N1, TWELITE_RX_front, TWELITE_TX_front); // 上部基板 18ch
  Serial.begin(9600, SERIAL_8N1, GPS_RXD_TX, GPS_TXD_RX);
  // Serial.begin(115200); //通信試験用

  Serial1.println("COMBOARD");
  // Serial2.println("COMBOARD");

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
    Serial1.println("Starting CAN failed");
    while (1)
      ;
  }

  switch (CAN.test()) // CANデバッグ用
  {
  case CAN_SUCCESS:
    Serial1.println("Success!!!");
    break;
  case CAN_UNKNOWN_ERROR:
    Serial1.println("Unknown error occurred");
    break;
  case CAN_NO_RESPONSE_ERROR:
    Serial1.println("No response error");
    break;
  case CAN_CONTROLLER_ERROR:
    Serial1.println("CAN CONTROLLER ERROR");
    break;
  default:
    break;
  }
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

    if (cmd == 'g') // sequenceより前にgpsを開始しないと衛星探す時間がとれない
    {
      /* GPS計測開始 */
      digitalWrite(GPS_SW, HIGH);
      Serial1.println("GPS START !!!");
    }
    else if (cmd == 'h')
    {
      /* GPSロギング、計測終了 */
      digitalWrite(GPS_SW, LOW);
      Serial1.println("GPS STOP");
    }
    else if (cmd == 'Q') // sequence 停止
    {
      Serial1.println("SEQUENCE END !!!");
      /*flash書き込み終了*/
      gps.flash_ok = false; // flashの書き込み終了
      pitot.flash_ok = false;
      liftoff = false;
      top = false;
      if (CAN.sendChar('Q')) // log基板に
      {
        Serial1.println("failed to send CAN data");
      }
    }
    else if (cmd == 'e') // flash erase
    {
      /* メモリデータ消去 */
      Serial1.printf("FLASH ERASE START!!!");
      flash1.erase();
      Serial1.println("ERASED !!!");
      gps.flash_address = 0x00;
      pitot.flash_address = 0x1800000;
    }
    else if (cmd == 'S')
    {
      /*シーケンス開始(flashの書き込み開始)*/
      gps.flash_ok = true; // flashの書き込み開始
      pitot.flash_ok = true;
      if (CAN.sendChar('S'))
      {
        Serial1.println("failed to send CAN data");
      }
      digitalWrite(GPS_SW, HIGH);
      Serial1.printf("SEQUENCE START !!!\r\n");
    }
    else if (cmd == 'C') // CAN test用
    {
      switch (CAN.test())
      {
      case CAN_SUCCESS:
        Serial1.println("CAN Success!!!");
        break;
      case CAN_UNKNOWN_ERROR:
        Serial1.println("CAN Unknown error occurred");
        break;
      case CAN_NO_RESPONSE_ERROR:
        Serial1.println("CAN No response error");
        break;
      case CAN_CONTROLLER_ERROR:
        Serial1.println("CAN CONTROLLER ERROR");
      }
    }
    else if (cmd == 'r') // flashよみとり(射場で見る用)
    {
      for (int j = 0; j < 3; j++) // 0x2000000 ÷ 256 = 65536
      {
        read_flash_address = 256 * j;
        flash1.read(read_flash_address, rx);
        for (int i = 0; i < 256; i++)
        {
          Serial1.printf("%d", rx[i]);
        }
      }
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
      Serial1.println("failed to get CAN data");
    }
    else
    {
      switch (Data.size)
      {
      case 1:
        // Serial1.printf("Can received: ");
        switch (isdigit(Data.data[0]))
        {
        case 0:                    // 文字のとき0
          if (Data.data[0] == 'l') // 離床検知
          {
            liftoff = true;
          }
          else if (Data.data[0] == 't') // 頂点検知
          {
            top = true;
          }
          else
          {
            Serial1.printf("%c\n\r", Data.data[0]);
          }
          break;
        default:                                  // 数字のとき0以外の適当な値
          Serial1.printf("%d\r\n", Data.data[0]); // char型をint型にキャスト
          break;
        }
        break;
      case 2:
        // Serial1.printf("Can received: ");
        if (Data.data[0] == '$')
        {
          Serial1.printf("W%d, ", Data.data[1]); // lps,icmのWhoAmI値を表示
        }
        else
        {
          for (int i = 0; i < 2; i++)
          {
            Serial1.printf("%c", Data.data[i]);
          }
        }
        Serial1.printf("\r\n");
        break;
      case 4:
      {
        // Serial1.printf("Can received: ");
        if (isdigit(Data.data[0])) // 文字のとき0
        {
          Serial1.printf("%c");
        }
        else // 数字のとき
        {
          int *tmp = reinterpret_cast<int *>(Data.data);
          Serial1.printf("%d", *tmp);
        }
      }
      break;
      case 5:
        // Serial1.printf("Can received: ");
        if (Data.data[0] == 189) // lpsのWhoAmI値
        {
          Serial1.printf("L");
          int *lps_data = reinterpret_cast<int *>(Data.data + sizeof(uint8_t)); // reinterpret_castでData.dataのメモリから後ろ4つまでを強引にint型として読む。
          Serial1.printf("%d, ", *lps_data);
        }
        else if (Data.data[0] == 18) // ICMのWhoAmI
        {
          Serial1.printf("I");
          int *icm_data = reinterpret_cast<int *>(Data.data + sizeof(uint8_t));
          Serial1.printf("%d ,", *icm_data);
        }
        else
        {
          for (int i = 0; i < 5; i++)
          {
            Serial1.printf("%d,", Data.data[i]);
          }
        }
        break;
      default:
        Serial1.printf("Unexpected Can data\r\n");
      }
    }
  }

  // if (Serial2.available())
  // {

  //   char pitot_read = Serial2.read();
  //   Serial1.printf("FROM UPPER COM: %c", pitot_read);
  //   pitot.data[pitot.index] = pitot_read;
  //   pitot.index++;
  //   if (pitot_read == '\n') // 終了
  //   {
  //     Serial1.printf("\r\n");
  //     for (int i = 0; i < pitot.index; i++)
  //     {
  //       tx_pitot[i] = pitot.data[pitot.index];
  //     }
  //     if (pitot.flash_ok) // flashの書き込み開始
  //     {
  //       flash1.write(pitot.flash_address, tx_pitot);
  //       pitot.flash_address += 0x100;
  //     }
  //     pitot.index = 0;
  //   }
  //   if (pitot.flash_ok) // flashの書き込み開始
  //   {
  //     flash1.write(pitot.flash_address, tx_pitot);
  //     pitot.flash_address += 0x100;
  //   }
  //   if (pitot.flash_address == 0x2000000) // 配列外アクセスしないように
  //   {
  //     pitot.flash_ok = false;
  //   }
  // }

  if (liftoff)
  {
    if (millis() - last_liftoff_time > 100)
    {
      Serial1.println("LIFTOFF !!!!!!!!!!!");
      last_liftoff_time = millis();
    }
    // if (liftoff_count == 500)
    // {
    //   Serial1.println("LIFTOFF !!!!!!!!!!!");
    //   liftoff_count = 0;
    // }
    // liftoff_count++;
    // liftoff_count++;
    // if (liftoff_count > 5) // 冗長性の確保
    // {
    //   Serial2.print('l');
    //   liftoff = false;
    // }
  }

  if (top)
  {
    // if (millis() - last_top_time > 100)
    // {
    //   Serial1.println("PARACHUTE OPENED !!!!!!");
    //   last_top_time = millis();
    // }
    if (top_count < 3)
    {
      Serial1.println("PARACHUTE OPENED !!!!!!");
    }
    top_count++;
    // {
    //   Serial1.println("PARACHUTE OPENED !!!!!!!!!!!"); // 頂点検知したらGPS表示の色を黄色に変える　->ansiエスケープ
    // }
    // top_count++;
    // if (top_count >= 5) // 冗長性の確保
    // {
    //   for (int i = 0; i < 100; i++)
    //   {
    //     Serial1.printf("%c", gps.data[i]);
    //     if (i == gps.index)
    //     {
    //       Serial1.printf("\r\n");
    //     }
    //   }
    // }
  }

  if (Serial.available())
  {
    // GPS
    Serial1.printf("\e[0]");
    char gps_read = Serial.read();
    Serial1.write(gps_read);
    Serial2.write(gps_read);
    gps.data[gps.index] = gps_read;
    gps.index++;
    if (gps_read == 0x0A || gps.index >= 85) // 終端文字、またはGPGGAの表示列が一個分終わったとき(nmeaフォーマットでgpggaの最大文字数は改行文字を含めて82)
    {
      // for (int i = 0; i < 100; i++)
      // {
      //   Serial1.printf("%c", gps.data[i]);
      //   if (i == gps.index)
      //   {
      //     Serial1.println();
      //     Serial2.println();
      //   }
      // }
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
      if (gps.flash_address == 0x1800000)
      {
        gps.flash_ok = false;
      }

      // Serial.println("hoge");//デバッグ用
      // }
    }
  }
}