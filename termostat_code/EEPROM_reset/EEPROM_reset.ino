#include <EEPROM.h>

#define MQTT_PUBLISH_TOPICS   4 // Number of topics that the controller publishes to
#define MQTT_SUBSCRIBE_TOPICS 2 // Number of topics that the controller subscribes to

char mqttID[] = "TS0000002";

/*
  direction:
    0 -> publish
    1 -> subscribe
    2 -> base topic for publishing other resources from device, at least 1 subtopic will be present following this topic
    3 -> subscribe topic for receiving data that are forwarded to other devices, on this topic the subscribe(<TOPIC>/#) is performed

  data type:
    0 -> any
    1 -> temperature: 5byte string representing temp value [-99.9, 99.99]
    2 -> bool:        1 byte(0 -> false, 1-255 -> true)
    3 -> uint8_t:     1 byte(0-255)
*/
struct mqttTopic_t{
  uint8_t direction; 
  uint8_t topicDataType;
  char topicName[10];
  char delimiter;
};

union mqttPublishTopicList_t{
  char listString[MQTT_PUBLISH_TOPICS*sizeof(mqttTopic_t)];
  struct mqttTopic_t publishTopics[MQTT_PUBLISH_TOPICS];
} mqttPublishTopicList;

union mqttSubscribeTopicList_t{
  char listString[MQTT_SUBSCRIBE_TOPICS*sizeof(mqttTopic_t)];
  struct mqttTopic_t subscribeTopics[MQTT_SUBSCRIBE_TOPICS];
} mqttSubscribeTopicList;

struct mqttTopic_t topicPublish1 = { 0, 1, "temp1", ',' };
struct mqttTopic_t topicPublish2 = { 0, 1, "temp2", ',' };
struct mqttTopic_t topicPublish3 = { 0, 1, "temp3", ',' };
struct mqttTopic_t topicPublish4 = { 2, 0, "TWIOut",'\0' };

struct mqttTopic_t topicSubscribe1={ 3, 0, "TWIIn", ',' };
struct mqttTopic_t topicSubscribe2={ 1, 3, "tempEN",'\0' };

void setup() {
  //Start serial
  Serial.begin(9600);

  reset_eeprom();
  
  mqttPublishTopicList.publishTopics[0] = topicPublish1;
  mqttPublishTopicList.publishTopics[1] = topicPublish2;
  mqttPublishTopicList.publishTopics[2] = topicPublish3;
  mqttPublishTopicList.publishTopics[3] = topicPublish4;

  mqttSubscribeTopicList.subscribeTopics[0] = topicSubscribe1;
  mqttSubscribeTopicList.subscribeTopics[1] = topicSubscribe2;
  
  write_topics_to_eeprom();

  Serial.println("DONE.");
}

void loop() {
  // put your main code here, to run repeatedly:

}

void reset_eeprom(){
  int address = 0;

  for(address; address < EEPROM.length(); address++){
    if(address >= 137 && address < 146){
      EEPROM.update(address, (uint8_t)mqttID[address - 137]);
    }else{
      EEPROM.update(address, 0);
    }

    if(address % 32 == 0) {
      Serial.println();
      Serial.print(address);
    }
    
    delay(10);
  }
}

void write_topics_to_eeprom(){
  EEPROM.put(256, mqttPublishTopicList);
  EEPROM.put(512, mqttSubscribeTopicList);
}
