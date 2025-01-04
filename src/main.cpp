#include <Arduino.h>
#include <CAN/CAN.h>

#define CAN_RX 19
#define CAN_TX 18
#define TWELITE_RX_front 16
#define TWELITE_TX_front 17
#define TWELITE_RX_back 26
#define TWELITE_TX_back 25
#define GPS_RXD_TX 14
#define GPS_TXD_RX 4
#define GPS_SW 32
#define MISO 15
#define MOSI 21
#define SCK 27
#define FLASH_CS 2

CAN_CREATE CAN;

void setup()
{
  Serial1.begin(115200, SERIAL_8N1, TWELITE_RX_back, TWELITE_TX_back);
  Serial2.begin(115200, SERIAL_8N1, TWELITE_RX_front, TWELITE_TX_front);
  Serial.begin(115200);
  // while (!Serial);

  Serial.println("CAN Receiver");
  CAN.setPins(CAN_RX, CAN_TX);

  // start the CAN bus at 100 kbps
  if (!CAN.begin(100E3))
  {
    Serial.println("Starting CAN failed!");
    while (1)
      ;
  }
}

void loop()
{

  if (CAN.available())
  {
    char CAN_cmd = (char)CAN.read();
    Serial.print(CAN_cmd);
    CAN.sendPacket(0x13, CAN_cmd); // 受信確認用
  }
}