#include <SPI.h>
#include <Ethernet.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <Wire.h>

#define TRIGGERED true
#define RST_DEBOUNCE 5000

// #define SERIAL_DEBUG      0  // uncomment to enable serial debugging. Be aware of the extra memory usage

#define I2C_ADDRESS       0x33
#define ETH_SHIELD_CS     6    // 6 for PCB and 10 for Arduino UNO

// Ethernet shield: CS=10, 11, 12, 13

#define AVG_TEMP_MEASUREMENTS 10

// MQTT TOPIC DESCRIPTORS
#define MQTT_PUBLISH_TOPICS   4 // Number of topics that the controller publishes to
#define MQTT_SUBSCRIBE_TOPICS 2 // Number of topics that the controller subscribes to

struct mqttTopic_t{
  uint8_t direction;
  uint8_t topicDataType;
  char topicName[10];
  char delimiter;
};

union mqttPublishTopicList_t{
  uint8_t listPayload[MQTT_PUBLISH_TOPICS*sizeof(mqttTopic_t)];
  struct mqttTopic_t publishTopics[MQTT_PUBLISH_TOPICS];
} mqttPublishTopicList;

union mqttSubscribeTopicList_t{
  uint8_t listPayload[MQTT_SUBSCRIBE_TOPICS*sizeof(mqttTopic_t)];
  struct mqttTopic_t subscribeTopics[MQTT_SUBSCRIBE_TOPICS];
} mqttSubscribeTopicList;

// ETHERNET DATA
byte mac[] = {0x28, 0x86, 0x4B, 0x44, 0x0C, 0xD3}; // LAST BYTE: C2, D3
IPAddress factorySettingsIP = IPAddress(192,168,8,10);

EthernetServer setupServer(35252); // If factory settings are on before any initialization setup will open a server on port 35252 and listen for new settings

EthernetClient ethClient;
PubSubClient client(ethClient);

// MQTT DATA
IPAddress mqttBrokerIP= IPAddress();
char mqttBrokerName[64];
uint16_t mqttPort     = 1883;
char mqttUserTest[32];
char mqttPassTest[32];
bool useMQTTcredentials = false;
char mqttDeviceId[10];

const char* mqttTopicDisplayText1 = "dispTxt1";

char subscribeTopic1[32]; // TWIIn topic
char subscribeTopic2[32]; // tempEN topic

char publishTopic1[32];   // temp1 topic
char publishTopic2[32];   // temp2 topic
char publishTopic3[32];   // temp3 topic
char publishTopic4[32];   // TWIOut topic

void callback(char*, byte*, unsigned int);

void reconnect();

bool mqttIsConnected = false;

//READ THERMISTOR WITH BETA
byte thermistorPin1 = A0;
byte thermistorPin2 = A1;
byte thermistorPin3 = A2;

const double BALANCE_RESISTOR = 10000.0;// Resistor value from the divider
const double ROOM_THERMISTOR = 10000.0; // Room temperature thermistor value
const double ROOM_TEMP = 298.15;        // Room temperature in kelvin
const double MAX_ADC = 1023.0;          // Maximum ADC output value
const double BETA = 3950.0;             // Thermistor Beta koeficient

// FACTORY RESET
byte setupServerStatusLEDpin = 7;
byte factoryResetSignalLED = 4;
byte factoryResetButton = 2;
bool factoryResetInteruptStatus = !TRIGGERED;
bool buttonSwitchPending = false;

// TIMINGS
unsigned long timingMqttTempReading;
unsigned long factoryResetButtonTimer;
unsigned long timingEthernetDHCPmaintain;

