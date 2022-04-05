
#define ARDUINOJSON_DEFAULT_NESTING_LIMIT 20

#include "secrets.h" //Arquivo que contém as chaves de segurança da AWS
#include <WiFiClientSecure.h> //Biblioteca para a segurança do protocolo wifi
#include <MQTTClient.h> //Biblioteca para o protocolo MQTT
#include <ArduinoJson.h> //Biliboteca para a criacao dos JSON
#include <WiFi.h> //Biblioteca para a comunicacao pelo meio fisico Wi-fi
#include "DHT.h" //Biblioteca para o sensor DHT11 - Umidade e Temperatura
#include <SPI.h>    //Biblioteca para a comunicação SPI com a antena LoRa
#include <LoRa.h>   //Biblioteca para a comunicação o LoRa
//#include "LoRa_functions.h"
#include "FS.h"
#include "SPIFFS.h"
#include "SPIFFS_functions.h"

#define FORMAT_SPIFFS_IF_FAILED true

String payloadJSON = "{\"time\":1640370551119,\"iot_identifier\":\"128392180392109381\",\"central\":{\"id_nucleo\":201,\"status_device\":1,\"time_send\":10,\"sensors\":[{\"id_sensor\":200001,\"type\":\"temperature\",\"ad_paramameters\":[{\"version\":\"DHT11\",\"port\": 27}]},{\"id_sensor\":200002,\"type\":\"humidity\",\"ad_paramameters\":[{\"version\":\"DHT11\",\"port\": 27}]}],\"granjas\":[{\"id_granja\":2001,\"iot_end\":[{\"id_iot_end\":20021,\"sensors\":[{\"id_sensor\":200001,\"type\":\"water-level\",\"ad_paramameters\":[{\"port\": 22}]},{\"id_sensor\":200002,\"type\":\"water-flow-rate\",\"ad_paramameters\":[{\"port\": 4}]},{\"id_sensor\":200003,\"type\":\"water-flow-rate\",\"ad_paramameters\":[{\"port\": 35}]}]},{\"id_iot_end\":20022,\"sensors\":[{\"id_sensor\":200001,\"type\":\"temperature\",\"ad_paramameters\":[{\"version\":\"DHT22\",\"port\": 27}]},{\"id_sensor\":200002,\"type\":\"humidity\",\"ad_paramameters\":[{\"version\":\"DHT22\",\"port\": 27}]},{\"id_sensor\":200003,\"type\":\"iqar\",\"ad_paramameters\":[{\"port\": 34}]},{\"id_sensor\":200004,\"type\":\"lux\",\"ad_paramameters\":[{\"port\": 32}]}]}]}]}}";

const int ledStatusErro = 12;
const int ledStatusCom = 13;
const int botaoManual = 21;

typedef struct estruturaSensor
{
  int id_sensor;
  const char* type;
  const char* ad_paramameters;
};

//Variavel interpretadas do Json recebido
byte numeroDevices[10]; //Variavel para determinar quantos dispositivos de campo existem
byte numeroGranjas = 0; //Varivel que armazena o numero de granjas
int idEndDevice[10][10]; //Variavel que armazena ID do end device
byte numeroSensoresCentral = 0; //Varivel que armazena o numero de sensores que tem na central
byte numeroSensoresEndDevice[10][10]; //Armazena o numero de sensores em cada end device
estruturaSensor sensoresCentral[10]; //Armazena a quantidade de sensores na central
estruturaSensor sensores[10][10][10]; //Armazena a informação de cada sensor

//Variveis globais
bool atualizarLed = 0; //Variavel para atualizar led de status
bool valoresEscritos = 0; //Variavel de conferencia para ver se os valores recebidos do LoRa já foram escritos
byte enderecoInicialEndDevices = 20; //Define o endereço iniciar dos dispositivos de campo
byte sensoresAtivos[5] = {B00001111, B01110000, B00000000, B00000000, B00000000};
String mensagemRecebidaLoRa;
int indeceString = 0; //Recebe o endereço que se encontra o caracter '/' dentro da string
String valorSensor = "";

