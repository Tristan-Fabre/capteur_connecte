# Capteur `sensor_h` (ESP32 + DHT11 + LED RGB)

Ce document décrit le contrat MPS du capteur. Il suffit pour écrire une tuile Godot sans lire le code Arduino.

## Généralités

- **Nom d'appareil** : `sensor_h`. La passerelle l'annonce via `on_node_online(node_id, mac, "sensor_h")`. Le `node_id` obtenu sert à filtrer les messages (`source_id == node_id`).
- **Encodage** : tous les entiers multi-octets sont en **little-endian**. En Godot : `decode_u32`, `decode_s16`, `decode_u8`.
- **Convention des noms** : `hb_` = heartbeat, `status_` = capteur → IHM, `cmd_` / `set_` = IHM → capteur.
- Le capteur **ne mémorise pas** l'état de la LED d'une session à l'autre : l'IHM doit renvoyer couleur et état à chaque (re)connexion (voir « Initialisation »).

## Résumé

| Subject | Sens | Payload | Taille |
|---|---|---|---|
| `hb_sensor_h` | capteur → IHM | non exploité | – |
| `status_sensor_h_counter` | capteur → IHM | `uint32` | 4 octets |
| `status_sensor_h_dht` | capteur → IHM | `int16` + `uint8` | 3 octets |
| `cmd_sensor_h_reset_counter` | IHM → capteur | vide | 0 octet |
| `cmd_sensor_h_switch_led_state` | IHM → capteur | `uint8` | 1 octet |
| `set_sensor_h_led_color` | IHM → capteur | `uint8` ×3 | 3 octets |

Un message dont la taille de payload ne correspond pas est **ignoré** par le capteur. L'IHM doit faire de même.

## Subjects publiés par le capteur (à souscrire)

### `hb_sensor_h` : heartbeat
- Envoyé toutes les **1 s** depuis `loop()`. Il prouve que le code du capteur tourne.
- Le payload n'a pas de signification, il ne faut pas le lire.
- Recommandation côté IHM : considérer le capteur déconnecté après **3,5 s** sans heartbeat.

### `status_sensor_h_counter` : compteur
| Offset | Type | Description |
|---|---|---|
| 0 | `uint32` | Valeur du compteur |

- Incrémenté et publié toutes les **2 s**.
- Republié immédiatement après un reset (`cmd_..._reset_counter`) et à l'ouverture de session.
- Au démarrage du capteur, il repart de 0.

### `status_sensor_h_dht` : température et humidité (DHT11)
| Offset | Type | Description |
|---|---|---|
| 0 | `int16` (signé) | Température en **dixièmes de °C** (ex. `235` = 23,5 °C) |
| 2 | `uint8` | Humidité relative en **%** (0 à 100) |

- Publié toutes les **2 s**, et à l'ouverture de session.
- Côté IHM : `température = decode_s16(0) * 0.1` et `humidité = decode_u8(2)` (pas de facteur 0,1).
- Le DHT11 a une précision d'environ ±2 °C et ±5 % : la décimale de température n'est pas significative.
- Si la lecture du DHT11 échoue, la valeur envoyée n'est pas fiable.

## Subjects reçus par le capteur (commandes)

### `cmd_sensor_h_reset_counter` : remise à zéro du compteur
- Payload **vide** (0 octet). Un payload non vide est ignoré.
- Effet : compteur = 0, puis publication immédiate de `status_sensor_h_counter`.

### `cmd_sensor_h_switch_led_state` : allumer / éteindre la LED
| Offset | Type | Description |
|---|---|---|
| 0 | `uint8` | `0` = éteinte, `1` = allumée |

- Éteinte, la LED est écrite en (0, 0, 0). La couleur choisie est conservée.
- Aucun accusé de réception : le capteur ne publie pas l'état de la LED.

### `set_sensor_h_led_color` : couleur de la LED
| Offset | Type | Description |
|---|---|---|
| 0 | `uint8` | Rouge (0 à 255) |
| 1 | `uint8` | Vert (0 à 255) |
| 2 | `uint8` | Bleu (0 à 255) |

- Prise en compte immédiate si la LED est allumée.
- Aucun accusé de réception.
- Exemple Godot : `PackedByteArray([c.r8, c.g8, c.b8])`.

## Initialisation côté IHM

Quand le capteur passe à « connecté » (premier heartbeat), la tuile doit envoyer :
1. `set_sensor_h_led_color` avec la couleur sélectionnée ;
2. `cmd_sensor_h_switch_led_state` avec l'état du bouton.

Sans cela, la LED reste éteinte (état initial = tout à 0). Les commandes sont à publier **avec accusé** (`publish(..., true)`).

## Souscriptions requises

`hb_sensor_h`, `status_sensor_h_counter`, `status_sensor_h_dht`.
Les subjects `cmd_` et `set_` sont publiés par l'IHM, pas souscrits.