void setup() {
  #ifdef SERIAL_DEBUG
    Serial.begin(9600);
    Serial.flush();
    Serial.println("Begin");
  #endif
  // ----------ETHERNET SETUP----------
  pinMode(ETH_SHIELD_CS, OUTPUT);
  
  Ethernet.init(ETH_SHIELD_CS);

  pinMode(setupServerStatusLEDpin, OUTPUT); // Indication of the initial setup status
  pinMode(factoryResetSignalLED, OUTPUT);   // Blinks when factory reset successful

  // INITIAL MQTT DATA SETUP
  if(EEPROM[0] == 0){
    initialDataSetupRoutine(); // Wait for programing data and write it to EEPROM
  }

  Ethernet.begin(mac);

  delay(1000);

  #ifdef SERIAL_DEBUG
    Serial.println(Ethernet.hardwareStatus());
  #endif
  // while(Ethernet.begin(mac) == 0) {
  //   // Failed to configure Ethernet using DHCP, try again in 5s

  //   delay(5000);
  // }
  digitalWrite(ETH_SHIELD_CS, HIGH);  // Fixing bug where CS pin stays LOW after Ethernet.begin call

  setMQTTbroker();    // Gets mqtt data from EEPROM and starts the connection with it
  setMQTTuser();      // Gets username and password for MQTT Broker from EEPROM
  getMQTTtopicList(); // Gets the topic descriptors from ROM

  #ifdef SERIAL_DEBUG
  Serial.print("Username: ");
  Serial.println(mqttUserTest);

  Serial.print("Pass: ");
  Serial.println(mqttPassTest);
  #endif

  client.setCallback(callback); // Callback function for mqtt subscription data input handling

  // // Got IP address
  #ifdef SERIAL_DEBUG
  Serial.println(Ethernet.localIP());
  #endif

  // I2C BUS CONFIG
  Wire.begin(I2C_ADDRESS);
  Wire.onReceive(wireReceiveHandler); // Change response to request based on last received data
  Wire.onRequest(wireRequestHandler); // Response to a request based on the last received data

  // THERMISTOR SETUP
  pinMode(thermistorPin1, INPUT);
  pinMode(thermistorPin2, INPUT);
  pinMode(thermistorPin3, INPUT);

  // FACTORY RESET BUTTON SETUP
  pinMode(factoryResetButton, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(factoryResetButton), handleFactoryResetInterupt, FALLING);
  
  // START TIMINGS
  timingMqttTempReading = millis();
  timingEthernetDHCPmaintain = timingMqttTempReading;
}

byte tempReading = 1;

void loop() {
  unsigned long currentMillis = millis();

  // MAINTAIN ETHERNET CONNECTION
  if(currentMillis - timingEthernetDHCPmaintain > 30000){
    // Every 30s maintain connection
    switch (Ethernet.maintain()) {
      case 1:
        //renewed fail
        break;

      case 2:
        //renewed success
        break;

      case 3:
        //rebind fail
        break;

      case 4:
        //rebind success
        break;

      default:
        //nothing happened
        break;
    }

    timingEthernetDHCPmaintain = currentMillis;   
  }

  // -------------MQTT CONNECTION HANDLING--------------
  if(!client.connected()) mqttIsConnected = false; // Ask for reconnection if disconnected
  
  // FACTORY RESET DEBOUNCING
  readFactoryResetButton(currentMillis);

  // TEMPERATURE MEASUREMENTS
  if(currentMillis - timingMqttTempReading > 5000){
    if(tempReading == 1){
      publishTemp(thermistorPin1, 1);
      tempReading++;
    }else if(tempReading == 2){
      publishTemp(thermistorPin2, 2);
      tempReading++;
    }else if(tempReading == 3){
      publishTemp(thermistorPin3, 3);
      tempReading = 1;
    }else{
      tempReading = 1;
    }
    
    timingMqttTempReading = currentMillis;
  }

  // Loop if connected, else try reconnecting
  if(mqttIsConnected){
    client.loop();
  }else{
    reconnect();
  }
}

// -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- 
// -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- 
// -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- 
// -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- 

void callback(char* topic, byte* payload, unsigned int length) {
  #ifdef SERIAL_DEBUG
  Serial.println("---------------------------------");
  Serial.print("Topic: ");
  Serial.print(topic);

  Serial.println();
  Serial.print("Payload(" + String(length) + "): ");
  #endif

  // TODO: Find which subscribe topic the request came from and parse the payload according to the data type in the topic descriptor
  
  // Try to match topic to subscribeTopic1
  for(uint8_t i = 0; i < 32; i++){
    if(topic[i] != subscribeTopic1[i]){
      break;
    }

    if(topic[i] == '\0' && subscribeTopic1[i] == '\0'){
      // TODO: Parse payload for subscribeTopic1 and forward it to its function
    }
  } 
}

