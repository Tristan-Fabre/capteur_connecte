extends Tile

# ---- CONSTANTES ----
const DEVICE_NAME = "sensor_h"
const SUB_HB = "hb_" + DEVICE_NAME
const SUB_STATUS_COUNTER = "status_" + DEVICE_NAME + "_counter"
const SUB_CMD_RESET = "cmd_" + DEVICE_NAME + "_reset_counter"
const SUB_STATUS_DHT = "status_" + DEVICE_NAME + "_dht"
const SUB_CMD_SWITCH_STATE = "cmd_" + DEVICE_NAME + "_switch_led_state"
const SUB_SET_LED_COLOR = "set_" + DEVICE_NAME + "_led_color"
## Délai sans heartbeat au-delà duquel le capteur est considéré comme bloqué (3,5 périodes de 1 s).
const HB_TIMEOUT_S := 3.5
## Durée d'allumage de l'icône de heartbeat à chaque réception.
const HB_BLINK_S := 0.5

const COUNTER_OFFSET := 0
const TEMPERATURE_OFFSET := 0
const HUMIDITY_OFFSET := 2

const COUNTER_PAYLOAD_SIZE := 4
const DHT_PAYLOAD_SIZE := 3

const PERCENTAGE_DIVISION := 0.1

# ---- VARIABLES ----
## Identifiant du capteur attribué par la passerelle (-1 tant qu'inconnu).
var _node_id: int = -1
var _seconds_since_hb := INF

## État de connexion du capteur. Le setter ne déclenche la mise à jour de l'interface
## qu'au changement réel, pour éviter de renvoyer l'initialisation LED à chaque heartbeat.
var _connected: bool = false:
	set(value):
		if value == _connected: return
		_connected = value
		if _connected:
			_on_sensor_connected()
		else:
			_on_sensor_disconnected()


## Déclare les subjects que la tuile veut recevoir : sans eux, on_message ne reçoit rien.
## Seuls les subjects publiés par le capteur sont souscrits, pas les commandes.
func get_subscriptions() -> PackedStringArray:
	var subscriptions: PackedStringArray = []
	subscriptions.append(SUB_HB)
	subscriptions.append(SUB_STATUS_COUNTER)
	subscriptions.append(SUB_STATUS_DHT)
	return subscriptions


## Source principale du node_id : la passerelle annonce le capteur par son nom dès son enregistrement,
## avant ses premières publications (dont l'état republié dans onSessionOpened côté Arduino).
func on_node_online(node_id: int, _mac_address: String, device_name: String) -> void:
	if device_name != DEVICE_NAME: return
	_node_id = node_id  # Nouvel identifiant possible après un redémarrage du capteur
	%IDLabel.text = "node_id: %d" % _node_id


## Marque le capteur déconnecté quand la passerelle signale son départ,
## sans attendre le délai du heartbeat.
func on_node_offline(node_id: int, _reason_code: int) -> void:
	if node_id == _node_id: _connected = false


## Réagit à la perte de la passerelle : plus aucun capteur n'est joignable
## et les identifiants peuvent être redistribués à son retour.
func on_gateway_reachability_changed(is_gateway_reachable: bool) -> void:
	if is_gateway_reachable: return
	_connected = false
	_node_id = -1  # La passerelle peut redistribuer les identifiants en redémarrant


## Point d'entrée des messages reçus : route le heartbeat et affiche les mesures
## du capteur (compteur, température, humidité). Ignore les payloads de mauvaise taille.
func on_message(subject_name: String, source_id: int, payload: PackedByteArray) -> void:
	if subject_name == SUB_HB: _on_hb(source_id)
	if source_id != _node_id: return # Plusieurs capteurs peuvent partager les mêmes subjects
	
	match subject_name:
		SUB_STATUS_COUNTER:
			if payload.size() != COUNTER_PAYLOAD_SIZE: return # Le contrat : 4 octets, sinon on ignore
			%CounterLabel.text = "Counter: %d" % payload.decode_u32(COUNTER_OFFSET)
		SUB_STATUS_DHT:
			if payload.size() != DHT_PAYLOAD_SIZE: return
			# Température signée (int16) en dixièmes de °C ; humidité en % entier
			%TemperatureLabel.text = "Temperature: %.1f °C" % (payload.decode_u16(TEMPERATURE_OFFSET) * PERCENTAGE_DIVISION)
			%HumidityLabel.text = "Humidity: %.1f" % (payload.decode_u8(HUMIDITY_OFFSET) * PERCENTAGE_DIVISION) +"%"


## Fait avancer le chrono depuis le dernier heartbeat, anime l'icône de heartbeat
## et déclare le capteur déconnecté s'il se tait trop longtemps.
func _process(delta: float) -> void:
	_seconds_since_hb += delta
	%HeartbeatIcon.state = _seconds_since_hb < HB_BLINK_S
	if _connected and _seconds_since_hb >= HB_TIMEOUT_S:
		_connected = false  # Plus de heartbeat : code du capteur bloqué ou capteur hors de portée


## Met l'interface en mode connecté et renvoie l'état de la LED au capteur,
## car celui-ci ne le mémorise pas d'une session à l'autre.
func _on_sensor_connected():
	%IDLabel.text = "node_id: %d" % _node_id
	%ConnectedIcon.state = true
	
	_send_led_color(%ColorPickerButton.color)
	_send_led_state(%ButtonLedActive.button_pressed)


## Met l'interface en mode déconnecté : efface les mesures pour ne pas
## afficher des valeurs périmées comme si elles étaient actuelles.
func _on_sensor_disconnected():
	%IDLabel.text = "node_id: ∅"
	%TemperatureLabel.text = "Temperature: ∅" 
	%HumidityLabel.text = "Humidity: ∅"
	%ConnectedIcon.state = false


## Traite un heartbeat : (re)connecte le capteur et relance la temporisation.
## [param source_id] Identifiant de l'appareil émetteur.
func _on_hb(source_id: int):
	# Secours si la tuile n'a pas vu on_node_online (IHM lancée après le capteur, par exemple)
	if _node_id == -1: _node_id = source_id
	if source_id != _node_id: return
	_seconds_since_hb = 0.0
	_connected = true


## Envoie la couleur de la LED au capteur (3 octets R, V, B), avec accusé de réception
## pour que la commande ne se perde pas silencieusement.
func _send_led_color(led_color: Color) -> void:
	var payload = PackedByteArray([led_color.r8, led_color.g8, led_color.b8])
	publish(SUB_SET_LED_COLOR, payload, true)


## Envoie au capteur l'état voulu de la LED (1 octet : 0 éteinte, 1 allumée), avec accusé.
func _send_led_state(is_led_active: bool) -> void:
	var payload = PackedByteArray([is_led_active])
	publish(SUB_CMD_SWITCH_STATE, payload, true)

## Initialise la tuile : abonnement aux subjects puis connexion des boutons aux commandes.
func _ready() -> void:
	super() # Obligatoire : c'est lui qui abonne la tuile
	
	%ResetButton.pressed.connect(func(): publish(SUB_CMD_RESET, PackedByteArray(), true))
	%ColorPickerButton.color_changed.connect(func (led_color: Color): _send_led_color(led_color))
	%ButtonLedActive.toggled.connect(func(is_led_active: bool): _send_led_state(is_led_active))
