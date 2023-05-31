#include "BLEDevice.h"
#include "crc16.h"

/* Specify the Service UUID of Server 
static BLEUUID serviceUUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
/* Specify the Characteristic UUID of Server 
static BLEUUID    charUUID("beb5483e-36e1-4688-b7f5-ea07361b26a8");
*/

// The remote Bluetti service we wish to connect to.
static BLEUUID serviceUUID("0000ff00-0000-1000-8000-00805f9b34fb");

// The characteristics of Bluetti Devices
static BLEUUID    WRITE_UUID("0000ff02-0000-1000-8000-00805f9b34fb");
static BLEUUID    NOTIFY_UUID("0000ff01-0000-1000-8000-00805f9b34fb");


static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
//static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLERemoteCharacteristic* pRemoteWriteCharacteristic;
static BLERemoteCharacteristic* pRemoteNotifyCharacteristic;
static BLEAdvertisedDevice* myDevice;
static boolean CMDPending = false;

struct command_handle {
  uint8_t AddrHighByte;
  uint8_t AddrLowByte;
  int len;
};

typedef struct __attribute__ ((packed)) {
  uint8_t prefix;            // 1 byte  BT prefix
  uint8_t field_update_cmd;  // 1 byte, Modbus Function code  
  uint8_t AddrHighByte;              // 1 byte, Modbus High Byte Starting Address
  uint8_t AddrLowByte;            // 1 byte, Modbus Low Byte Starting Address
  uint16_t len;              // 2 bytes, Modbus Quantity of Registers
  uint16_t check_sum;        // 2 bytes  
} bt_command_t;   

typedef struct __attribute__ ((packed)) {
  uint8_t Request;            // 1 byte
  uint16_t idx;              // 2 bytes  
  uint16_t len;              // 2 bytes  
  uint8_t Buffer[512];        // 512 bytes  
} pageBuffer_t;

static pageBuffer_t pageBuffer;
static bt_command_t cmd;

/********************************************/
void printMsg(pageBuffer_t *pageBuffer){
  Serial.printf("Full Message:\n\r");
  Serial.printf("pageBuffer->idx: %d\n\r", pageBuffer->idx);
  Serial.printf("pageBuffer->len: %d\n\r", pageBuffer->len);
  for (int i=0; i<pageBuffer->idx; i++){
     
    Serial.printf("%02x", pageBuffer->Buffer[i]);
    
    if((i+1) % 2 == 0){
      Serial.print(" ");
    } 

    if((i+1) % 20 == 0){
      Serial.println();
    }
  }
  Serial.println();
}

/**************************************************************************************************/
int parse_bluetooth_data(uint8_t AddrHighByte, uint8_t AddrLowByte, uint8_t* pData, size_t length, pageBuffer_t *pageBuffer){
  uint8_t AddrHigh = AddrHighByte; 
  uint8_t AddrLow = AddrLowByte;

  Serial.printf("Parser: pData[1] = %02x\n", pData[1]);
  Serial.printf("Parser: pData[2] = %02x\n", pData[2]);
  Serial.printf("AddrHighByte: %02x\nAddrLowByte: %02x \n", 
                  AddrHighByte, AddrLowByte);

  if(pageBuffer->idx == 0){ //erstes packet
    if(pData[0] == 0x01){
      pageBuffer->Request = pData[1];
      pageBuffer->len = pData[2];
      Serial.printf("len: %d\n", pageBuffer->len); //Serial.flush();
    }else{
      return 0;
     }
  }
  for(int p=0; p<length; p++){
    pageBuffer->Buffer[pageBuffer->idx] = pData[p];
    pageBuffer->idx++;
  }

  Serial.printf("pageBuffer->idx: %d\n", pageBuffer->idx); //Serial.flush();
  Serial.printf("pageBuffer->len: %d\n", pageBuffer->len); //Serial.flush();

  if(pageBuffer->idx < pageBuffer->len+4){  //
    Serial.printf("Message not completed... left bytes: %d\n",  pageBuffer->len - pageBuffer->idx); //Serial.flush();
    //return (pageBuffer->len - pageBuffer->idx+4);
    return -1;
  }

  Serial.printf("Message completed!\n");
  printMsg(pageBuffer);
  CMDPending = false;
  pageBuffer->idx = 0;
  pageBuffer->len = 0;
  return 0;
}

/**************************************************************************************************/
static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic,
                            uint8_t* pData, size_t len, bool isNotify)
{
  Serial.printf("F01 - Write Response with ;length %d\n", len); //Serial.flush();
  /* pData Debug... */
  for (int i=1; i<=len; i++){
     
    Serial.printf("%02x", pData[i-1]);
    
    if(i % 2 == 0){
      Serial.print(" ");
    } 

    if(i % 20 == 0){
      Serial.println();
    }
  }
  Serial.println();
  
  if(CMDPending){
    if(!parse_bluetooth_data(cmd.AddrHighByte, cmd.AddrLowByte, pData, len, &pageBuffer)){
      CMDPending = false;
    }
  }
}

/**************************************************************************************************/
class MyClientCallback : public BLEClientCallbacks
{
  void onConnect(BLEClient* pclient)
  {
    Serial.println("onConnect");
    connected = true;
  }
  
  /**************************************************************************************************/
  void onDisconnect(BLEClient* pclient)
  {
    connected = false;
    Serial.println("onDisconnect");
  }
};

