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
bool top = false;
bool liftoff = false;
int liftoff_count = 0; // 冗長性のため
int top_count = 0;     // 冗長性のため
// char gps_data[100] = {};
// char gps_parsed[100] = {};
// int checksum = 0;
// int correct_checksum;
// float gps_height = 0;
// int gps_index = 0; // 1回のループでのGPGGA表示の一文字を保存する位置
// uint32_t gps_flash_address = 0x00;
// uint32_t pitot_flash_address = 0x1000000; // 128Mbitの半分　0x2000000
// char pitot_data[100] = {};
// int pitot_index = 0;
// int pitot_count = 0;
// int gps_count = 0;
// int consume_count = 0;
// int gate_num = 0;
// int checksum_index = 0;
// int checksum_1 = 0;
// int checksum_2 = 0;
typedef struct
{
  char data[100] = {};
  int index = 0;
  int count = 0;
  uint32_t flash_address = 0x1000000; // 128Mbitの半分　0x2000000
} PITOT;

typedef struct
{
  char data[100] = {};
  int index = 0; // 1回のループでのGPGGA表示の一文字を保存する位置
  int count = 0;
  uint32_t flash_address = 0x00;
  char parsed[100] = {}; // チェックサムの計算のためNMEAフォーマットの$と*を除外
} GPS;

typedef struct
{
  int correct;
  int index = 0;
  int gps = 0;
  int tmp[2] = {};
} CHECKSUM;

// int test_count = 0; // 通信試験用
PITOT pitot;
GPS gps;
CHECKSUM checksum;
CAN_CREATE CAN(true);
SPICREATE::SPICreate SPIC1;
Flash flash1;

void setup()
{
  Serial1.begin(115200, SERIAL_8N1, TWELITE_RX_back, TWELITE_TX_back); // 本部 18ch
  //Serial2.begin(115200, SERIAL_8N1, TWELITE_RX_front, TWELITE_TX_front); // 上部基板 26ch
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
    }
    else if (cmd == 'e')
    {
      /* メモリデータ消去 */
      flash1.erase();
      Serial1.println("ERASED !!!");
      gps.flash_address = 0x00;
      pitot.flash_address = 0x1000000;
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

    char can_data[9]; // 最大8文字+改行文字が送信される
    if (CAN.readLine((char *)can_data))
    {
      // エラーの場合の処理
      Serial1.println("failed to get CAN data");
    }
    else
    {

      for (int i = 0; i < 3; i++)
      {
        Serial1.printf("Can received!!!: %hu\r\n", can_data[i]);
      }
      if (can_data[0] == 'l') // liftoff
      {
        liftoff = 1;
      }
      if (can_data[0] == 't') // 頂点検知・開傘
      {
        top = 1;
      }
    }
  }

  if (Serial2.available())
  {
    pitot.data[pitot.index] = Serial2.read();
    if (pitot.data[pitot.index] == '\n') // 終了
    {
      for (int i = 0; i < pitot.index; i++)
      {
        tx_pitot[i] = pitot.data[pitot.index];
      }
      flash1.write(pitot.flash_address, tx_pitot);
      pitot.flash_address += 0x1000100;
      pitot.index = -1;
    }
    pitot.index++;
  }

  if (liftoff)
  {
    // Serial1.printf("Height: %f ", gps_height);
    Serial1.println("LIFTOFF !!!");
    Serial2.println('l');
    // liftoff_count++;
    // if (liftoff_count > 5) // 冗長性の確保
    // {
    liftoff = 0;
    // }
  }

  if (top)
  {
    // Serial1.printf("Height: %f ", gps_height);
    Serial1.println("PARACHUTE OPENED !!!");
    Serial2.println('t');
    // top_count++;
    // if (top_count > 5) // 冗長性の確保
    // {
    top = 0;
    // }
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
      gps.index = 0;
      // checksum.index = 0;
      // while (gps.data[checksum.index] != '*') // チェックサムの計算のためNMEAフォーマットの$と*を除外
      // {
      //   if (gps.data[checksum.index] == '$')
      //   {
      //   }
      //   else
      //   {
      //     gps.parsed[checksum.index] = gps.data[checksum.index];
      //   }
      //   checksum.index++;
      // }
      // if ((u_int)gps.parsed[checksum.index + 1] > (int)'0')
      // {
      //   checksum.tmp[0] = (u_int)gps.parsed[checksum.index + 1] - (int)'A' + 10;
      // }
      // else
      // {
      //   checksum.tmp[0] = (u_int)gps.parsed[checksum.index + 1] - (int)'0';
      // }
      // if ((u_int)gps.parsed[checksum.index + 2] > (int)'0')
      // {
      //   checksum.tmp[1] = (u_int)gps.parsed[checksum.index + 2] - (int)'A' + 10;
      // }
      // else
      // {
      //   checksum.tmp[1] = (u_int)gps.parsed[checksum.index + 2] - (int)'0';
      // }
      // checksum.correct = 16 * checksum.tmp[0] + checksum.tmp[1]; // 16進数のチェックサムを10進数に変換
      // Serial1.printf("correct checksum: %d", checksum.correct);  // デバッグ用

      // for (int i = 0; i < (checksum.index + 6); i++) // チェックサムの計算　+6はnmea formatで*の後のチェックサム2桁+\r\n
      // {
      //   checksum.gps ^= (int)gps.parsed[i];
      // }
      // // gps_count = 0;
      // // consume_count = 0;
      // // gate_num = 0;
      // if (checksum.gps == checksum.correct)
      // {
      //   Serial1.printf("CHECKSUM OK!!!\r\n");
      // }

      //   for (int i = 0; i < (checksum_index + 6); i++)
      //   {
      //     if (gps_checksum[i] == ',')
      //     {
      //       gps_count++;
      //     }
      //     if (gps_count == 9 & gate_num == 0)
      //     {
      //       while (gps_checksum[i + consume_count] != '.')
      //       {
      //         consume_count++;
      //       }
      //       switch (consume_count)
      //       {
      //       case 2: // 1桁. ...
      //         gps_height += gps_checksum[i + 1];
      //         break;
      //       case 3: // 2桁. ...
      //         gps_height += 10 * gps_checksum[i + 1] + gps_checksum[i + 2];
      //         break;
      //       case 4: // 3桁. ...
      //         gps_height += 100 * gps_checksum[i + 1] + 10 * gps_checksum[i + 2] + gps_checksum[i + 3];
      //         break;
      //       case 5: // 4桁. ...
      //         gps_height += 1000 * gps_checksum[i + 1] + 100 * gps_checksum[i + 2] + 10 * gps_checksum[i + 3] + gps_checksum[i + 4];
      //         break;
      //       }
      //       while (gps_checksum[i + consume_count] != ',')
      //       {
      //         consume_count;
      //       }
      //       switch (consume_count)
      //       {
      //       case 6:
      //         gps_height += 0.1 * gps_checksum[i + 5];
      //       case 7:
      //         gps_height += 0.1 * gps_checksum[i + 6];
      //       }
      //       gate_num++;
      //     }
      //   }
      //   Serial1.printf("HEIGHT: ");
      //   Serial1.printf("%f\r\n", gps_height);
      // }
      // Serial.println("hoge hoge");//デバッグ用
      for (int i = 0; i < 80; i++)
      {
        tx_gps[i] = gps.data[i];
      }
      flash1.write(gps.flash_address, tx_gps);
      gps.flash_address += 0x100;

      // Serial.println("hoge");//デバッグ用
      // }
    }
  }
}