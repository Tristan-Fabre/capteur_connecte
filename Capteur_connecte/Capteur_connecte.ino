#include <DHT.h>
#include <DHT_U.h>
#include <MPS.h>
#include "config.h"

// ---- Structures de payload et filets de sécurité ----
/// Payload de status_sensor_h_counter : valeur du compteur incrémenté toutes les 2 s.
struct __attribute__((packed)) CounterPayload 
{
  uint32_t value;
};

/// Payload de status_sensor_h_dht : mesures du DHT11.
/// La température est en dixièmes de °C (235 = 23,5 °C) pour garder une décimale sans float
/// l'humidité est en % entier, le DHT11 ne mesure pas plus finement.
struct __attribute__((packed)) DhtPayload 
{
  int16_t temperatureDecimalCelciusDegrees;
  uint8_t humidityDecimalPercentage;
};

/// État courant de la LED RGB, mis à jour par les commandes de l'IHM.
/// N'est jamais publié : l'IHM doit le renvoyer à chaque connexion.
struct __attribute__((packed)) LedPayload
{
  uint8_t isLedActive; // 0 = éteinte, sinon allumée
  
  uint8_t redValue;
  uint8_t greenValue;
  uint8_t blueValue;
};

static_assert(sizeof(CounterPayload) == 4, "CounterPayload = 4 octets");
static_assert(sizeof(LedPayload) == 4, "LedPayload = 4 octets");
static_assert(sizeof(DhtPayload) == 3, "DhtPayload = 3 octets");

// ---- Variables globales ----
static const uint8_t kLedPin = 2;
static const uint8_t kDhtPin = 0;

DHT dht(kDhtPin, DHT11); //création de l'objet dht pour lire la température et l'humidité

static CounterPayload counterPayload = {0};
static DhtPayload dhtPayload = {0, 0};
static LedPayload ledPayload = {};

// ---- Subjects ----
constexpr mps::Subject kSubHb = mps::makeSubject("hb_" DEVICE_NAME);
constexpr mps::Subject kSubStatusCounter = mps::makeSubject("status_" DEVICE_NAME "_counter");
constexpr mps::Subject kSubCmdResetCounter = mps::makeSubject("cmd_" DEVICE_NAME "_reset_counter");
constexpr mps::Subject kSubStatusDht = mps::makeSubject("status_" DEVICE_NAME "_dht");
constexpr mps::Subject kSubSetLedColor = mps::makeSubject("set_" DEVICE_NAME "_led_color");
constexpr mps::Subject kSubCmdSwitchLedState = mps::makeSubject("cmd_" DEVICE_NAME "_switch_led_state");

// ---- Callbacks ----

// @brief Callback déclenché lors de l'ouverture d'une session. Elle republie l'état initial du capteur pour ne pas avoir à attendre le prochain cycle.
static void onSessionOpened() 
{
  // Republication de l'état initial du capteur, la passerelle ne garde rien
  MPS.publishValue(kSubStatusCounter, counterPayload);
  MPS.publishValue(kSubStatusDht, dhtPayload);
}

// @brief Callback déclenché lorsqu'un envoi a échoué, pour diagnostiquer les problèmes de liaison.
static void onSendFailed(uint16_t sequenceNumber, uint32_t subjectId, mps::SendFailureReason, mps::AcknowledgementStatus) 
{
  Serial.printf("/!\\ envoi %u (subject 0x%08lX) perdu\n", sequenceNumber, (unsigned long)subjectId);
}

// @brief Callback déclenché lors de la remise à zéro du compteur.
static void onCmdResetCounter(const mps::FrameView& message, void*) 
{
  if (message.payloadLength != 0) return; // ce format n'a pas de payload
  counterPayload.value = 0;

  MPS.publishValue(kSubStatusCounter, counterPayload);
}

// @brief Callback déclenché lors du changement d'état d'activation de la LED pour modifier son état. L'affichage est ensuite fait par writeLed() dans loop().
static void onCmdSwitchLedState(const mps::FrameView& message, void*)
{
  if (message.payloadLength != 1) return;

  ledPayload.isLedActive = message.payload[0];
}

// @brief Callback déclenché lors du changement de la couleur de la LED pour modifier sa couleur. Conserve la couleur de la LED même éteinte, pour se rallumer dans la même teinte.
static void onSetLedColor(const mps::FrameView& message, void*)
{
  if (message.payloadLength != 3) return;

  ledPayload.redValue = message.payload[0];
  ledPayload.greenValue = message.payload[1];
  ledPayload.blueValue = message.payload[2];
}

// ---- Fonctions appelées par setup/loop ----

// @brief La fonction envoi des heartbeats réguliers sans bloquer l'exécution du programme. (comparaison au temps écoulé sans delay())
void publishHeartbeat()
{
  static uint32_t lastHeartbeatTimeMs = 0;
  static const uint16_t kHbPeriod = 1000;

  if(millis() - lastHeartbeatTimeMs >= kHbPeriod) 
  {
    lastHeartbeatTimeMs = millis();
    
    // L'envoi de heartbeats depuis loop() assure au récepteur (IHM) que le code "métier" n'est pas planté
    MPS.publishHeartbeat(kSubHb);
  }
}

// @brief L'appel de cette fonction met à jour la couleur de la LED si elle est activée.
void writeLed()
{
  if (ledPayload.isLedActive) rgbLedWrite(kLedPin, ledPayload.redValue, ledPayload.greenValue, ledPayload.blueValue);
  else rgbLedWrite(kLedPin, 0, 0, 0);
}

// ---- Fonctions Arduino ----

// @brief La fonction est appelée une fois au démarrage. Elle s'occupe d'initialiser et d'enregistrer les données
void setup() 
{
  Serial.begin(115200); // Démarrage de la communication série à 115200 bauds (sortie des Serial.print.. par exemple)
  MPS.setLogOutput(&Serial); // Définition de la sortie des messages de log de la lib MPS

  // enregistrement des callbacks
  MPS.setRegisteredCallback(onSessionOpened);
  MPS.setSendFailedCallback(onSendFailed);

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

// @brief La fonction est exécutée en boucle, permettant d'y ajouter les évènements et les données (LED, heartbeat...). Elle publie le compteur et le DHT11 toutes les 2 secondes.
void loop() 
{
  // appel de la boucle MPS (/!\ du code bloquant dans votre boucle loop() pourrait occasionner des problèmes de connexion)
  MPS.loop();

  // Appel non bloquant de l'envoi d'HB (polling)
  publishHeartbeat();
  writeLed();
  
  static uint32_t lastCounterTimeMs = 0;
  if (millis() - lastCounterTimeMs >= 2000) 
  {
    lastCounterTimeMs = millis();
    counterPayload.value++;

    // On multiplie le résultat par 10 pour obtenir la valeur à la dixième près et côté godot on multiplie par 0.1 pour avoir la bonne précision
    dhtPayload.temperatureDecimalCelciusDegrees = (dht.readTemperature() * 10);
    dhtPayload.humidityDecimalPercentage = (dht.readHumidity() * 10);

    MPS.publishValue(kSubStatusDht, dhtPayload);
    MPS.publishValue(kSubStatusCounter, counterPayload);
  }
}