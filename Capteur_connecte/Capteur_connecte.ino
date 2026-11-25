#include <DHT.h>
#include <DHT_U.h>

#include <MPS.h>
#include "config.h"

// ---- Structures de payload et filets de sécurité ----
// placer ici vos structures de payloads, par exemple :
// struct __attribute__((packed)) SomePayload { // __attribute__((packed)) permet de contrôler la disposition des octets de celle-ci
//   uint8_t someVariableA;
//   int16_t someVariableB;
// };
// static_assert(sizeof(SomePayload) == 3, "SomePayload = 3 octets"); // Filet de sécurité à la compilation



struct __attribute__((packed)) CounterPayload 
{
  uint32_t value;
};

struct __attribute__((packed)) DHTPayload 
{
  int16_t degree;
  uint8_t humidity; 
};
struct __attribute__((packed)) LedPayload
{
  uint8_t isLedActive;
  
  uint8_t redValue;
  uint8_t greenValue;
  uint8_t blueValue;
};

static_assert(sizeof(CounterPayload) == 4, "CounterPayload = 4 octets");
static_assert(sizeof(LedPayload) == 4, "LedPayload = 4 octets");
static_assert(sizeof(DHTPayload) == 3, "TemperaturePayload = 3 octets");
// ---- Variables globales ----
// placez ici vos variables à partager entre plusieurs contextes (ex : setup, loop, callbacks)
static CounterPayload counter = {0};
static DHTPayload dhtP = {0, 0};
static LedPayload led = {};

static const uint8_t ledPin = 2;
static const uint8_t pinDHT = 0; //variable pour la sortie de pin du DHT
DHT dht(pinDHT, DHT11); //création de l'objet dht pour lire la température et l'humidité

// ---- Subjects ----
constexpr mps::Subject kSubHb = mps::makeSubject("hb_" DEVICE_NAME);
// ... ajoutez vos sujets ici sur le même schéma que le heartbeat, mais en précisant la donnée envoyée,
// par exemple : `constexpr mps::Subject kSubStatusSomeData = mps::makeSubject("status_" DEVICE_NAME "_some_data");`
// ou : `constexpr mps::Subject kSubCmdSomeOrder = mps::makeSubject("cmd_" DEVICE_NAME "_some_order");`
constexpr mps::Subject kSubStatusCounter = mps::makeSubject("status_" DEVICE_NAME "_counter");
constexpr mps::Subject kSubCmdResetCounter = mps::makeSubject("cmd_" DEVICE_NAME "_reset_counter");
constexpr mps::Subject kSubStatusDHT = mps::makeSubject("status_" DEVICE_NAME "_dht");
constexpr mps::Subject kSubSetLedColor = mps::makeSubject("set_" DEVICE_NAME "_led_color");
constexpr mps::Subject kSubCmdSwitchLedState = mps::makeSubject("cmd_" DEVICE_NAME "_switch_led_state");

// ---- Callbacks ----
// Appelé lors de l'ouverture de la session (callback)
static void onSessionOpened() 
{
  // Republication de l'état initial du capteur, la passerelle ne garde rien
  MPS.publishValue(kSubStatusCounter, counter);
  MPS.publishValue(kSubStatusDHT, dhtP);
}

// Appelé en cas d'échec d'envoi (callback)
static void onSendFailed(uint16_t sequenceNumber, uint32_t subjectId, mps::SendFailureReason, mps::AcknowledgementStatus) 
{
  Serial.printf("/!\\ envoi %u (subject 0x%08lX) perdu\n", sequenceNumber, (unsigned long)subjectId);
}

// ... ajoutez vos callbacks ici, ex : 
// static void onCmdSomeOrder(const mps::FrameView& message, void*) {
//   SomePayload payload;
//   if(!mps::readPayloadAs(message, &payload)) return;
//   Serial.printf("Donnée : %02X\n", payload.someVariableA);
// }

static void onCmdResetCounter(const mps::FrameView& message, void*) 
{
  if (message.payloadLength != 0) return; // ce format n'a pas de payload
  counter.value = 0;
  MPS.publishValue(kSubStatusCounter, counter); // l'IHM voit le résultat réel
}

