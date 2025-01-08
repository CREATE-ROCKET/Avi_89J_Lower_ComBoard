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
  Serial1.begin(115200, SERIAL_8N1, TWELITE_RX_back, TWELITE_TX_back);   // 本部
  Serial2.begin(115200, SERIAL_8N1, TWELITE_RX_front, TWELITE_TX_front); // 上部基板
  Serial.begin(9600);
  // while (!Serial);

  Serial1.println("COMBOARD");

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
    char cmd = Serial.read();
    Serial1.println(cmd);
    // Serial1.print("Sending packet ... ");

    /* CAN send */
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

  if (CAN.available())
  {
    char CAN_cmd = (char)CAN.read();
    Serial1.print("CAN RECEIVED !! :");
    Serial1.print(CAN_cmd);
  }
}