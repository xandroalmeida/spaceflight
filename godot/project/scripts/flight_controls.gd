class_name FlightControls
extends RefCounted
## O que o piloto comanda, e nada mais (regras 14, 15, 28).
##
## Cada tecla vira um PEDIDO ao core: um torque, uma força, um acelerador, um
## modo de apontamento. Não há caminho nenhum daqui até a orientação ou até a
## velocidade -- o RCS queima propelente para girar a nave, o motor queima
## propelente para acelerá-la, e as duas coisas acontecem dentro do integrador.
##
## O acelerador muda ENTRE quadros, nunca dentro de um passo de propagação:
## abri-lo no meio de um passo seria uma descontinuidade na derivada, que é a
## mesma razão pela qual as queimas planejadas são partidas nas suas épocas de
## ignição (`docs/architecture/navigation.md` seção 4).

## Torque de comando manual, em N·m. É aproximadamente o que o RCS modelado
## entrega; pedir mais só faz o alocador saturar, o que é visível no instrumento.
const MANUAL_TORQUE := 400.0
## Força de comando de translação, em N. Dois bicos de 180 N por eixo.
const MANUAL_FORCE := 360.0

const WARP_LEVELS := [1.0, 10.0, 100.0, 1000.0, 10000.0, 100000.0, 1.0e6, 1.0e7, 1.0e8]
const THROTTLE_TRIM := 0.5    ## por segundo, com Shift/Ctrl mantidos

## Quanto tempo `Shift` (ou `Ctrl`) tem de ficar sozinho antes de começar a
## mexer no acelerador.
##
## ⚠️ Isto existe por causa de uma colisão real no mapa de teclas da regra 13, que
## a própria regra propõe: `Shift` abre o acelerador E `Shift+P` é retrógrado.
## Sem isto, pedir retrógrado abria o acelerador de passagem -- uns 5 % por
## toque, o suficiente para uma nave em espera começar a acelerar sem que
## ninguém tenha pedido.
##
## 0,2 s é mais do que qualquer acorde `Shift+tecla` demora e menos do que
## alguém espera antes de achar que o acelerador está avariado. E, se um comando
## modificado disparar durante o toque, o trim fica bloqueado até o modificador
## ser solto -- ver `block_throttle_trim`.
const TRIM_ARM_SECONDS := 0.20

var simulation: SpaceflightSimulation
var throttle := 0.0
var rcs_enabled := true
var warp_index := 0
var paused := false

var _last_torque := Vector3.ZERO
var _last_force := Vector3.ZERO
var _trim_hold := 0.0
var _trim_blocked := false

signal message(text: String, level: int)
signal rcs_fired()
signal engine_changed(running: bool)


func _init(sim: SpaceflightSimulation) -> void:
	simulation = sim


func warp() -> float:
	return WARP_LEVELS[warp_index]


## Chamado todo quadro, antes de `advance`.
func apply(_delta: float) -> void:
	if simulation == null:
		return

	var torque := Vector3.ZERO
	var force := Vector3.ZERO

	if rcs_enabled:
		# Arfagem em torno de +y do corpo, guinada em torno de +z, rolagem em
		# torno de +x. O nariz é +x, então é isto e não outra convenção; um sinal
		# trocado aqui faz o piloto voar com o controle invertido nas mãos e nada
		# o acusa a não ser voar.
		torque.y += Input.get_action_strength("pitch_up") * MANUAL_TORQUE
		torque.y -= Input.get_action_strength("pitch_down") * MANUAL_TORQUE
		torque.z += Input.get_action_strength("yaw_left") * MANUAL_TORQUE
		torque.z -= Input.get_action_strength("yaw_right") * MANUAL_TORQUE
		torque.x += Input.get_action_strength("roll_right") * MANUAL_TORQUE
		torque.x -= Input.get_action_strength("roll_left") * MANUAL_TORQUE

		force.x += Input.get_action_strength("translate_forward") * MANUAL_FORCE
		force.x -= Input.get_action_strength("translate_back") * MANUAL_FORCE
		force.y += Input.get_action_strength("translate_left") * MANUAL_FORCE
		force.y -= Input.get_action_strength("translate_right") * MANUAL_FORCE
		force.z += Input.get_action_strength("translate_up") * MANUAL_FORCE
		force.z -= Input.get_action_strength("translate_down") * MANUAL_FORCE

	simulation.set_manual_torque(torque)
	simulation.set_manual_translation(force)

	var was_firing := _last_torque.length() > 0.0 or _last_force.length() > 0.0
	var firing := torque.length() > 0.0 or force.length() > 0.0
	if firing and not was_firing:
		rcs_fired.emit()
	_last_torque = torque
	_last_force = force

	# Shift e Ctrl mantidos aparam o acelerador continuamente. O trim é por
	# SEGUNDO e não por quadro: a 30 fps e a 120 fps o acelerador tem de subir na
	# mesma velocidade, ou a taxa de quadros passa a ser um controle de voo.
	var trim := 0.0
	if Input.is_action_pressed("throttle_up"):
		trim += THROTTLE_TRIM
	if Input.is_action_pressed("throttle_down"):
		trim -= THROTTLE_TRIM

	if trim == 0.0:
		# O modificador foi solto: o bloqueio acaba aqui e o contador zera.
		_trim_hold = 0.0
		_trim_blocked = false
		return
	_trim_hold += _delta
	if _trim_blocked or _trim_hold < TRIM_ARM_SECONDS:
		return
	set_throttle(throttle + trim * _delta)


