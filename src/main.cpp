#include <Arduino.h>
#include <CANCREATE 1.0.0/CANCREATE.h>
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
#define LANDING_START_INDEX 44

#define MISO 15
#define MOSI 21
#define SCK 27
#define FLASH_CS 2
#define SPI_FREQUENCY 5000000

#define LED 13

char landing_point[100];
char gps_data[100] = {};
int gps_index = 0; // 1回のループでのGPGGA表示の一文字を保存する位置
uint32_t flash_address = 0x00;
uint8_t tx_gps[256] = {};
uint8_t tx_pitot[256] = {};
bool top = false;
bool liftoff = false;
char pitot_data[256] = {};
int pitot_index = 0;
int liftoff_count = 0;
int top_count = 0;

// int test_count = 0; // 通信試験用
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
    Serial1.println("Starting CAN failed!");
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
  // Serial.println(test_count);
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
    if (cmd == 'h')
    {
      /* GPSロギング、計測開始 */
      digitalWrite(GPS_SW, HIGH);
      Serial1.println("GPS START!");
    }
    else if (cmd == 'g')
    {
      /* GPSロギング、計測終了 */
      digitalWrite(GPS_SW, LOW);
      Serial1.println("GPS STOP!");
    }
    else if (cmd == 'f')
    {
      /* メモリデータ消去 */
      // flash1.erase();
      // Serial1.println("ERASED!");
      // flash_address = 0x00;
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
        liftoff = true;
      }
      if (can_data[0] == 't') // 頂点検知
      {
        top = true;
      }
    }
  }

  if (Serial2.available())
  {
    pitot_data[pitot_index] = Serial2.read();
    pitot_index++;
    // if (pitot_index >)
    // {
    //   for (int i = 0; i <; i++)
    //   {
    //     tx_pitot_data[i]
    //   }
    // }
  }

  if (liftoff)
  {
    Serial1.println("LIFTOFF");
    Serial2.println("LIFTOFF");
    liftoff_count++;
    if (liftoff_count > 5)
    {
      liftoff = false;
    }
  }

  if (top)
  {
    Serial1.println("kaisan");
    Serial2.println("kaisan");
    top_count++;
    if (top_count > 5)
    {
      top = false;
    }
  }

  if (Serial.available())
  {
    // GPS
    char gps_read = Serial.read();
    Serial1.write(gps_read);

    gps_data[gps_index] = gps_read;
    gps_index++;

    if (gps_read == 0x0A || gps_index > 80) // 終端文字、またはGPGGAの表示列が一個分終わったとき
    {
      Serial1.print("GPS: ");
      for (int i = 0; i < 80; i++)
      {
        Serial1.printf("%c", gps_data[i]);
      }
      // Serial1.println(gps_data);
      //  for (int i = 0; i < 80; i++)
      //  {
      //    tx_gps[i] = gps_data[i];
      //  }
      //  flash1.write(flash_address, tx_gps);
      //  flash_address += 0x100;
      gps_index = 0;
    }

    // if (landing)
    // {
    //   Serial1.print("LANDINGPOINT: ");
    //   Serial1.println(landing_point);
    //   delay(1000);
    // }
  }
}