//Pinos de conexão SPI da antena LoRa
const int csPin = 5;          //LoRa radio chip select
const int resetPin = 14;      //LoRa radio reset
const int irqPin = 2;         //change for your board; must be a hardware interrupt pin

//Define valor para a comunicação das AntenasLoRa
String outgoing;              // outgoing message
byte msgCount = 0;            // count of outgoing messages
byte localAddress = 0x0A;     // address of this device
byte destination;             // destination to send
long lastSendTime = 0;        // last send time
int interval = 2000;          // interval between sends
byte enderecoEscravo = 0;     //Recebe o endereço lido do LoRa
int valorEscravo = 0;         //Recebe o valor lido do LoRa
int valoresrecebidos[5]; //Vetor que armazena os valores recebidos do LoRa

bool debug = true; //Variavel utilizado para o debug do programa (True = inicia a serial / False = não imprime na serial)

hw_timer_t *timer = NULL; //Faz o controle do temporizador (interrupção por tempo)

DHT dht(27, DHT11); //Declara sensor DHT11 na porta 27

//Variveis enviadas ao AWS
String temperaturaCentral; //Variavel Temperatura da central
String umidadeCentral; //Variavel Umidade da central
String temperatura; //Variavel Temperatura
String umidade; //Variavel Umidade
String luminosidade; //Variavel Luminosidade
String qualidadeAr; //Variavel Qualidade de Ar
String totalLitrosEsquerda; //Variavel que armazena o total de litros passados na esquerda
String totalLitrosDireita; //Variavel que armazena o total de litros passados na direita
String nivelReservatorio; //Variavel que armazena o nivel do reservatorio

//Variaveis recebidas na AWS
byte tempopublicacao = 5; //Variavel do tempo entre cada publicacao a nuvem - default 5s
byte statusDevice = 0; //Recebe o status se é para escrever ou não

//Define variaveis para contar tempo
unsigned long previousMillis = 0;
unsigned long tempLedDesligado = 0;
unsigned long timeOutLoRaCampo = 0;

//Topicos do MQTT para publicacao e leitura
#define AWS_IOT_PUBLISH_TOPIC   "avesoft/201/pub"
#define AWS_IOT_SUBSCRIBE_TOPIC "avesoft/201/sub"

WiFiClientSecure net = WiFiClientSecure(); //Instancia o wifi seguro
MQTTClient client = MQTTClient(1500);  //Instancia o client MQTT

void imprimiInfoSensores()
{
  Serial.print("Total de Sensores Central: "); Serial.println(numeroSensoresCentral);
  for (int h = 0; h < numeroSensoresCentral; h++)
  {
    Serial.print("Dados do Sensor  "); Serial.print(h); Serial.println(" da central:");
    Serial.println("-------------------------------------------");
    Serial.print("ID do Sensor: "); Serial.println(sensoresCentral[h].id_sensor);
    Serial.print("Type do Sensor: "); Serial.println(sensoresCentral[h].type);
    Serial.print("Parameters do Sensor: "); Serial.println(sensoresCentral[h].ad_paramameters);
    Serial.println("-------------------------------------------");
  }

  for (int i = 0; i < numeroGranjas; i++)
  {
    Serial.print("Granja: "); Serial.println(i);
    int aux = numeroDevices[i];
    Serial.print("Numero de end devices da granja: "); Serial.println(aux);
    for (int j = 0; j < aux; j++)
    {
      int aux1 = numeroSensoresEndDevice[i][j];
      Serial.print("ID do end device "); Serial.print(j); Serial.print(": "); Serial.println(idEndDevice[i][j]);
      Serial.print("Numero sensores no end device "); Serial.print(j); Serial.print(": "); Serial.println(aux1);
      for (int k = 0; k < aux1; k++)
      {
        Serial.print("Dados do Sensor  "); Serial.print(k); Serial.print(" do end device "); Serial.print(j); Serial.println(":");
        //Armazena as informação do sensor. Sendo a linha a granja, a coluna o end device, e a profundidade o sensor
        Serial.println("-------------------------------------------");
        Serial.print("ID do Sensor: "); Serial.println(sensores[i][j][k].id_sensor);
        Serial.print("Type do Sensor: "); Serial.println(sensores[i][j][k].type);
        Serial.print("Parameters do Sensor: "); Serial.println(sensores[i][j][k].ad_paramameters);
        Serial.println("-------------------------------------------");
      }
    }
  }
}