## Chamado quando um comando com modificador dispara: aquele `Shift` era parte de
## um acorde e não um pedido de acelerador.
func block_throttle_trim() -> void:
	_trim_blocked = true


## O que o RCS está fazendo AGORA (regra 15: "ou combinação equivalente
## existente"). Um seletor ROTAÇÃO/TRANSLAÇÃO que trancasse metade dos controles
## seria pior de pilotar e diria menos: este rótulo diz o que a nave está a
## fazer, não o que ela poderia.
##
## `firing` é quantos bicos o ALOCADOR abriu, e ele é um argumento e não um
## detalhe interno de propósito. A primeira versão derivava o rótulo só dos
## comandos manuais e escrevia "IDLE" enquanto quatro thrusters queimavam para o
## piloto automático -- exatamente a mentira que a regra 15 proíbe, cometida no
## texto em vez de no desenho. O rótulo tem de sair da mesma fonte que as chamas.
func rcs_activity(firing: int = 0) -> String:
	if not rcs_enabled:
		return "OFF"
	var rotating := _last_torque.length() > 0.0
	var translating := _last_force.length() > 0.0
	if rotating and translating:
		return "ROT+TRANS"
	if rotating:
		return "ROTATION"
	if translating:
		return "TRANSLATION"
	if firing > 0:
		# Ninguém carregou numa tecla e há bicos abertos: quem está a pilotar é o
		# controlador de atitude, dentro do core.
		return "AUTOPILOT"
	return "IDLE"


func set_throttle(value: float) -> void:
	var wanted := clampf(value, 0.0, 1.0)
	if is_equal_approx(wanted, throttle):
		return
	var was_running := throttle > 0.0
	throttle = wanted
	simulation.set_throttle(throttle)
	if (throttle > 0.0) != was_running:
		engine_changed.emit(throttle > 0.0)


func toggle_rcs() -> void:
	rcs_enabled = not rcs_enabled
	if not rcs_enabled:
		simulation.set_manual_torque(Vector3.ZERO)
		simulation.set_manual_translation(Vector3.ZERO)
		# Desligar o RCS tem de desligar o apontamento também. O controlador de
		# atitude vive DENTRO do core e continuaria a pedir torque; um interruptor
		# que apagasse o desenho das chamas e deixasse o propelente a sair seria
		# exatamente a mentira que a regra 15 proíbe, só que ao contrário.
		simulation.set_pointing_mode("")
	message.emit("RCS %s" % ("ENABLED" if rcs_enabled else "DISABLED"),
		MessageLog.Level.INFO)


func point(mode: String) -> void:
	if not rcs_enabled and mode != "":
		message.emit("RCS DISABLED -- pointing unavailable", MessageLog.Level.WARNING)
		return
	if not simulation.set_pointing_mode(mode):
		# Uma missão armada pilota a nave, e a simulação RECUSA o comando em vez
		# de aceitá-lo e sobrescrevê-lo no quadro seguinte. Dizê-lo em voz alta é
		# a diferença entre um cockpit que ignora o piloto e um que diz quem está
		# com os controles.
		message.emit(simulation.get_last_error(), MessageLog.Level.WARNING)
		return
	message.emit("ATTITUDE %s" % (mode.to_upper() if mode != "" else "HOLD"),
		MessageLog.Level.INFO)


func step_warp(direction: int) -> void:
	var wanted := clampi(warp_index + direction, 0, WARP_LEVELS.size() - 1)
	if wanted == warp_index:
		return
	warp_index = wanted
	simulation.set_time_warp(warp())
	# Nunca em silêncio (regra 28).
	message.emit("TIME WARP %s" % Fmt.warp(warp()), MessageLog.Level.INFO)


func set_paused(value: bool) -> void:
	## Pausar é parar o TEMPO DE SIMULAÇÃO, e só ele: a câmera continua a andar e
	## a interface continua a responder (regra 69). Quem faz isso é `flight.gd`,
	## que deixa de chamar `advance()`.
	##
	## NÃO é feito pondo o warp em zero, e a razão é que `SimulationClock` recusa
	## um warp que não seja estritamente positivo -- com razão: um relógio parado
	## não é um relógio lento, e a diferença entre "a simulação está pausada" e "o
	## tempo está a passar mil vezes devagar" é uma que o core não deve ter de
	## adivinhar a partir de um fator.
	paused = value


func cycle_engine_mode() -> void:
	simulation.cycle_engine_mode()
	message.emit("ENGINE %s" % simulation.get_engine_mode(), MessageLog.Level.INFO)
