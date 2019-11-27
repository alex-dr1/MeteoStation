#include <OneWire.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>

#include <Adafruit_BMP280.h>

#define ONE_WIRE_BUS 2 // GPIO к которому подключен DS18B20
#define postingInterval 1000*10*60 // интервал между отправками данных в миллисекундах

#ifndef STASSID
#define STASSID "Router81"
#define STAPSK  "12341234"
#endif

const char* host = "narodmon.ru";
const int httpPort = 8283;
const char* ssid = STASSID;
const char* password = STAPSK;
unsigned long lastConnectionTime = 0;           // время последней передачи данных
OneWire  ds(ONE_WIRE_BUS);
Adafruit_BMP280 bmp;

IPAddress IP(192, 168, 1, 5);
IPAddress IPGataway(192, 168, 1, 1);
IPAddress IPSubnet(255, 255, 255, 0);

float T1;
float Press1;

bool StartProg;
int countStart;

const int ledWork = 16;// Индикатор работы Д1
const int ledErr = 0; // Индикатор ошибки Д2

int getPress(float *pressure) // Опрос датчика давления
{
	if (!bmp.begin(0x76)) {
    Serial.println(F("Could not find a valid BMP280 sensor, check wiring!"));
    return false;
  	}

  	/* Default settings from datasheet. */
  	bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
                  Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                  Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                  Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                  Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */
    
    *pressure = floor(bmp.readPressure()*.007500637554192*100)/100;
    return true;
} 

int getTemp(float *temperature) // фукция опоса датчика температуры
{

	byte addr[8];
	byte data[12]; 

	if (!ds.search(addr)) {
	    Serial.println("No more addresses."); 
		return false;
	}
	ds.reset_search(); 
	 
	if (OneWire::crc8(addr, 7) != addr[7]) {
	    Serial.println("CRC is not valid!");
	    return false;
	}

	ds.reset();            
	ds.select(addr);        
	ds.write(0x44);      
	delay(500);   
	  
	ds.reset();
	ds.select(addr);    
	ds.write(0xBE);          

	for (int i = 0; i < 9; i++) {           
	    data[i] = ds.read();  
	}

	int16_t raw = (data[1] << 8) | data[0];
	byte cfg = (data[4] & 0x60);
	// at lower res, the low bits are undefined, so let's zero them
	if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
	else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
	else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
	//// default is 12 bit resolution, 750 ms conversion time
	
	if (raw/16.0>=85.0) return 1; 
	*temperature = raw/16.0; 
	return true;
}

int sendToNarodMon()
{
    WiFi.begin(ssid, password);
  
    // Wait for connection
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }

    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("MAC address: ");
    Serial.println(WiFi.macAddress());
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(host);
    
    // Use WiFiClient class to create TCP connections
    WiFiClient client;
  
    if (!client.connect(host, httpPort)) {
      Serial.println("connection failed");
      return false;
    }

    // отправляем данные
  /*
  #5B-A5-66-0C-9C-BA#Метео
  #T1#-17.52#Улица
  #T2#24.54#Дом
  #P1#746.6#Барометр
  #LAT#57.8979
  #LNG#60.02
  #ELE#123.5
  ##
  */
  String telega  = "#DC-4F-22-61-15-56#ул.Колхозная\n";
  telega += "#T1#";//-17.52
  telega += T1;
  telega += "#Улица\n";
  telega += "#P1#";//742.18
  telega += Press1;
  telega += "#Барометр\n";
  telega += "#LAT#57.7883\n";
  telega += "#LNG#60.0717\n";
  telega += "##\n";  
  Serial.println("Sending..."); 
      // заголовок
  client.print(telega);
  Serial.print(telega);

  delay(10);

  // читаем ответ с и отправляем его в сериал
  Serial.print("Requesting: ");  
  while(client.available()){
    String line = client.readStringUntil('\r');
    Serial.print(line); // хотя это можно убрать
  }
  
  client.stop();
  Serial.println();
  Serial.println("Closing connection");
  return true;
}

void setup() // установки
{
    pinMode(ledWork, OUTPUT);
    digitalWrite(ledWork, 0);
  
    pinMode(ledErr, OUTPUT);
    digitalWrite(ledErr, 0);
    
  	Serial.begin(115200);
   	delay(10);

    StartProg = true;
	  countStart = 0;
   	Serial.println("\nStart...");
}

void loop() // главный цикл
{
	if(StartProg){
		if (countStart%2)
		{
			digitalWrite(ledWork, 0);
	     	digitalWrite(ledErr, 1);
	     	//Serial.println("RED");
	     	countStart++;
	     	delay(1000);
		}
		else{
	       	digitalWrite(ledWork, 1);
	     	digitalWrite(ledErr, 0);
	     	//Serial.println("BLUE");
	     	countStart++;
	     	delay(1000);
	   	}
	   	if(countStart >= postingInterval/1000){
	   		 StartProg = false;
	   			// борьба с 0.00 показаниями при старте
			   	// вызываем 
			   	getTemp(&T1);
			    delay(10);
	   		}
	}
	else{
		
		// отправка данных на народный мониторинг
		if (millis() - lastConnectionTime > postingInterval)// ждем postingInterval минут и отправляем
		{ 
		  lastConnectionTime = millis();
	      digitalWrite(ledWork, 0);
	      // забираем температуру
	      if(getTemp(&T1) && getPress(&Press1))
	        {
	          if (sendToNarodMon())
	          {
	            digitalWrite(ledErr, 0);
	            digitalWrite(ledWork, 1);
	          }
	          else
	          {
	            digitalWrite(ledErr, 1);
	          }
	        }
	        else
	        {
	          Serial.println("Sensor malfunction");
	          digitalWrite(ledErr, 1);
	        }
	      
	    }

   yield(); // что за команда - фиг знает, но ESP работает с ней стабильно и не глючит.
	}   

}