//Funcao para reiniciar o ESP32
void IRAM_ATTR resetModule() {
  if (debug) Serial.println("(watchdog) reiniciar\n"); //Imprime no log
  ESP.restart(); //Reinicia o chip
}

//Funcao para a conexão a nuvem da AWS
void connectAWS()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  if (debug) Serial.println("Connecting to Wi-Fi");

  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(ledStatusErro, HIGH);
    delay(500);
    if (debug) Serial.print(".");
  }
  digitalWrite(ledStatusErro, LOW);
  delay(100);

  // Configure WiFiClientSecure to use the AWS IoT device credentials
  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);

  // Connect to the MQTT broker on the AWS endpoint we defined earlier
  client.begin(AWS_IOT_ENDPOINT, 8883, net);

  // Create a message handler
  client.onMessage(messageHandler);

  if (debug) Serial.println("Connecting to AWS IOT");
  digitalWrite(ledStatusErro, HIGH);
  while (!client.connect(THINGNAME)) {
    if (debug) Serial.print(".");
    delay(100);
  }

  if (!client.connected()) {
    if (debug) Serial.println("AWS IoT Timeout!");
    return;
  }

  // Subscribe to a topic
  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);

  if (debug) Serial.println("AWS IoT Connected!");

  digitalWrite(ledStatusErro, LOW);
  delay(100);
}

//Funcao para a publicacao de uma mensagem
void publishMessage(String jsonBuffer)
{
  client.publish(AWS_IOT_PUBLISH_TOPIC, jsonBuffer);
}

//Funcao para receber uma mensagem
void messageHandler(String &topic, String &payload) {
  timerWrite(timer, 0); //Reset o temporizador (alimenta o watchdog) zera o contador
  if (debug) Serial.println("incoming: " + topic + " - " + payload);

  deleteFile(SPIFFS, "/JSON.txt");
  writeFile(SPIFFS, "/JSON.txt", payload.c_str());
  timerWrite(timer, 0); //Reset o temporizador (alimenta o watchdog) zera o contador
  listDir(SPIFFS, "/", 0);

  interpretaJSON();
}

//Função para a receber e enviar mensagem pelo LoRa
void sendMessage(String outgoing) {
  LoRa.beginPacket();                   // start packet
  LoRa.write(destination);              // add destination address
  LoRa.write(localAddress);             // add sender address
  LoRa.write(msgCount);                 // add message ID
  LoRa.write(outgoing.length());        // add payload length
  LoRa.print(outgoing);                 // add payload
  LoRa.endPacket();                     // finish packet and send it
  msgCount++;                           // increment message ID
  if (msgCount == 250) msgCount = 0;
  if (debug) Serial.println("Finalizou de enviar mensagem");
}

void onReceive(int packetSize)
{
  if (packetSize == 0) return;          // if there's no packet, return

  //read packet header bytes:
  int recipient = LoRa.read();          // recipient address
  byte sender = LoRa.read();            // sender address
  byte incomingMsgId = LoRa.read();     // incoming msg ID
  byte incomingLength = LoRa.read();    // incoming msg length

  String incoming = "";

  while (LoRa.available()) {
    incoming += (char)LoRa.read();
  }

  if (incomingLength != incoming.length()) {   // check length for error
    Serial.println("error: message length does not match length");
    return;                             // skip rest of function
  }

  // if the recipient isn't this device or broadcast,
  if (recipient != localAddress && recipient != 0xFF) {
    Serial.println("This message is not for me.");
    return;                             // skip rest of function
  }

  // if message is for this device, or broadcast, print details:
  Serial.println("Received from: 0x" + String(sender, HEX));
  Serial.println("Sent to: 0x" + String(recipient, HEX));
  Serial.println("Message ID: " + String(incomingMsgId));
  Serial.println("Message length: " + String(incomingLength));
  Serial.println("Message: " + incoming);
  Serial.println("RSSI: " + String(LoRa.packetRssi()));
  Serial.println("Snr: " + String(LoRa.packetSnr()));
  //Serial.println();
  enderecoEscravo = sender;
  mensagemRecebidaLoRa = incoming;
}

