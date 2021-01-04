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
#include <Adafruit_MCP4728.h>
#include <Wire.h>
#include <Defs.h>
#include "AudioFileSourcePROGMEM.h"
#include "AudioGeneratorWAV.h"
#include "AudioOutputI2SNoDAC.h"
#include "vfs_api.h"
#include "WiFi.h"
#include "SD.h"

// VIOLA sample taken from https://ccrma.stanford.edu/~jos/pasp/Sound_Examples.html
#include "viola.h"

AudioGeneratorWAV *wav;
AudioFileSourcePROGMEM *file;
AudioOutputI2S *out;

#define SQUARE_GATE_PIN 2     // HIGH-LOW
#define SUB_SEQ_PIN 4         // HIGH-LOW
#define ANALOG_DIGITAL_PIN 13 // HIGH-LOW
#define GATE_PIN 27
#define FREQUENCY_PIN 19

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

Adafruit_MCP4728 mcp;

// Define the MCP4822 instance, giving it the SS (Slave Select) pin
// The constructor will also initialize the SPI library
// We can also define a MCP4812 or MCP4802
//MCP4822 dac(4);

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
unsigned long tFreq = 1;
float frequency;
float T;
float trustFactor = 0.7;
float prevFrequency;
uint8_t stepIndex = 0;
// play/stop
bool play = false;
bool gate = false;

// frequency detection
bool squarePositive = false;
bool squareAux = false;

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

    case OP_Route:
      Serial.println("llego un OP Route !!!");
      switch (rxValue[1])
      {
      case Square:
        Serial.println("Square");
        digitalWrite(SQUARE_GATE_PIN, HIGH);
        break;
      case Gate:
        Serial.println("Gate");
        digitalWrite(SQUARE_GATE_PIN, LOW);
        break;
      case Sub:
        Serial.println("Sub");
        digitalWrite(SUB_SEQ_PIN, HIGH);
        break;
      case Seq:
        Serial.println("Seq");
        digitalWrite(SUB_SEQ_PIN, LOW);
        break;
      case A_out:
        Serial.println("Sine");
        digitalWrite(ANALOG_DIGITAL_PIN, HIGH);
        break;
      case D_out:
        Serial.println("Digital out");
        digitalWrite(ANALOG_DIGITAL_PIN, LOW);
        break;

      default:
        break;
      }
      break;

    default:
      break;
    }
  }
};

void playNote(int voltage)
{
  gate = true;
  digitalWrite(GATE_PIN, HIGH);
  if (voltage <= 4000)
  {
    mcp.setChannelValue(MCP4728_CHANNEL_C, voltage, MCP4728_VREF_INTERNAL, MCP4728_GAIN_2X);
    //dac.setVoltageB(0);
    // dac.setVoltageA(voltage);
  }
  else
  {
    voltage = voltage - 4000;
    mcp.setChannelValue(MCP4728_CHANNEL_D, voltage, MCP4728_VREF_INTERNAL, MCP4728_GAIN_2X);
    //dac.setVoltageA(4000);
    //dac.setVoltageB(voltage);
  }
  //dac.updateDAC();
}

void setup()
{
  file = new AudioFileSourcePROGMEM(viola, sizeof(viola));
  wav = new AudioGeneratorWAV();
  out = new AudioOutputI2S();
  out->SetGain(1);
  out->SetPinout(33, 25, 32);
  out->SetChannels(0);
  wav->begin(file, out);

  // We call the init() method to initialize the instance

  //dac.init();

  // The channels are turned off at startup so we need to turn the channel we need on
  //dac.turnOnChannelA();
  //dac.turnOnChannelB();

  // We configure the channels in High gain
  // It is also the default value so it is not really needed
  // dac.setGainA(MCP4822::High);
  //dac.setGainB(MCP4822::High);

  pinMode(34, INPUT); // Borrar cuando se arregle el layout
  pinMode(SQUARE_GATE_PIN, OUTPUT);
  pinMode(SUB_SEQ_PIN, OUTPUT);
  pinMode(ANALOG_DIGITAL_PIN, OUTPUT);
  pinMode(GATE_PIN, OUTPUT);
  digitalWrite(SQUARE_GATE_PIN, HIGH);
  digitalWrite(SUB_SEQ_PIN, HIGH);
  digitalWrite(ANALOG_DIGITAL_PIN, LOW);

  pinMode(FREQUENCY_PIN, INPUT);
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

  // --------------- MCP4728 --------------------
  Serial.println("Adafruit MCP4728 test!");

  // Try to initialize!
  if (!mcp.begin())
  {
    Serial.println("Failed to find MCP4728 chip");
    while (1)
    {
      delay(10);
    }
  }
  Serial.println("MCP4728 Found!");
  mcp.setChannelValue(MCP4728_CHANNEL_A, 0, MCP4728_VREF_INTERNAL, MCP4728_GAIN_2X);
  mcp.setChannelValue(MCP4728_CHANNEL_B, 0, MCP4728_VREF_INTERNAL, MCP4728_GAIN_2X);
  mcp.setChannelValue(MCP4728_CHANNEL_C, 0, MCP4728_VREF_INTERNAL, MCP4728_GAIN_2X);
  mcp.setChannelValue(MCP4728_CHANNEL_D, 0, MCP4728_VREF_INTERNAL, MCP4728_GAIN_2X);
  mcp.saveToEEPROM();
  // --------------- MCP4728 --------------------
}

void loop()
{
  if (wav->isRunning())
  {
    if (!wav->loop())
      wav->stop();
  }
  else
  {
    Serial.printf("WAV done\n");
    delay(1000);
  }
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

  squarePositive = digitalRead(FREQUENCY_PIN);
  if (squarePositive && !squareAux)
  {
    T = float(micros() - tFreq) / 1000000;
    frequency = 1 / T;
    // FILTRO
    frequency = prevFrequency * (1 - trustFactor) + frequency * trustFactor;
    prevFrequency = frequency;
    // FILTRO
    tFreq = micros();
    squareAux = true;
  }
  if (!squarePositive && squareAux)
  {
    squareAux = false;
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
    //Serial.println(frequency);
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