/**************************************************************************************************/
/* Start connection to the BLE Server */
bool connectToServer()
{
  Serial.print("Forming a connection to ");
  Serial.println(myDevice->getAddress().toString().c_str());
    
  BLEClient*  pClient  = BLEDevice::createClient();
  Serial.println(" - Created client");

  pClient->setClientCallbacks(new MyClientCallback());

    /* Connect to the remote BLE Server */
  pClient->connect(myDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
  Serial.println(" - Connected to server");

    /* Obtain a reference to the service we are after in the remote BLE server */
  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr)
  {
    Serial.print("Failed to find our service UUID: ");
    Serial.println(serviceUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our service");

  // Obtain a reference to the characteristic in the service of the remote BLE server.
  pRemoteWriteCharacteristic = pRemoteService->getCharacteristic(WRITE_UUID);
  if (pRemoteWriteCharacteristic == nullptr) {
    Serial.print(F("Failed to find our characteristic UUID: "));
    Serial.println(WRITE_UUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(F(" - Found our Write characteristic"));
    
  /* Obtain a reference to the characteristic in the service of the remote BLE server */
  pRemoteNotifyCharacteristic = pRemoteService->getCharacteristic(NOTIFY_UUID);
  if (pRemoteNotifyCharacteristic == nullptr)
  {
    Serial.print("Failed to find our characteristic UUID: ");
    Serial.println(NOTIFY_UUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our characteristic");

  /* Read the value of the characteristic */
  /* Initial value is 'Hello, World!' */
  if(pRemoteNotifyCharacteristic->canRead())
  {
    std::string value = pRemoteNotifyCharacteristic->readValue();
    Serial.print("The characteristic value was: ");
    Serial.println(value.c_str());
  }

  if(pRemoteNotifyCharacteristic->canNotify())
  {
    Serial.println("Notifikation allowed!");
    pRemoteNotifyCharacteristic->registerForNotify(notifyCallback);
  }

    return true;
}

/**************************************************************************************************/
/* Scan for BLE servers and find the first one that advertises the service we are looking for. */
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks
{
 /* Called for each advertising BLE server. */
  void onResult(BLEAdvertisedDevice advertisedDevice)
  {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());

    /* We have found a device, let us now see if it contains the service we are looking for. */
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID))
    {
      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = true;
    }
  }
};

/**************************************************************************************************/
uint16_t modbus_crc(uint8_t buf[], int len){
  unsigned int crc = 0xFFFF;
  
  for (unsigned int i = 0; i < len; i++)
  {
    crc = crc16_update(crc, buf[i]);
  }
  return crc;
}

/********************************************/
void sendCommand(bt_command_t command){
  
  Serial.print("Write Request FF02 - Value: ");
  
  for(int i=0; i<8; i++){
     if ( i % 2 == 0){ Serial.print(" "); };
     Serial.printf("%02x", ((uint8_t*)&command)[i]);
  }
  
  Serial.println("");

  CMDPending = true;
  pRemoteWriteCharacteristic->writeValue((uint8_t*)&command, sizeof(command),true);
}

/**************************************************************************************************/
void setup()
{
  Serial.begin(115200);
  Serial.println("Starting Arduino BLE Client application...");
  BLEDevice::init("ESP32-BLE-Client");

  /* Retrieve a Scanner and set the callback we want to use to be informed when we
     have detected a new device.  Specify that we want active scanning and start the
     scan to run for 5 seconds. */
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
  pageBuffer.idx = 0;
  pageBuffer.len = 0;
  /**************************************************************************************************/
  /*
          typedef struct __attribute__ ((packed)) {
            uint8_t prefix;            // 1 byte  BT prefix
            uint8_t field_update_cmd;  // 1 byte, Modbus Function code  
            uint8_t AddrHighByte;      // 1 byte, Modbus High Byte Starting Address
            uint8_t AddrLowByte;       // 1 byte, Modbus Low Byte Starting Address
            uint16_t len;              // 2 bytes, Modbus Quantity of Registers
            uint16_t check_sum;        // 2 bytes  
          } bt_command_t;
  */
  cmd.prefix = 0x01;
  cmd.field_update_cmd = 0x03;
  cmd.AddrHighByte = 0x00;
  cmd.AddrLowByte = 0x0A;
  cmd.len = 0x2800;
  cmd.check_sum = modbus_crc((uint8_t*)&cmd, 6);
}

void loop()
{

  /* If the flag "doConnect" is true, then we have scanned for and found the desired
     BLE Server with which we wish to connect.  Now we connect to it.  Once we are 
     connected we set the connected flag to be true. */
  if (doConnect == true)
  {
    if (connectToServer())
    {
      Serial.println("We are now connected to the BLE Server.");
    } 
    else
    {
      Serial.println("We have failed to connect to the server; there is nothin more we will do.");
    }
    doConnect = false;
  }

  /* If we are connected to a peer BLE Server, update the characteristic each time we are reached
     with the current time since boot */
  if (connected)
  {
    if(!CMDPending) {
      sendCommand(cmd);
    }
  }
  else if(doScan)
  {
    BLEDevice::getScan()->start(0);  // this is just example to start scan after disconnect, most likely there is better way to do it in arduino
  }
  
  delay(2000); /* Delay 2 second between loops */
}