void configuraDispositivosCampo(byte i, byte j, String mensagem)
{
  destination = idEndDevice[i][j] - 20000;

  Serial.println(mensagem);

  sendMessage(mensagem);      //Envia a mensagem
  timeOutLoRaCampo = millis();
  while (!(millis() - timeOutLoRaCampo >= 500))
  {
    timerWrite(timer, 0); //Reset o temporizador (alimenta o watchdog) zera o contador
    mensagemRecebidaLoRa = "";
    onReceive(LoRa.parsePacket());
    if (mensagemRecebidaLoRa == "Configurou_" + String(destination) + "_OK")
    {
      if (debug) Serial.println("End Device " + String(destination) + " conseguiu configurar");
      if (debug) Serial.println();
      break;
    }
  }
  if (millis() - timeOutLoRaCampo >= 800)
  {
    if (debug) Serial.println("Time Out ao configurar End Device " + String(destination));
    if (debug) Serial.println();
  }
}

void interpretaJSON()
{
  StaticJsonDocument<1500> doc1;
  DeserializationError err = deserializeJson(doc1, readFileString(SPIFFS, "/JSON.txt")); //Quebra o JSON para pegar as variaveis
  //Caso der erro no valor ele desconsidera os valores recebidos
  if (err) {
    if (debug) Serial.print(F("deserializeJson() failed with code "));
    if (debug) Serial.println(err.f_str());
    return;
  }
  else
  {
    timerWrite(timer, 0); //Reset o temporizador (alimenta o watchdog) zera o contador
    statusDevice = doc1["central"]["status_device"];
    if (doc1["central"]["time_send"] >= 5) tempopublicacao = doc1["central"]["time_send"];

    numeroSensoresCentral = doc1["central"]["sensors"].size();

    for (int h = 0; h < numeroSensoresCentral; h++)
    {
      sensoresCentral[h].id_sensor = doc1["central"]["sensors"][h]["id_sensor"];
      sensoresCentral[h].type = doc1["central"]["sensors"][h]["type"];
      sensoresCentral[h].ad_paramameters = doc1["central"]["sensors"][h]["ad_paramameters"];
    }

    numeroGranjas = doc1["central"]["granjas"].size();

    for (int i = 0; i < numeroGranjas; i++)
    {
      byte numeroEndDevices = doc1["central"]["granjas"][i]["iot_end"].size();
      numeroDevices[i] = numeroEndDevices; //Vai armazenar o numero de endDevices por granja
      for (int j = 0; j < numeroEndDevices; j++)
      {
        byte sensoresEndDevice = doc1["central"]["granjas"][i]["iot_end"][j]["sensors"].size();

        //Vai armazenar a quantidade de sensores em cada endDevice. Sendo a linha a granja e a coluna o end device
        numeroSensoresEndDevice[i][j] = sensoresEndDevice;
        //Armazena o ID do end device. Sendo a linha a granja e a coluna o end device
        idEndDevice[i][j] = doc1["central"]["granjas"][i]["iot_end"][j]["id_iot_end"];

        if (statusDevice == 2)
        {
          StaticJsonDocument<1000> doc2;
          String jsonBuffer = "";

          doc2["status_device"] = doc1["central"]["status_device"];
          doc2["number_sensors"] = doc1["central"]["granjas"][i]["iot_end"][j]["sensors"].size();
          doc2["id_iot_end"] = doc1["central"]["granjas"][i]["iot_end"][j]["id_iot_end"];
          serializeJson(doc2, jsonBuffer); //Escreve para o client
          configuraDispositivosCampo(i, j, jsonBuffer);
        }

        for (int k = 0; k < sensoresEndDevice; k++)
        {
          //Armazena as informação do sensor. Sendo a linha a granja, a coluna o end device, e a profundidade o sensor
          sensores[i][j][k].id_sensor = doc1["central"]["granjas"][i]["iot_end"][j]["sensors"][k]["id_sensor"];
          sensores[i][j][k].type = doc1["central"]["granjas"][i]["iot_end"][j]["sensors"][k]["type"];
          sensores[i][j][k].ad_paramameters = doc1["central"]["granjas"][i]["iot_end"][j]["sensors"][k]["ad_paramameters"];
          if (statusDevice == 2)
          {
            String jsonBuffer = "";
            serializeJson(doc1["central"]["granjas"][i]["iot_end"][j]["sensors"][k], jsonBuffer); //Escreve para o client
            configuraDispositivosCampo(i, j, jsonBuffer);
          }
        }

      }
    }
    //if (debug) imprimiInfoSensores();
  }
}

