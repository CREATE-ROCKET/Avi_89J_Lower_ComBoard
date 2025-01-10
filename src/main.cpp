#include <Arduino.h>
#include <CAN/CAN.h>

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
#define LED 13

CAN_CREATE CAN;

void setup()
{
  Serial1.begin(115200, SERIAL_8N1, TWELITE_RX_back, TWELITE_TX_back);   // 本部 18ch
  Serial2.begin(115200, SERIAL_8N1, TWELITE_RX_front, TWELITE_TX_front); // 上部基板
  Serial.begin(115200);
  // while (!Serial);

  Serial1.println("COMBOARD");

  CAN.setPins(CAN_RX, CAN_TX);
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);

  // start the CAN bus at 100 kbps
  if (!CAN.begin(100E3))
  {
    Serial1.println("Starting CAN failed!");
    while (1);
  }
}

void loop()
{
  if (Serial1.available())
  {
    char cmd = Serial1.read();
    Serial1.print("Serial1");
    Serial1.println(cmd);
    Serial.println(cmd);
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
  if (Serial2.available())
  {
    char cmd_2 = Serial2.read();
    Serial2.println(cmd_2);
    Serial2.print("Serial2");
    Serial.println(cmd_2);
    // Serial1.print("Sending packet ... ");

    /* CAN send */
    uint8_t ercd = CAN.sendPacket(0x13, cmd_2);
    switch (ercd)
    {
    case CAN_OK:
      Serial2.println("done");
      break;
    case ACK_ERROR:
      Serial2.println("ACK ERROR");
      break;
    case PAR_ERROR:
      Serial2.println("PAR ERROR");
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
    Serial2.print("CAN RECEIVED !! :");
    Serial2.print(CAN_cmd);
  }
}