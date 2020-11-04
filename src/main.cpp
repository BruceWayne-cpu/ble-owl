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
#include <Defs.h>

#define LED_PIN 2
#define GATE_PIN 27

BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic;
bool deviceConnected = false;
bool oldDeviceConnected = false;
uint8_t txValue[3] = {};
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

// Variables used to calculate tempo
// set BPM
int bpm = 120;
int *pBpm = &bpm;
// set Subdivision 1=quarter note; 0.5 ->eight note, ....
float subdivision = 1;
int interval;
int *pInterval = &interval;
unsigned long tInterval;
unsigned long tGate;
int gatePercentage = 1; //percentage of interval
int gateInterval;
uint8_t stepIndex = 0;
// play/stop
bool play = false;
bool gate = false;

/* int sequence[] = {
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
    NOTE_C4,
    NOTE_B2,
    NOTE_C3}; */

/* int sequence[] = {
    100,
    100,
    100,
    100,
    1100,
    1100,
    1100,
    1100,
    2100,
    2100,
    2100,
    2100,
    3100,
    3100,
    3100,
    3100}; */

int sequence[] = {
    0,
    0,
    1000,
    1000,
    2000,
    2000,
    3000,
    3000,
    4000,
    4000,
    5000,
    5000,
    6000,
    6000,
    7000,
    7000};

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
    uint8_t *rxPrt = pCharacteristic->getData();
    uint8_t rxValue[MSG_LENGTH];
    for (size_t i = 0; i < MSG_LENGTH; i++)
    {
      rxValue[i] = *rxPrt;
      rxPrt++;
    }
    switch (rxValue[0])
    {
    case OP_Tempo:
      bpm = rxValue[1] + 1;
      break;

    case OP_PlayStop:
      Serial.println("llego un OP_PlayStop !!!");
      if (rxValue[1] == Play)
      {
        play = true;
      }
      else if (rxValue[1] == Pause)
      {
        play = false;
      }
      else
      {
        play = false;
        stepIndex = 0;
      }
      break;

    case OP_Note:
      sequence[rxValue[1]] = (rxValue[2] * 83);
      //sequence[rxValue[1]] = 17 + (rxValue[2] * 83); //notes come as multiple of 83mV, 17 offset for 100mV (17+83)minimum for DAC
      Serial.println("llego un OP Note !!!");
      break;

    default:
      break;
    }

    /* if (rxValue.length() > 0)
    {
      Serial.println("*********");
      Serial.print("Received Value: ");
      for (int i = 0; i < rxValue.length(); i++)
      {
        Serial.print(rxValue[i]);
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
    } */
  }
};

void playNote(int voltage)
{
  gate = true;
  digitalWrite(GATE_PIN, HIGH);
  if (voltage <= 4000)
  {
    //dac.shutdownChannelB();
    dac.setVoltageB(0);
    dac.setVoltageA(voltage);
  }
  else
  {
    //dac.turnOnChannelB();
    voltage = voltage - 4000;
    dac.setVoltageA(4000);
    dac.setVoltageB(voltage);
  }
  dac.updateDAC();
}

void setup()
{
  // We call the init() method to initialize the instance

  dac.init();

  // The channels are turned off at startup so we need to turn the channel we need on
  dac.turnOnChannelA();
  dac.turnOnChannelB();

  // We configure the channels in High gain
  // It is also the default value so it is not really needed
  dac.setGainA(MCP4822::High);
  dac.setGainB(MCP4822::High);

  pinMode(LED_PIN, OUTPUT);
  pinMode(GATE_PIN, OUTPUT);
  Serial.begin(115200);

  //sequencer
  tInterval = millis();
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
    // TODO usar timer envez de delay para no trancar la secuencia
    delay(500);                  // give the bluetooth stack the chance to get things ready
    pServer->startAdvertising(); // restart advertising
    Serial.println("start advertising");
    oldDeviceConnected = deviceConnected;
  }
  // connecting
  if (deviceConnected && !oldDeviceConnected)
  {
    // Init, send status of sequencer
    // do stuff here on connecting
    delay(3000); // HACER TIMER!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    txValue[0] = OP_Tempo;
    txValue[1] = bpm;
    pTxCharacteristic->setValue(txValue, 2);
    pTxCharacteristic->notify();
    oldDeviceConnected = deviceConnected;
  }

  interval = 60000 / (subdivision * bpm);
  gateInterval = interval * gatePercentage;

  if (play && gate && (millis() - tGate >= gateInterval))
  {
    tGate += gateInterval;
    digitalWrite(GATE_PIN, LOW);
  }
  if (millis() - tInterval >= interval)
  {
    /* Serial.print(xPortGetCoreID()); */
    tInterval += interval;
    if (play)
    {
      /* dac.setVoltageA(sequence[stepIndex]);
      dac.updateDAC(); */
      playNote(sequence[stepIndex]);
      txValue[0] = OP_Step;
      txValue[1] = stepIndex;
      pTxCharacteristic->setValue(txValue, 2);
      pTxCharacteristic->notify();
      stepIndex++;
      if (stepIndex >= 16)
        stepIndex = 0;
    }
  }
}