void testeJson(String payload) {
  timerWrite(timer, 0); //Reset o temporizador (alimenta o watchdog) zera o contador
  deleteFile(SPIFFS, "/JSON.txt");
  writeFile(SPIFFS, "/JSON.txt", payload.c_str());
  timerWrite(timer, 0); //Reset o temporizador (alimenta o watchdog) zera o contador
  listDir(SPIFFS, "/", 0);

  interpretaJSON();
}

void setup() {
  //pinMode(ledStatusErro, OUTPUT); //Declara pino do LED de erro como saida
  //pinMode(ledStatusCom, OUTPUT); //Declara pino do LED de comunicacao como saida
  pinMode(botaoManual, INPUT); //Declara pino do botao maual como input

  if (debug) Serial.begin(115200); //Inicializa serial
  connectAWS(); //Chama a funcao de se conectar a nuvem da AWS

  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }
  //  digitalWrite(ledStatusCom, HIGH);
  //  delay(100);
  //  digitalWrite(ledStatusCom, LOW);
  //  delay(100);

  dht.begin(); //Inicializa sensor DHT11

  timer = timerBegin(0, 80, true); //Inicia o contador progressivo
  timerAttachInterrupt(timer, &resetModule, true); //Instancia do timer, função callback, interrupção de borda
  timerAlarmWrite(timer, 5000000, true); //Instancia de timer, tempo (us),5.000.000 us = 5 segundos
  timerAlarmEnable(timer); //Habilita a interrupção

  //Define os pinos de conexão da antena LoRa
  LoRa.setPins(csPin, resetPin, irqPin);// set CS, reset, IRQ pin
  //Inicia a antena Lora
  if (!LoRa.begin(915E6)) {             // initialize ratio at 915 MHz
    if (debug)Serial.println("LoRa init failed. Check your connections.");
    digitalWrite(ledStatusErro, HIGH);
    while (true);                       // if failed, do nothing
  }
  if (debug)Serial.println("LoRa init succeeded.");
  LoRa.enableCrc();
  LoRa.setGain(1);
  LoRa.setTxPower(20);
  
  interpretaJSON();
}

