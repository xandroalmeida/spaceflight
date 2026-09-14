class_name AudioDirector
extends Node
## O som da nave (regras 36, 37).
##
## ## A regra que este arquivo existe para respeitar
##
## No vácuo não há som. O que há é ESTRUTURA: o motor e os thrusters estão
## presos ao mesmo casco em que o piloto está sentado, e o casco conduz. Por
## isso o motor se ouve de dentro e NÃO se ouve de fora -- e é por isso que
## `set_interior(false)` cala tudo o que é da nave e deixa apenas a interface.
##
## Nenhum som atravessa o espaço aqui. Nem explosão, nem passagem, nem motor de
## outra nave. Se um dia houver música, ela entra por outro barramento.
##
## ## O volume do motor segue o EMPUXO
##
## Pela mesma razão que a pluma: com o tanque vazio a tecla continua a funcionar
## e o empuxo é zero. Um som que seguisse o acelerador continuaria a rugir sobre
## um motor apagado.

const CLIP_PATHS := {
	"engine": "res://assets/audio/engine_loop.wav",
	"ventilation": "res://assets/audio/ventilation.wav",
	"rcs": "res://assets/audio/rcs_thump.wav",
	"switch": "res://assets/audio/switch_click.wav",
	"button": "res://assets/audio/button_press.wav",
	"warning": "res://assets/audio/warning_tone.wav",
	"notify": "res://assets/audio/computer_notify.wav",
}

const ENGINE_REFERENCE_N := 200_000.0

var master_volume := 0.8
var effects_volume := 0.8

var _clips: Dictionary = {}
var _engine_player: AudioStreamPlayer
var _ventilation_player: AudioStreamPlayer
var _one_shots: Array[AudioStreamPlayer] = []
var _one_shot_index := 0
var _interior := true
var _engine_level := 0.0
var _rcs_cooldown := 0.0


func _ready() -> void:
	for key in CLIP_PATHS:
		var path: String = CLIP_PATHS[key]
		if not ResourceLoader.exists(path):
			continue
		var stream := load(path)
		if stream is AudioStreamWAV and (key == "engine" or key == "ventilation"):
			# O laço é ativado aqui e não num `.import` versionado: o Godot gera
			# o `.import` na primeira abertura e um arquivo gerado no repositório
			# é uma cópia que envelhece. A propriedade fica onde ela é lida.
			var wav := stream as AudioStreamWAV
			wav.loop_mode = AudioStreamWAV.LOOP_FORWARD
			wav.loop_begin = 0
			wav.loop_end = wav.data.size() / 2      # 16 bits, mono
		_clips[key] = stream

	_engine_player = _make_player()
	_ventilation_player = _make_player()
	# Oito reprodutores em rodízio para os disparos. Um só cortaria o clique
	# anterior a cada clique, e doze thrusters acendendo juntos são doze
	# disparos no mesmo quadro.
	for i in range(8):
		_one_shots.append(_make_player())

	if _clips.has("ventilation"):
		_ventilation_player.stream = _clips["ventilation"]
		_ventilation_player.play()
	if _clips.has("engine"):
		_engine_player.stream = _clips["engine"]
		_engine_player.play()
	_apply_volumes()


func _make_player() -> AudioStreamPlayer:
	var player := AudioStreamPlayer.new()
	add_child(player)
	return player


func _process(delta: float) -> void:
	_rcs_cooldown = maxf(_rcs_cooldown - delta, 0.0)


## `thrust_n` é o empuxo real do core.
func set_engine_thrust(thrust_n: float) -> void:
	var wanted := clampf(thrust_n / ENGINE_REFERENCE_N, 0.0, 1.0)
	# Uma constante de tempo de 120 ms na subida e na descida. Sem ela, o corte
	# do motor é um degrau e um degrau num som grave estala.
	_engine_level = lerpf(_engine_level, wanted, 0.12)
	if _engine_player.stream == null:
		return
	var level := _engine_level if _interior else 0.0
	_engine_player.volume_db = linear_to_db(maxf(level * master_volume * effects_volume, 0.0001))
	# O tom sobe um pouco com o empuxo: 0,92x em marcha lenta, 1,08x no máximo.
	# É um som mais cheio, e é a única coisa aqui que não corresponde a nada
	# físico -- dito em voz alta em vez de embutido.
	_engine_player.pitch_scale = 0.92 + 0.16 * _engine_level


func rcs_fired(count: int) -> void:
	if not _interior or count <= 0 or _rcs_cooldown > 0.0:
		return
	# Um golpe por disparo, não por bico: doze cliques idênticos no mesmo
	# milissegundo somam-se num estalo e não em doze golpes.
	_rcs_cooldown = 0.09
	_play("rcs", 0.55 + 0.05 * float(mini(count, 6)),
		0.92 + 0.03 * float(mini(count, 4)))


func switch_flipped() -> void:
	_play("switch", 0.5, 1.0)


func button_pressed() -> void:
	_play("button", 0.45, 1.0)


func warning() -> void:
	_play("warning", 0.55, 1.0)


func notify() -> void:
	_play("notify", 0.5, 1.0)


func set_interior(inside: bool) -> void:
	## Regra 36: na câmera externa, por padrão, não há som propagado pelo espaço.
	## A interface continua a soar porque ela não está no espaço -- está no
	## monitor de quem joga.
	_interior = inside
	_apply_volumes()


func set_volumes(master: float, effects: float) -> void:
	master_volume = clampf(master, 0.0, 1.0)
	effects_volume = clampf(effects, 0.0, 1.0)
	_apply_volumes()


func _apply_volumes() -> void:
	var ambient := (0.30 if _interior else 0.0) * master_volume * effects_volume
	_ventilation_player.volume_db = linear_to_db(maxf(ambient, 0.0001))


func _play(key: String, level: float, pitch: float) -> void:
	if not _clips.has(key):
		return
	var player := _one_shots[_one_shot_index]
	_one_shot_index = (_one_shot_index + 1) % _one_shots.size()
	player.stream = _clips[key]
	player.volume_db = linear_to_db(maxf(level * master_volume * effects_volume, 0.0001))
	player.pitch_scale = pitch
	player.play()
