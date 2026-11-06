# capteur_connecte
Création d'un capteur connecté ainsi qu'une IHM avec Godot.

constexpr mps::Subject kSubStatusCounter = mps::makeSubject("status_" DEVICE_NAME "_counter");
constexpr mps::Subject kSubCmdResetCounter = mps::makeSubject("cmd_" DEVICE_NAME "_reset_counter");
constexpr mps::Subject kSubSetLEDColor = mps::makeSubject("set_" DEVICE_NAME "_led_color");
constexpr mps::Subject kSubCmdSwitchLEDState = mps::makeSubject("cmd_" DEVICE_NAME "_led_state");

Le subject constexpr mps::Subject kSubStatusDHT = mps::makeSubject("status_" DEVICE_NAME "_dht"); retourne la structure DHTPayload possédant les valeurs degree pour la température et humidity pour l'humidité, étant un int16_t pour degree et un uint8_t pour l'humidity, la taille de la structure est de 3 octets