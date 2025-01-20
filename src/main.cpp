#include <Arduino.h>
#include <CAN/CAN.h>
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

char landing_point[80];
char gps_data[80];
int gps_index = 0; //1回のループでのGPGGA表示の一文字を保存する位置
uint32_t flash_address = 0x00;
uint8_t tx[256];
bool landing = false;
bool liftoff = false;

CAN_CREATE CAN;
SPICREATE::SPICreate SPIC1;
Flash flash1;

void setup()
{
  Serial1.begin(115200, SERIAL_8N1, TWELITE_RX_back, TWELITE_TX_back);   // 本部 18ch
  Serial2.begin(115200, SERIAL_8N1, TWELITE_RX_front, TWELITE_TX_front); // 上部基板 26ch
  Serial.begin(9600, SERIAL_8N1, GPS_RXD_TX, GPS_TXD_RX);
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

  CAN.setPins(CAN_RX, CAN_TX);
  // start the CAN bus at 100 kbps
  if (!CAN.begin(100E3))
  {
    Serial1.println("Starting CAN failed!");
    while (1)
      ;
  }
}

void loop()
{
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
      flash1.erase();
      Serial1.println("ERASED!");
      flash_address = 0x00;
    }
    else
    {
      uint8_t ercd = CAN.sendPacket(0x13, cmd);
      switch (ercd)
      {
      case CAN_OK:
        Serial1.println("done");
        break;
      case ACK_ERROR:
        Serial1.println("ACK ERROR");
        break;
      case PAR_ERROR:
        Serial1.println("PAR ERROR");
        break;
      default:
        break;
      }
    }
  }

  if (CAN.available())
  {
    char can_read = (char)CAN.read();
    Serial1.print("CAN RECEIVED !! :");
    Serial1.print(can_read);
  }

  if (Serial.available())
  {
    // GPS
    char gps_read = Serial.read();
    Serial1.write(gps_read);

    gps_data[gps_index] = gps_read;
    gps_index++;

    if (liftoff)
    {
      Serial1.println("LIFTOFF");
      Serial2.println("LIFTOFF");
      liftoff = false;
    }

    if (gps_read == 0x0A || gps_index > 80) // 終端文字、またはGPGGAの表示列が一個分終わったとき
    {

      Serial1.print("GPS: ");
      Serial1.println(gps_data);
      Serial2.print("HONTAI_GPS: ");
      Serial2.println(gps_data);

      for (int i = 0; i < 80; i++)
      {
        gps_data[i] = 0x00;
      }
    }

    if (landing)
    {
      Serial1.print("LANDINGPOINT: ");
      Serial1.println(landing_point);
      Serial2.print("LANDINGPOINT: ");
      Serial2.println(landing_point);
      delay(1000);
    }
  }
}