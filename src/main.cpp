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
bool top = false;
bool liftoff = false;
int liftoff_count = 0; // 冗長性のため
int top_count = 0;     // 冗長性のため

typedef struct
{
  char data[100] = {};
  int index = 0;
  int count = 0;
  uint32_t flash_address = 0x1000000; // 128Mbitの半分　最大:0x2000000
  bool flash_ok = false;
} PITOT;

typedef struct
{
  char data[100] = {};
  int index = 0; // 1回のループでのGPGGA表示の一文字を保存する位置
  int count = 0;
  uint32_t flash_address = 0x00;
  char parsed[100] = {}; // チェックサムの計算のためNMEAフォーマットの$と*を除外
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

    if (cmd == 'g')
    {
      /* GPSロギング、計測開始 */
      digitalWrite(GPS_SW, HIGH);
      Serial1.println("GPS START !!!");
    }
    else if (cmd == 'Q')
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
    else if (cmd == 'S')
    {
      /*シーケンス開始(flashの書き込み開始)*/
      Serial1.printf("FLASH START!!!\r\n");
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
      switch (Data.size)
      {
      case 1:
        switch (isdigit(Data.data[0]))
        {
        case 0: // 文字のとき0
          if (Data.data[0] == 'l')
          {
            liftoff = true;
          }
          else if (Data.data[0] == 't')
          {
            top = true;
          }
          else
          {
            Serial1.printf("Can received!!!: %c\n\r", Data.data[0]);
          }
          break;
        default: // 数字のとき0以外の適当な値
          // int *tmp = reinterpret_cast<int *>(Data.data[0]);
          // Serial1.printf("%d\r\n", *tmp);
          Serial1.printf("%d\r\n", Data.data[0]);
          break;
        }
        break;
      case 2:
        Serial1.printf("Can received!!!: ");
        // if (*(reinterpret_cast<int *>(Data.data[0])) == 189) // lpsのWhoAMI値
        // {
        // }
        // // ICM
        if (Data.data[0])
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
      {
        Serial1.printf("Can received!!!: ");
        int *tmp = reinterpret_cast<int *>(Data.data);
        Serial1.printf("%d\r\n", *tmp);
      }
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
        Serial1.printf("Unexpected Can data!!!\r\n");
      }
    }
  }

  if (Serial2.available())
  {

    char pitot_read = Serial2.read();
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
        pitot.flash_address += 0x100;
      }
      pitot.index = 0;
    }
    if (pitot.flash_ok) // flashの書き込み開始
    {
      flash1.write(pitot.flash_address, tx_pitot);
      pitot.flash_address += 0x100;
    }
    if (pitot.flash_address == 0x2000000)
    {
      pitot.flash_ok = false;
    }
  }

  if (liftoff)
  {
    Serial1.println("LIFTOFF !!!");
    Serial2.println('l');
    liftoff_count++;
    if (liftoff_count > 5) // 冗長性の確保
    {
      liftoff = false;
    }
  }

  if (top)
  {
    Serial1.println("PARACHUTE OPENED !!!");
    Serial2.println('t');
    top_count++;
    if (top_count > 5) // 冗長性の確保
    {
      top = false;
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

      // Serial.println("hoge");//デバッグ用
      // }
    }
  }
}