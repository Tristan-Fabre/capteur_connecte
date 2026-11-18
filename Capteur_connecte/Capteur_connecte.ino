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
/// La température est en dixièmes de °C (235 = 23,5 °C) pour garder une décimale sans float ;
/// l'humidité est en % entier, le DHT11 ne mesure pas plus finement.
struct __attribute__((packed)) DHTPayload 
{
  int16_t degree;
  uint8_t humidity; 
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
static_assert(sizeof(DHTPayload) == 3, "DHTPayload = 3 octets");

// ---- Variables globales ----
static const uint8_t pinDHT = 0; // Broche de données du DHT11
DHT dht(pinDHT, DHT11);
static CounterPayload counter = {0};
static DHTPayload dhtP = {0, 0};
static LedPayload led = {};

static const uint8_t ledPin = 2; // Broche de la LED RGB intégrée (WS2812)

// ---- Subjects ----
constexpr mps::Subject kSubHb = mps::makeSubject("hb_" DEVICE_NAME);
constexpr mps::Subject kSubStatusCounter = mps::makeSubject("status_" DEVICE_NAME "_counter");
constexpr mps::Subject kSubCmdResetCounter = mps::makeSubject("cmd_" DEVICE_NAME "_reset_counter");
constexpr mps::Subject kSubStatusDHT = mps::makeSubject("status_" DEVICE_NAME "_dht");
constexpr mps::Subject kSubSetLedColor = mps::makeSubject("set_" DEVICE_NAME "_led_color");
constexpr mps::Subject kSubCmdSwitchLedState = mps::makeSubject("cmd_" DEVICE_NAME "_switch_led_state");

// ---- Callbacks ----

/// Republie l'état des capteurs à l'ouverture de session, pour que l'IHM
/// affiche des valeurs sans attendre le prochain cycle de 2 s.
static void onSessionOpened() 
{
  // La passerelle ne garde rien : on republie plutôt que de supposer que l'IHM a l'état
  MPS.publishValue(kSubStatusCounter, counter);
  MPS.publishValue(kSubStatusDHT, dhtP);
}

/// Signale sur la liaison série un message perdu, pour diagnostiquer les problèmes de liaison.
static void onSendFailed(uint16_t sequenceNumber, uint32_t subjectId, mps::SendFailureReason, mps::AcknowledgementStatus) 
{
  Serial.printf("/!\\ envoi %u (subject 0x%08lX) perdu\n", sequenceNumber, (unsigned long)subjectId);
}

/// Traite cmd_<nom>_reset_counter : remet le compteur à 0 et publie le résultat,
/// afin que l'IHM affiche la valeur réelle et non celle qu'elle suppose.
static void onCmdResetCounter(const mps::FrameView& message, void*) 
{
  if (message.payloadLength != 0) return; // Contrat : cette commande n'a pas de payload
  counter.value = 0;
  MPS.publishValue(kSubStatusCounter, counter);
}

/// Traite cmd_<nom>_switch_led_state : mémorise l'état voulu de la LED (1 octet).
/// L'affichage est fait par writeLed() dans loop().
static void onCmdSwitchLedState(const mps::FrameView& message, void*)
{
  if (message.payloadLength != 1) return;

  led.isLedActive = message.payload[0];
}

/// Traite set_<nom>_led_color : mémorise la couleur voulue (R, V, B sur 3 octets).
/// La couleur est conservée même LED éteinte, pour se rallumer dans la même teinte.
static void onSetLedColor(const mps::FrameView& message, void*)
{
  if (message.payloadLength != 3) return;

  led.redValue = message.payload[0];
  led.greenValue = message.payload[1];
  led.blueValue = message.payload[2];
}

// ---- Fonctions appelées par setup/loop ----

/// Envoie un heartbeat par seconde sans bloquer (comparaison au temps écoulé, pas de delay()),
/// pour prouver à l'IHM que le code du capteur n'est pas planté.
void publishHeartbeat(){
  static uint32_t lastHeartbeatTimeMs = 0;
  static const uint16_t kHbPeriod = 1000; // ms ; l'IHM tolère 3,5 périodes sans heartbeat

  if(millis() - lastHeartbeatTimeMs >= kHbPeriod) 
  {
    lastHeartbeatTimeMs = millis();
    MPS.publishHeartbeat(kSubHb);
  }
}

/// Applique l'état mémorisé à la LED : couleur choisie si allumée, noir sinon.
/// Appelée à chaque tour de loop() pour que les commandes soient visibles immédiatement.
void writeLed()
{
  if (led.isLedActive) rgbLedWrite(ledPin, led.redValue, led.greenValue, led.blueValue);
  else rgbLedWrite(ledPin, 0, 0, 0);
}

// ---- Fonctions Arduino ----

/// Initialise la liaison série, les callbacks MPS, le client MPS, le DHT11 et la LED.
void setup() 
{
  Serial.begin(115200);
  MPS.setLogOutput(&Serial);

  MPS.setRegisteredCallback(onSessionOpened);
  MPS.setSendFailedCallback(onSendFailed);
  MPS.on(kSubCmdResetCounter, onCmdResetCounter);
  MPS.on(kSubCmdSwitchLedState, onCmdSwitchLedState);
  MPS.on(kSubSetLedColor, onSetLedColor);

  // Adresse nulle = config.h pas encore renseigné : inutile de démarrer le client
  static const uint8_t kUnsetMacAddress[6] = {0, 0, 0, 0, 0, 0};
  if(memcmp(kGatewayMacAddress, kUnsetMacAddress, 6) == 0) 
  {
    Serial.println("MPS: renseignez kGatewayMacAddress dans config.h (adresse affichee par l'IHM)");
    return;
  }

  if(!MPS.begin(kGatewayMacAddress, DEVICE_NAME, kWifiChannel)) Serial.println("MPS: demarrage impossible");
  dht.begin();
  writeLed();
}

/// Boucle principale : fait tourner MPS, envoie le heartbeat, met à jour la LED,
/// puis publie le compteur et le DHT11 toutes les 2 s. Doit rester non bloquante.
void loop() 
{
  MPS.loop();

  publishHeartbeat();
  writeLed();
  
  static uint32_t lastCounterTimeMs = 0;
  if (millis() - lastCounterTimeMs >= 2000) 
  {
    lastCounterTimeMs = millis();
    counter.value++;
    MPS.publishValue(kSubStatusCounter, counter);

    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    // Une lecture ratée renvoie NaN, dont la conversion en entier est indéfinie : on saute la publication
    if (!isnan(temperature) && !isnan(humidity))
    {
      dhtP.degree = (int16_t)(temperature * 10); // x10 pour garder la décimale ; Godot multiplie par 0,1
      dhtP.humidity = (uint8_t)humidity;         // Le DHT11 donne un % entier : pas de facteur
      MPS.publishValue(kSubStatusDHT, dhtP);
    }
  }
}