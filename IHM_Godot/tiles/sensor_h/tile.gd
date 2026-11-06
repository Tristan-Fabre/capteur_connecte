extends Tile

# ---- CONSTANTES ----
const DEVICE_NAME = "sensor_h"
const SUB_HB = "hb_" + DEVICE_NAME
const SUB_STATUS_COUNTER = "status_" + DEVICE_NAME + "_counter"
const SUB_CMD_RESET = "cmd_" + DEVICE_NAME + "_reset_counter"
const SUB_STATUS_DHT = "status_"+DEVICE_NAME+"_dht"
const SUB_CMD_SWITCH_STATE = "cmd_" + DEVICE_NAME + "_switch_led_state"
const SUB_SET_LED_COLOR = "set_" + DEVICE_NAME + "_led_color"
## Délai sans heartbeat au-delà duquel le capteur est considéré comme bloqué (3,5 périodes de 1 s).
const HB_TIMEOUT_S := 3.5
## Durée d'allumage de l'icône de heartbeat à chaque réception.
const HB_BLINK_S := 0.5

# ---- VARIABLES ----
var _node_id: int = -1
var _seconds_since_hb := INF
# Variable "_connected" avec setter personnalisé pour appeler la MAJ de l'UI au changement d'état
var _connected: bool = false:
	set(value):
		if value == _connected: return
		_connected = value
		if _connected:
			_on_sensor_connected()
		else:
			_on_sensor_disconnected()

func get_subscriptions() -> PackedStringArray:
	var subscriptions: PackedStringArray = []
	# sans ces lignes, on_message ne reçoit rien
	subscriptions.append(SUB_HB)
	subscriptions.append(SUB_STATUS_COUNTER) # sans cette ligne, on_message ne reçoit rien
	subscriptions.append(SUB_STATUS_DHT)
	return subscriptions


## Source principale du node_id : la passerelle annonce le capteur par son nom dès son enregistrement,
## avant ses premières publications (dont l'état republié dans onSessionOpened côté Arduino).
func on_node_online(node_id: int, _mac_address: String, device_name: String) -> void:
	if device_name != DEVICE_NAME: return
	_node_id = node_id  # nouvel identifiant possible après un redémarrage du capteur
	%IDLabel.text = "node_id: %d" % _node_id


func on_node_offline(node_id: int, _reason_code: int) -> void:
	if node_id == _node_id: _connected = false


func on_gateway_reachability_changed(is_gateway_reachable: bool) -> void:
	if is_gateway_reachable: return
	_connected = false
	_node_id = -1  # la passerelle peut redistribuer les identifiants en redémarrant


func on_message(subject_name: String, source_id: int, _payload: PackedByteArray) -> void:
	if subject_name == SUB_HB: _on_hb(source_id)
	if source_id != _node_id: return # ne traitez que les messages dédiés à votre capteur
	
	# Ajoutez vos messages à la suite
	match subject_name:
		SUB_STATUS_COUNTER:
			if _payload.size() != 4: return # le contrat : 4 octets, sinon on ignore
			%CounterLabel.text = "Counter: %d" % _payload.decode_u32(0)
		SUB_STATUS_DHT:
			if _payload.size() != 3: return
			%TemperatureLabel.text = "Temperature: %.1f °C" % (_payload.decode_u16(0)*0.1)
			%HumidityLabel.text = "Humidity: %.1f" % (_payload.decode_u8(2)*0.1) +"%"
	

func _process(delta: float) -> void:
	_seconds_since_hb += delta
	%HeartbeatIcon.state = _seconds_since_hb < HB_BLINK_S
	if _connected and _seconds_since_hb >= HB_TIMEOUT_S:
		_connected = false  # plus de heartbeat : code du capteur bloqué ou capteur hors de portée


func _on_sensor_connected():
	%IDLabel.text = "node_id: %d" % _node_id
	%ConnectedIcon.state = true
	
	# Envoi des données pour l'initialisation
	_send_led_color(%ColorPickerButton.color)
	_send_led_state(%ButtonLedActive.button_pressed)


func _on_sensor_disconnected():
	%IDLabel.text = "node_id: ∅"
	%TemperatureLabel.text = "Temperature: ∅" 
	%HumidityLabel.text = "Humidity: ∅"
	%ConnectedIcon.state = false
	


## @brief Traite un heartbeat : (re)connecte le capteur et relance la temporisation.
## @param source_id  Identifiant de l'appareil émetteur.
func _on_hb(source_id: int):
	# Secours si la tuile n'a pas vu on_node_online (IHM lancée après le capteur, par exemple)
	if _node_id == -1: _node_id = source_id
	if source_id != _node_id: return
	_seconds_since_hb = 0.0
	_connected = true
	
func _send_led_color(led_color: Color) -> void:
	var payload = PackedByteArray([led_color.r8, led_color.g8, led_color.b8])
	publish(SUB_SET_LED_COLOR, payload, true)

func _send_led_state(is_led_active: bool) -> void:
	var payload = PackedByteArray([is_led_active])
	publish(SUB_CMD_SWITCH_STATE, payload, true)

## @brief Initialise la tuile
func _ready() -> void:
	super() # obligatoire : c'est lui qui abonne la tuile
	
	# Connexion des signaux
	%ResetButton.pressed.connect(func(): publish(SUB_CMD_RESET, PackedByteArray(), true))
	%ColorPickerButton.color_changed.connect(func (led_color: Color): _send_led_color(led_color))
	%ButtonLedActive.toggled.connect(func(is_led_active: bool): _send_led_state(is_led_active))