static void onCmdSwitchLedState(const mps::FrameView& message, void*)
{
  if (message.payloadLength != 1) return;

  led.isLedActive = message.payload[0];
}

static void onSetLedColor(const mps::FrameView& message, void*)
{
  if (message.payloadLength != 3) return;

  led.redValue = message.payload[0];
  led.greenValue = message.payload[1];
  led.blueValue = message.payload[2];
}

// ---- Fonctions appelées par setup/loop ----
// envoi de heartbeats réguliers sans bloquer (comparaison au temps écoulé sans delay())
void publishHeartbeat(){
  static uint32_t lastHeartbeatTimeMs = 0;
  static const uint16_t kHbPeriod = 1000;

  if(millis() - lastHeartbeatTimeMs >= kHbPeriod) 
  {
    lastHeartbeatTimeMs = millis();
    
    // L'envoi de heartbeats depuis loop() assure au récepteur (IHM) que le code "métier" n'est pas planté
    MPS.publishHeartbeat(kSubHb);
  }
}

void writeLed()
{
  if (led.isLedActive) rgbLedWrite(ledPin, led.redValue, led.greenValue, led.blueValue);
  else rgbLedWrite(ledPin, 0, 0, 0);
}

// ---- Fonctions Arduino ----
// Exécutée une fois au démarrage
void setup() 
{
  Serial.begin(115200); // Démarrage de la communication série à 115200 bauds (sortie des Serial.print.. par exemple)
  MPS.setLogOutput(&Serial); // Définition de la sortie des messages de log de la lib MPS

  // enregistrement des callbacks
  MPS.setRegisteredCallback(onSessionOpened);
  MPS.setSendFailedCallback(onSendFailed);
  // ... ajoutez vos callbacks à l'aide de MPS.on(const mps::Subject& subject, mps::MessageHandler handler, void* userContext = nullptr)
  // par exemple :
  // MPS.on(kSubCmdSomeOrder, onCmdSomeOrder);
  MPS.on(kSubCmdResetCounter, onCmdResetCounter);
  MPS.on(kSubCmdSwitchLedState, onCmdSwitchLedState);
  MPS.on(kSubSetLedColor, onSetLedColor);

  // constante d'adresse vide pour vérifier que l'adresse a été définie dans config.h
  static const uint8_t kUnsetMacAddress[6] = {0, 0, 0, 0, 0, 0};
  if(memcmp(kGatewayMacAddress, kUnsetMacAddress, 6) == 0) 
  {
    Serial.println("MPS: renseignez kGatewayMacAddress dans config.h (adresse affichee par l'IHM)");
    return;
  }

  // démarrage du client MPS
  if(!MPS.begin(kGatewayMacAddress, DEVICE_NAME, kWifiChannel)) Serial.println("MPS: demarrage impossible");
  dht.begin();
  writeLed();
}

// Exécutée en boucle (= while(true))
void loop() 
{
  // appel de la boucle MPS (/!\ du code bloquant dans votre boucle loop() pourrait occasionner des problèmes de connexion)
  MPS.loop();

  // Appel non bloquant de l'envoi d'HB (polling)
  publishHeartbeat();
  writeLed();
  
  // ... ajoutez vos envois de données et autres comportements **non bloquants**
  // par exemple :
  // static uint32_t lastDataStatusTimeMs = 0;
  // if(millis() - lastDataStatusTimeMs >= 2000){
  //   lastDataStatusTimeMs = millis();
  //   SomePayload payload = {0, 0};
  //   payload.someVariableA = digitalRead(3);
  //   payload.someVariableB = analogRead(5);
  //   MPS.publishValue(kSubStatusSomeData, payload);
  // }
  static uint32_t lastCounterTimeMs = 0;
  if (millis() - lastCounterTimeMs >= 2000) 
  {
    lastCounterTimeMs = millis();
    counter.value++;

    MPS.publishValue(kSubStatusCounter, counter);
    //on multiplie par 10 pour obtenir la valeur à la dixième près et côté godot on multiplie par 0.1 pour avoir la bonne précision
    dhtP.degree = (dht.readTemperature() * 10);
    dhtP.humidity = (dht.readHumidity()*10);
    MPS.publishValue(kSubStatusDHT, dhtP);
  }
}