void reconnect() {
  // Try connecting

  delay(1000);

  // Attempt to connect with credentials or just ID as set in settings
  if(useMQTTcredentials){
    if (client.connect(mqttDeviceId, &mqttUserTest[0], &mqttPassTest[0])) {
      // connected
      mqttIsConnected = true;

      #ifdef SERIAL_DEBUG
      Serial.println("MQTT Connected");
      #endif
      // Once connected, publish an announcement containing topics this device supports
      client.publish(mqttDeviceId, &mqttPublishTopicList.listPayload[0], sizeof(mqttPublishTopicList_t));
      client.publish(mqttDeviceId, &mqttSubscribeTopicList.listPayload[0], sizeof(mqttSubscribeTopicList_t));

      renewSubscriptions();

    } else {
      #ifdef SERIAL_DEBUG
      Serial.println("MQTT Not Connected");
      #endif
      // Wait 2 seconds before retrying
      // delay(2000);
    }
  } else {
    if(client.connect(mqttDeviceId)){
      mqttIsConnected = true;

      #ifdef SERIAL_DEBUG
      Serial.println("MQTT Connected");
      #endif
      // Once connected, publish an announcement containing topics this device supports
      client.publish(mqttDeviceId, &mqttPublishTopicList.listPayload[0], sizeof(mqttPublishTopicList_t));
      client.publish(mqttDeviceId, &mqttSubscribeTopicList.listPayload[0], sizeof(mqttSubscribeTopicList_t));

      renewSubscriptions();

    } else {
      #ifdef SERIAL_DEBUG
      Serial.println("MQTT Not Connected");
      #endif
      // Wait 2 seconds before retrying
      // delay(2000);
    }
  }
}

void handleFactoryResetInterupt(){
  if(factoryResetInteruptStatus == !TRIGGERED){

    factoryResetInteruptStatus = TRIGGERED;
    factoryResetButtonTimer = millis();
  }
}

void readFactoryResetButton(unsigned long currentMillis){
  if(factoryResetInteruptStatus == TRIGGERED){
    if(digitalRead(factoryResetButton) == HIGH){
      factoryResetInteruptStatus = !TRIGGERED;
      return;
    }

    if(currentMillis - factoryResetButtonTimer > RST_DEBOUNCE){
      factoryResetInteruptStatus = !TRIGGERED;

      digitalWrite(factoryResetSignalLED, HIGH);
      EEPROM.update(0, 0);
        
      #ifdef SERIAL_DEBUG
      Serial.println("FACTORY RESET");
      #endif
        
      delay(1000);
      digitalWrite(factoryResetSignalLED, LOW);
    }
  }
}

void initialDataSetupRoutine(){
  Ethernet.begin(mac, factorySettingsIP);

  #ifdef SERIAL_DEBUG
    Serial.println(Ethernet.hardwareStatus());
  #endif

  digitalWrite(ETH_SHIELD_CS, HIGH);  // Fixing bug where CS pin stays LOW after Ethernet.begin call

  // Start initial setup server
  digitalWrite(setupServerStatusLEDpin, HIGH);

  setupServer.begin();

  bool transferBegin = false;
  byte addressLocationCounter = 1;

  while(EEPROM[0] != 1){
    EthernetClient setupClient = setupServer.available();
    if(setupClient){
      byte data = setupClient.read();
      #ifdef SERIAL_DEBUG
      Serial.print(data);
      #endif

      if(transferBegin){
        EEPROM.update(addressLocationCounter++, data);
      }

      if(data == 254){
        transferBegin = true;
      }
    }

    if(addressLocationCounter == 137){
      EEPROM[0] = 1;
    }
  }

  // setupServer.end();
  digitalWrite(setupServerStatusLEDpin, LOW);

  while(1){
    // Constant loop. Reset is necesary to get unstuck and reload program
    delay(1000);
    digitalWrite(setupServerStatusLEDpin, HIGH);
    delay(1000);
    digitalWrite(setupServerStatusLEDpin, LOW);
  }
}

void renewSubscriptions(){
  client.subscribe(subscribeTopic1);
  client.subscribe(subscribeTopic2);
}

// ------------------------------ GETTING DATA FROM EEPROM ------------------------------ //

