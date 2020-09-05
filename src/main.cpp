/*
    Video: https://www.youtube.com/watch?v=oCMOYS71NIU
    Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleNotify.cpp
    Ported to Arduino ESP32 by Evandro Copercini

   Create a BLE server that, once we receive a connection, will send periodic notifications.
   The service advertises itself as: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
   Has a characteristic of: 6E400002-B5A3-F393-E0A9-E50E24DCCA9E - used for receiving data with "WRITE" 
   Has a characteristic of: 6E400003-B5A3-F393-E0A9-E50E24DCCA9E - used to send data with  "NOTIFY"

   The design of creating the BLE server is:
   1. Create a BLE Server
   2. Create a BLE Service
   3. Create a BLE Characteristic on the Service
   4. Create a BLE Descriptor on the characteristic
   5. Start the service.
   6. Start advertising.

   In this example rxValue is the data received (only accessible inside that function).
   And txValue is the data to be sent, in this example just a byte incremented every second. 
*/

#include <Arduino.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <MCP48xx.h>

#define LED_PIN 2

BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic;
bool deviceConnected = false;
bool oldDeviceConnected = false;
uint8_t txValue = 0;
bool led_on = false;
bool latencyMode = true;
unsigned long startTime = 0;
unsigned long currentTime = 0;
uint32_t latency = 0;

// Define the MCP4822 instance, giving it the SS (Slave Select) pin
// The constructor will also initialize the SPI library
// We can also define a MCP4812 or MCP4802
MCP4822 dac(4);

// We define an int variable to store the voltage in mV so 100mV = 0.1V
int voltage = 100;

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// mV table definition
#define NOTE_C1 100
#define NOTE_CS1 183
#define NOTE_D1 267
#define NOTE_DS1 350
#define NOTE_E1 433
#define NOTE_F1 517
#define NOTE_FS1 600
#define NOTE_G1 683
#define NOTE_GS1 767
#define NOTE_A1 850
#define NOTE_AS1 933
#define NOTE_B1 1017
#define NOTE_C2 1100
#define NOTE_CS2 1183
#define NOTE_D2 1267
#define NOTE_DS2 1350
#define NOTE_E2 1433
#define NOTE_F2 1517
#define NOTE_FS2 1600
#define NOTE_G2 1683
#define NOTE_GS2 1767
#define NOTE_A2 1850
#define NOTE_AS2 1933
#define NOTE_B2 2017
#define NOTE_C3 2100
#define NOTE_CS3 2183
#define NOTE_D3 2267
#define NOTE_DS3 2350
#define NOTE_E3 2433
#define NOTE_F3 2517
#define NOTE_FS3 2600
#define NOTE_G3 2683
#define NOTE_GS3 2767
#define NOTE_A3 2850
#define NOTE_AS3 2933
#define NOTE_B3 3017
#define NOTE_C4 3100
#define NOTE_CS4 3183
#define NOTE_D4 3267
#define NOTE_DS4 3350
#define NOTE_E4 3433
#define NOTE_F4 3517
#define NOTE_FS4 3600
#define NOTE_G4 3683
#define NOTE_GS4 3767
#define NOTE_A4 3850
#define NOTE_AS4 3933
#define NOTE_B4 4017
#define NOTE_C5 4100

#define MAX_STEPS 16

// Variables used to calculate tempo
// set BPM
int bpm = 100;
int *pBpm = &bpm;
// set Subdivision 1=quarter note; 0.5 ->eight note, ....
float subdivision = 1;
int interval;
int *pInterval = &interval;

int sequence[] = {
    NOTE_C2,
    NOTE_D2,
    NOTE_E2,
    NOTE_F2,
    NOTE_G2,
    NOTE_A2,
    NOTE_B2,
    NOTE_C3,
    NOTE_C2,
    NOTE_G2,
    NOTE_C3,
    NOTE_G2,
    NOTE_C2,
    NOTE_C2,
    NOTE_B2,
    NOTE_C3};