void loop() {
  timerWrite(timer, 0); //Reset o temporizador (alimenta o watchdog) zera o contador

  if (LoRa.isTransmitting())
  {
    Serial.println("Não está mais transmitindo");
    if (!LoRa.begin(915E6)) ESP.restart();
  }

  unsigned long currentMillis = millis();

  //  //Solicita as variaveis de campo pelo LoRa
  if ((currentMillis - previousMillis >= ((tempopublicacao) * 1000)) and statusDevice == 1)
  {
    StaticJsonDocument<2000> doc1;
    DeserializationError err = deserializeJson(doc1, readFileString(SPIFFS, "/JSON.txt")); //Quebra o JSON para pegar as variaveis
    //Caso der erro no valor ele desconsidera os valores recebidos
    if (err) {
      if (debug) Serial.print(F("deserializeJson() failed with code "));
      return;
    }
    else
    {
      if (debug) Serial.println("deserializeJson OK");


      for (int h = 0; h < numeroSensoresCentral; h++)
      {
        doc1["central"]["sensors"][h].createNestedObject("value");
        if (doc1["central"]["sensors"][h]["id_sensor"] == 200001) doc1["central"]["sensors"][h]["value"] = dht.readTemperature();
        if (doc1["central"]["sensors"][h]["id_sensor"] == 200002) doc1["central"]["sensors"][h]["value"] = dht.readHumidity();
      }

      for (int i = 0; i < numeroGranjas; i++)
      {
        for (int j = 0; j < numeroDevices[i]; j++)
        {
          destination = idEndDevice[i][j] - 20000;
          String message = "{\"status_device\": 1}";
          sendMessage(message);
          Serial.print("Endereço enviado: ");
          Serial.println(destination);
          Serial.print("Mensagem enviada: ");
          Serial.println(message);
          timeOutLoRaCampo = millis();
          enderecoEscravo = 0;
          mensagemRecebidaLoRa = "0";
          while (!(millis() - timeOutLoRaCampo >= 800))
          {
            client.loop();//Analisa solictação do client(Nuvem AWS)
            timerWrite(timer, 0); //Reset o temporizador (alimenta o watchdog) zera o contador
            onReceive(LoRa.parsePacket()); //Analisa recebimento de algum pacote pelo LoRa

            if (enderecoEscravo == destination)
            {
              if (debug) Serial.println("Recebeu certo");
              StaticJsonDocument<300> doc2;
              DeserializationError err = deserializeJson(doc2, mensagemRecebidaLoRa); //Quebra o JSON para pegar as variaveis
              //Caso der erro no valor ele desconsidera os valores recebidos
              if (err) {
                if (debug) Serial.print(F("deserializeJson() failed with code "));
                if (debug) Serial.println(err.f_str());
              }
              else
              {
                if (debug) Serial.println("deserializeJson OK");

                for (int i = 0; i < numeroGranjas; i++)
                {
                  byte numeroEndDevices = doc1["central"]["granjas"][i]["iot_end"].size();
                  for (int j = 0; j < numeroEndDevices; j++)
                  {
                    byte sensoresEndDevice = doc1["central"]["granjas"][i]["iot_end"][j]["sensors"].size();

                    if (doc1["central"]["granjas"][i]["iot_end"][j]["id_iot_end"] == doc2["id_iot_end"])
                    {
                      for (int k = 0; k < sensoresEndDevice; k++)
                      {
                        if (doc1["central"]["granjas"][i]["iot_end"][j]["sensors"][k]["id_sensor"] == doc2["sensors"][k]["id_sensor"])
                        {
                          if (debug) Serial.println("Chegou para adicionar value");
                          doc1["central"]["granjas"][i]["iot_end"][j]["sensors"][k].createNestedObject("value");
                          doc1["central"]["granjas"][i]["iot_end"][j]["sensors"][k]["value"] = doc2["sensors"][k]["value"];
                        }
                      }

                    }

                  }

                }
              }
              break;
            }
          }
        }
      }
      String jsonBuffer = "";
      serializeJson(doc1, jsonBuffer); //Escreve para o client
      if (debug) Serial.println(jsonBuffer);
      publishMessage(jsonBuffer);
      if (debug) Serial.println("-------------------------------------");
      //digitalWrite(ledStatusCom, LOW);
      //atualizarLed = true;
      //tempLedDesligado = currentMillis;
    }
    previousMillis = currentMillis;
  }

  client.loop();//Analisa solictação do client(Nuvem AWS)

  delay(10);

  //Comando manual para começar o processo
  if (digitalRead(botaoManual) == HIGH)
  {
    testeJson(payloadJSON);
  }
}