void setMQTTbroker(){

  // EEPROM.get(70, mqttPort); <- Returns little-endian and the saved data is big-endian
  mqttPort = (EEPROM[70] << 8) + EEPROM[71];

  if(EEPROM[1] == 0){
    // Use IP for Broker
    mqttBrokerIP = IPAddress(EEPROM[2], EEPROM[3], EEPROM[4], EEPROM[5]);
    #ifdef SERIAL_DEBUG
    Serial.print(mqttBrokerIP);
    Serial.print(":");
    Serial.println(mqttPort);
    #endif
    client.setServer(mqttBrokerIP, mqttPort);
  }else{
    // Use domain name for broker
    for(byte i = 0; i < 64; i++){
      mqttBrokerName[i] = (char)EEPROM.read(i + 6);
    }
    #ifdef SERIAL_DEBUG
    Serial.print(mqttBrokerName);
    Serial.print(":");
    Serial.println(mqttPort);
    #endif
    client.setServer(mqttBrokerName, mqttPort);
  }

  for(uint8_t i = 0; i < 9; i++){
    mqttDeviceId[i] = EEPROM[i + 137]; // Loading ID from EEPROM
  }

  mqttDeviceId[9] = '\0';
}

void setMQTTuser(){
  if(EEPROM[72] == 1){
    useMQTTcredentials = true;

    for(int i = 0; i < 32; i++){
      mqttUserTest[i] = (char)EEPROM[ 73 + i];
      mqttPassTest[i] = (char)EEPROM[105 + i];
    }
  }else{
    useMQTTcredentials = false;

    for(int i = 0; i < 32; i++){
      mqttUserTest[i] = '\0';
      mqttPassTest[i] = '\0';
    }
  }
  
}

void getMQTTtopicList(){
  EEPROM.get(256, mqttPublishTopicList);
  EEPROM.get(512, mqttSubscribeTopicList);
  
  // assembling subscribe subtopics from userID and subtopic names
  for(int i = 0; i < MQTT_SUBSCRIBE_TOPICS; i++){
    uint8_t topicCharCounter = 0;

    mqttTopic_t tempTopic = mqttSubscribeTopicList.subscribeTopics[i];

    if (tempTopic.direction == 1){
      
      for(int j = 0; j < 9; j++){
        subscribeTopic2[topicCharCounter++] = mqttDeviceId[j];
      }

      subscribeTopic2[topicCharCounter++] = '/';

      for(int j = 0; j < 10; j++){
        if(tempTopic.topicName[j] != '\0'){
          subscribeTopic2[topicCharCounter++] = tempTopic.topicName[j];
        }
      }

      subscribeTopic2[topicCharCounter++] = '\0';

    }else if(tempTopic.direction == 3){

      for(int j = 0; j < 9; j++){
        subscribeTopic1[topicCharCounter++] = mqttDeviceId[j];
      }

      subscribeTopic1[topicCharCounter++] = '/';

      for(int j = 0; j < 10; j++){
        if(tempTopic.topicName[j] != '\0'){
          subscribeTopic1[topicCharCounter++] = tempTopic.topicName[j];
        }
      }

      subscribeTopic1[topicCharCounter++] = '/';
      subscribeTopic1[topicCharCounter++] = '#';
      subscribeTopic1[topicCharCounter++] = '\0';
    }
  }

  // assembling publish subtopics from userID and subtopic names
  for(int i = 0; i < MQTT_PUBLISH_TOPICS; i++){
    uint8_t topicCharCounter = 0;

    mqttTopic_t tempTopic = mqttPublishTopicList.publishTopics[i];
    char *topicToFill;

    switch(i){
      case 0:
      topicToFill = &publishTopic1[0];
      break;

      case 1:
      topicToFill = &publishTopic2[0];
      break;

      case 2:
      topicToFill = &publishTopic3[0];
      break;

      case 3:
      topicToFill = &publishTopic4[0];
      break;

      default:
      return;  
    }

    for(int j = 0; j < 9; j++){
      topicToFill[topicCharCounter++] = mqttDeviceId[j];
    }

    topicToFill[topicCharCounter++] = '/';

    for(int j = 0; j < 10; j++){
      if(tempTopic.topicName[j] != '\0'){
        topicToFill[topicCharCounter++] = tempTopic.topicName[j];
      }
    }

    if(i == 3){
      // For TWIOUT prepare for subtopics
      topicToFill[topicCharCounter++] = '/';
    }

    topicToFill[topicCharCounter++] = '\0';

  }

}