class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    deviceConnected = true;
  };

  void onDisconnect(BLEServer *pServer)
  {
    deviceConnected = false;
  }
};

class MyCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristic)
  {
    std::string rxValue = pCharacteristic->getValue();

    if (rxValue.length() > 0)
    {
      Serial.println("*********");
      Serial.print("Received Value: ");
      for (int i = 0; i < rxValue.length(); i++)
      {
        Serial.print(rxValue[i]);
        /* txValue = rxValue[i];
        pTxCharacteristic->setValue(&txValue, 1);
        pTxCharacteristic->notify(); */
        if (rxValue[i] == 'A' && latencyMode)
        {
          if (led_on)
          {
            digitalWrite(LED_PIN, LOW);
            led_on = false;
            currentTime = millis();
            latency = currentTime - startTime;
            Serial.println("");
            Serial.print("latency: ");
            Serial.print(latency >> 2);
            //pTxCharacteristic->setValue((uint8_t *)&latency, 4);
            //pTxCharacteristic->notify();
          }
          else
          {
            digitalWrite(LED_PIN, HIGH);
            led_on = true;
            startTime = millis();
            txValue = rxValue[i];
            pTxCharacteristic->setValue(&txValue, 1);
            pTxCharacteristic->notify();
          }
        }

        if (rxValue[i] == 'T')
        {
          if (led_on)
          {
            (*pBpm) = 100;
            led_on = false;
            Serial.println("cambio bpm a 100");
          }
          else
          {
            (*pBpm) = 180;
            led_on = true;
            Serial.println("cambio bpm a 180");
          }
        }
      }

      Serial.println();
      Serial.println("*********");
    }
  }
};

void setup()
{
  // We call the init() method to initialize the instance

  dac.init();

  // The channels are turned off at startup so we need to turn the channel we need on
  dac.turnOnChannelA();
  //dac.turnOnChannelB();

  // We configure the channels in High gain
  // It is also the default value so it is not really needed
  dac.setGainA(MCP4822::High);
  //dac.setGainB(MCP4822::High);

  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);

  //sequencer

  // Create the BLE Device
  BLEDevice::init("UART Service");

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pTxCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID_TX,
      BLECharacteristic::PROPERTY_NOTIFY);

  pTxCharacteristic->addDescriptor(new BLE2902());

  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID_RX,
      BLECharacteristic::PROPERTY_WRITE);

  pRxCharacteristic->setCallbacks(new MyCallbacks());

  // Start the service
  pService->start();

  // Start advertising
  pServer->getAdvertising()->start();
  Serial.println("Waiting a client connection to notify...");
}

void loop()
{

  /*  if (deviceConnected)
  {
    pTxCharacteristic->setValue(&txValue, 1);
    pTxCharacteristic->notify();
    txValue++;
    delay(1000); // bluetooth stack will go into congestion, if too many packets are sent
  } */

  // disconnecting
  if (!deviceConnected && oldDeviceConnected)
  {
    delay(500);                  // give the bluetooth stack the chance to get things ready
    pServer->startAdvertising(); // restart advertising
    Serial.println("start advertising");
    oldDeviceConnected = deviceConnected;
  }
  // connecting
  if (deviceConnected && !oldDeviceConnected)
  {
    // do stuff here on connecting
    oldDeviceConnected = deviceConnected;
  }

  /*   // We set channel A to output 500mV
  dac.setVoltageA(voltage);

  // We send the command to the MCP4822
  // This is needed every time we make any change
  dac.updateDAC();

  if (voltage > 2000)
  {
    voltage = 100;
  }

  voltage = voltage + 100;

  delay(5); */

  for (size_t i = 0; i < 16; i++)
  {
    interval = 60000 / (subdivision * bpm);
    dac.setVoltageA(sequence[i]);
    dac.updateDAC();
    delay((*pInterval));
  }
}