#include <M5Core2.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h> //https://github.com/bblanchon/ArduinoJson
#include <time.h>
#define emptyString String()
#include "certificates.h"
const int MQTT_PORT = 8883;
const char MQTT_SUB_TOPIC[] = "$aws/things/" THINGNAME "/shadow/update";
const char MQTT_PUB_TOPIC[] = "$aws/things/" THINGNAME "/shadow/update";
uint8_t DST = 1;

WiFiClientSecure net;
WiFiClientSecure client_mail;
PubSubClient client(net);

#define samp_siz 4
#define rise_threshold 5
int sensorPin = 36;
double alpha=0.75;
int period=20;
double refresh=0.0;

#define RXp2 13
#define TXp2 14
String indata;
int counter=0;
void setup() {
  // put your setup code here, to run once:
  M5.begin();
  WiFi.hostname(THINGNAME);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  net.setTrustAnchors(&cert);
  net.setClientRSACert(&client_crt, &key);
  client.setServer(MQTT_HOST, MQTT_PORT);
  while (!client.connected())
  {
    if (client.connect(THINGNAME))
    {
      M5.Lcd.println("connected to mqtt");
    }
  }
  M5.Lcd.fillScreen(WHITE);
  M5.Lcd.setTextColor(BLACK , WHITE);
  M5.Lcd.setTextSize(2);
  Serial2.begin(38400, SERIAL_8N1, RXp2, TXp2);
  pinMode(35,INPUT); //IR
  pinMode(36, INPUT);  //Heart
}
void loop() {
  M5.Lcd.setTextColor(BLACK , GREEN);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(20, 40);
  M5.Lcd.print("Welcome \n Mask and Heart Rate  \n Management System");
  
    if(digitalRead(35)==LOW){
        
    M5.Lcd.clear(WHITE);
    M5.Lcd.setCursor(20, 40);
    M5.Lcd.print("Person Detected \n Reading Mask \n Stand in Front of Camera");
    counter=0;
    Serial2.println("1");
    while (Serial2.available() <= 0)
    {}
    
    while(1){
      indata=Serial2.readString();
      if(indata.length() == 0){
      break;  
      }
      counter+=indata.toInt();
    }
    M5.Lcd.clear(WHITE);
    if(counter>=4){
      M5.Lcd.setCursor(20, 40);
      M5.Lcd.setTextColor(GREEN , WHITE);
      M5.Lcd.print("Mask Detected Successfully");
      int heart_cnt=0,tmp=0;
      while(heart_cnt<50){
         static double oldValue=0;
         static double oldrefresh=0;
         int beat=analogRead(36);
         double value=alpha*oldValue+(0-alpha)*beat;
         refresh=value-oldValue;
         tmp=beat/13;
         M5.Lcd.setCursor(0, 80);
         M5.Lcd.printf("BP:%d\n",tmp);
         oldValue=value;
         oldrefresh=refresh;
         delay(period*13);
         heart_cnt++;
      } 
      
      DynamicJsonDocument jsonBuffer(JSON_OBJECT_SIZE(3) + 100);
      JsonObject root = jsonBuffer.to<JsonObject>();
      JsonObject state = root.createNestedObject("state");
      JsonObject heart_reported = state.createNestedObject("heart");
      JsonObject mask_reported = state.createNestedObject("mask");
      heart_reported["value"] = tmp; //heart rate
      mask_reported["value"] = 1; //1 for correct mask
      Serial.printf("Sending  [%s]: ", MQTT_PUB_TOPIC);
      serializeJson(root, Serial);
      char shadow[measureJson(root) + 1];
      serializeJson(root, shadow, sizeof(shadow));
      if (!client.publish(MQTT_PUB_TOPIC, shadow, false))   //publish data
         M5.Lcd.println("Error Sending Data");
    }
    else{
      M5.Lcd.setCursor(20, 40);
      M5.Lcd.setTextColor(RED , WHITE);
      M5.Lcd.print("No Mask Detected");
      //sending ses email change capital words to your respective destination and authenticated email
      client_mail.connect("GET https://YOUREMAIL.us-west-2.amazonaws.com?Action=SendEmail&Source=YOUREMAIL&Destination.ToAddresses.member.1=DESTINATIONEMAIL&Message.Subject.Data=Mask%20Intruder%20Alert.&Message.Body.Text.Data=There%20Is%20Person%20at%20Gate%20without%20a%20Mask.");
      
    }
    delay(3000);
    M5.Lcd.clear(WHITE);
    }
    
    
}