// -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- 
// -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- 
// -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- 
// -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- // -- 

void wireRequestHandler(){
  Wire.write("Termostat SN: 12345678910");
}

void wireReceiveHandler(){
  while(Wire.available()){
    char c = Wire.read();
    #ifdef SERIAL_DEBUG
    Serial.print(c);
    #endif
  }
  #ifdef SERIAL_DEBUG
  Serial.println();
  #endif
}

double readThermistorBeta(uint8_t thermistorPin){

  double rThermistor = 0;
  double tKelvin = 0;
  double tCelsius = 0;
  double adcAvg = 0;
  int adcSamples[AVG_TEMP_MEASUREMENTS];

  for(int i = 0; i < AVG_TEMP_MEASUREMENTS; i++){
    adcSamples[i] = analogRead(thermistorPin);
    delay(20);
  }

  for(int i = 0; i < AVG_TEMP_MEASUREMENTS; i++){
    adcAvg += adcSamples[i];
  }

  adcAvg /= AVG_TEMP_MEASUREMENTS;

  rThermistor = BALANCE_RESISTOR * ((MAX_ADC / adcAvg) - 1);
  
  #ifdef SERIAL_DEBUG
  Serial.print("rThermistor: ");
  Serial.println(rThermistor);
  #endif

  tKelvin = (BETA * ROOM_TEMP) / (BETA + (ROOM_TEMP * log(rThermistor / ROOM_THERMISTOR)));

  tCelsius = tKelvin - 273.15;

  return tCelsius;
}

void publishTemp(uint8_t pin, uint8_t tempTopic){
  double temperature = readThermistorBeta(pin);

  char payloadString[6];
  doubleTempValueToChar(temperature, payloadString);

  #ifdef SERIAL_DEBUG
  Serial.println(payloadString);
  Serial.println(client.connected());
  #endif

  if(client.connected()){
    switch(tempTopic){
      case 1:
      client.publish(publishTopic1, payloadString);
      break;

      case 2:
      client.publish(publishTopic2, payloadString);
      break;

      case 3:
      client.publish(publishTopic3, payloadString);
      break;

      default:
      break;
    }
    
  }
  
}

// size of char holder should be at least 6 char -> 5 for temp[99.99, -99.9] and \0 
void doubleTempValueToChar(double tempValue, char* charHolder){
  byte charHolderPointer = 0;
  if(tempValue < 0){
    tempValue = -tempValue;
    charHolder[charHolderPointer++] = '-';
  }

  if(tempValue > 99.99){
    tempValue = 99.99;
  }

  unsigned int integerComponentOfTemp = (unsigned int)tempValue;

  if(integerComponentOfTemp == 0){
    charHolder[charHolderPointer++] = '0';
  }else{
    (integerComponentOfTemp <= 9)?
    (charHolder[charHolderPointer++] = (char)(integerComponentOfTemp + 48)):
    ({
      charHolder[charHolderPointer++] = (char)((integerComponentOfTemp / 10) + 48);
      charHolder[charHolderPointer++] = (char)((integerComponentOfTemp % 10) + 48);
    });
  }

  charHolder[charHolderPointer++] = '.';

  tempValue -= integerComponentOfTemp;
  integerComponentOfTemp = (int)(tempValue * 100);

  if(charHolderPointer == 3){
    (integerComponentOfTemp <= 9)?
    ({
      charHolder[charHolderPointer++] = '0';
      charHolder[charHolderPointer++] = (char)(integerComponentOfTemp + 48);
    }):
    ({
      charHolder[charHolderPointer++] = (char)((integerComponentOfTemp / 10) + 48);
      charHolder[charHolderPointer++] = (char)((integerComponentOfTemp % 10) + 48);
    });
  }else{
    charHolder[charHolderPointer++] = (char)((integerComponentOfTemp / 10) + 48);
  }

  charHolder[charHolderPointer] = '\0';
}
