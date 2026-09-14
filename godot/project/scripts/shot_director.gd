class_name ShotDirector
extends RefCounted
## O roteiro do §83, fotografado (regra 60).
##
## Voa a demonstração inteira sozinho -- órbita terrestre, vista externa, motor,
## RCS, seleção de alvo, plano, execução, chegada, órbita lunar -- e guarda uma
## imagem em cada ponto. É a evidência do milestone, e é REPRODUZÍVEL: o mesmo
## comando produz a mesma sequência, porque cada passo é uma condição sobre o
## estado da simulação e não um número de quadros escolhido à mão.
##
## ⚠️ As imagens são EVIDÊNCIA, não oráculo. Elas provam que a cena põe alguma
## coisa na tela nos momentos certos; se aquilo está bonito é julgamento humano e
## este arquivo não opina (regra 60).
##
##     SPACEFLIGHT_M7_SHOTS=docs/validation/m7 godot --path godot/project

var flight: Node
var directory := ""

var _step := 0
var _settle := 0
var _hold := 0

## Quantos quadros esperar DEPOIS de a condição ser satisfeita, antes de
## fotografar.
##
## ⚠️ `get_viewport().get_texture()` devolve o que já foi desenhado, ou seja, o
## quadro ANTERIOR. Sem esta espera, um passo cuja condição fica verdadeira num
## quadro fotografa o estado de antes dele: a captura do RCS saía com o motor
## principal ainda aceso e o acelerador a 100 %, porque o corte tinha acontecido
## no quadro que ainda não tinha sido desenhado.
##
## Três quadros, e não um: entre mudar um comando e ele aparecer desenhado há a
## própria simulação, a atualização dos instrumentos e o desenho.
const SETTLE_FRAMES := 3
var _armed := false
var _steps: Array[Dictionary] = []


func _init(owner: Node, out_directory: String) -> void:
	flight = owner
	directory = out_directory
	DirAccess.make_dir_recursive_absolute(directory)
	_steps = _script()
	# Iterar na imagem não pode exigir voar até à Lua primeiro. Com
	# SPACEFLIGHT_SHOT_STOP=N a sequência para no passo N, o que faz uma volta de
	# ajuste visual custar segundos em vez de minutos.
	var stop := OS.get_environment("SPACEFLIGHT_SHOT_STOP")
	if stop.is_valid_int() and int(stop) > 0:
		_steps = _steps.slice(0, int(stop))


func _script() -> Array[Dictionary]:
	## Cada passo: `setup` uma vez ao entrar, depois espera `until` (ou
	## `frames`), depois fotografa `shot` e segue.
	return [
		{
			"name": "cockpit in Earth orbit",
			"setup": func() -> void:
				flight.camera_rig.set_mode(CameraRig.Mode.COCKPIT)
				flight.controls.set_throttle(0.0),
			# Noventa quadros e não zero: as texturas procedurais do planeta são
			# desenhadas no primeiro quadro e a órbita precisa de alguns para o
			# apontamento assentar. Fotografar antes disso registraria o carregar
			# e não o simulador.
			"frames": 90,
			"shot": "cockpit-earth-orbit",
		},
		{
			"name": "cockpit instruments, nose on prograde",
			"setup": func() -> void:
				# Warp 10x durante a guinada. Ela leva um par de minutos de tempo
				# de simulação, e a warp 1 isso é mais quadros do que esta
				# sequência tem -- a primeira versão fotografou o instrumento com
				# 65 graus de erro e chamou-lhe "nariz no prógrado".
				_set_warp(1)
				flight.controls.point("prograde"),
			"until": _pointed,
			"limit": 2400,
			"shot": "cockpit-instruments",
		},
		{
			"name": "the whole Earth, from far enough to judge it",
			"setup": func() -> void:
				# A regra 31 diz que a Terra tem de parecer uma Terra, e isso não
				# se decide a 400 km de altitude, onde só se vê um pedaço de
				# oceano. Focar o planeta põe a câmera a três raios de distância,
				# que é o enquadramento em que continentes, nuvens, terminador e
				# lado noturno aparecem todos ao mesmo tempo.
				flight.camera_rig.focus_index = flight.celestial.index_of("Earth")
				flight.camera_rig.orbit_azimuth = 0.9
				flight.camera_rig.orbit_elevation = 0.25
				flight.camera_rig.orbit_zoom = 1.0,
			"frames": 40,
			"shot": "earth-whole-disc",
		},
		{
			"name": "the whole Moon, where the relief is judgeable",
			"setup": func() -> void:
				# O terminador é onde um normal map se vê: é lá que a luz é
				# rasante e uma cratera lança sombra dentro de si mesma. No
				# meio do disco iluminado o relevo desaparece, e uma captura
				# feita ali não decidiria nada (regra 32).
				flight.camera_rig.focus_index = flight.celestial.index_of("Moon")
				flight.camera_rig.orbit_azimuth = 0.6
				flight.camera_rig.orbit_elevation = 0.18
				flight.camera_rig.orbit_zoom = 1.0,
			"frames": 40,
			"shot": "moon-whole-disc",
		},
		{
			"name": "external view of the spacecraft",
			"setup": func() -> void:
				flight.camera_rig.focus_index = -1,
			"frames": 5,
			"shot": "",
		},
		{
			"name": "external view of the spacecraft",
			"setup": func() -> void:
				flight.camera_rig.set_mode(CameraRig.Mode.EXTERNAL_ORBIT)
				# 0,7 e não 1,15: esta é a figura que o manual anota peça a peça,
				# e cinco marcadores de 5 mm sobre uma nave que ocupa um décimo
				# do quadro caem uns por cima dos outros.
				flight.camera_rig.orbit_zoom = 0.7
				_frame_sunlit(0.42),
			"frames": 40,
			"shot": "external-spacecraft",
			"anchors": _ship_anchors,
		},
		{
			"name": "main engine running",
			"setup": func() -> void:
				flight.controls.set_throttle(1.0)
				# Para trás: a nave mede 22 m e a pluma cheia mais 14, e a 28 m
				# de distância o conjunto não cabe no quadro.
				flight.camera_rig.orbit_zoom = 1.25,
			"until": _engine_running,
			"limit": 120,
			"shot": "engine-plume",
		},
		{
			"name": "RCS firing",
			"setup": func() -> void:
				flight.controls.set_throttle(0.0)
				# Tão perto quanto a cabine permite. Um bico de RCS empurra
				# 180 N contra os 200 kN do motor principal, e o jato que isso
				# desenha tem 1,35 m num veículo de 22 m: a 46 m de distância
				# são três pixels. A 13 m vê-se QUAL bico está aberto, que é o
				# assunto da imagem -- contá-los é trabalho do instrumento de
				# RCS, não da fotografia.
				flight.camera_rig.orbit_zoom = 0.38
				# Um comando de apontamento novo obriga o alocador a abrir bicos,
				# e são os bicos abertos que a imagem tem de mostrar -- não uma
				# tecla premida (regra 15).
				flight.controls.point("normal"),
			"until": _rcs_firing,
			"limit": 300,
			"shot": "rcs-firing",
			"anchors": _lit_thruster_anchor,
		},
		{
			"name": "back out for the rest",
			"setup": func() -> void:
				flight.camera_rig.orbit_zoom = 1.15,
			"frames": 5,
			"shot": "",
		},
		{
			"name": "navigation display and orbit map",
			"setup": func() -> void:
				flight.camera_rig.set_mode(CameraRig.Mode.COCKPIT)
				flight._show_panel(flight.orbit_map, true),
			"frames": 30,
			"shot": "orbit-map-earth",
		},
		{
			"name": "target the Moon",
			"setup": func() -> void:
				flight._show_panel(flight.orbit_map, false)
				flight.set_target("Moon")
				flight._show_panel(flight.mission_panel, true),
			"frames": 20,
			"shot": "moon-target",
		},
		{
			"name": "plan the transfer",
			"setup": func() -> void:
				flight.controls.point("")
				flight.plan_mission("Moon"),
			"frames": 20,
			"shot": "mission-plan",
		},
		{
			"name": "execute, and look at the transfer",
			"setup": func() -> void:
				_armed = flight.arm_mission()
				flight._show_panel(flight.mission_panel, false)
				flight._show_panel(flight.orbit_map, true)
				flight.orbit_map.zoom = 0.5,
			"frames": 30,
			"shot": "transfer-map",
		},
		{
			"name": "coast under warp",
			"setup": func() -> void:
				flight._show_panel(flight.orbit_map, false)
				flight.camera_rig.set_mode(CameraRig.Mode.TARGET_REFERENCE)
				_set_warp(5),
			"until": _within_approach,
			"limit": 6000,
			"shot": "lunar-approach",
		},
		{
			"name": "capture burn",
			"setup": func() -> void:
				# Ainda 1e5 até estar perto. A etapa anterior parava em 1e3 e o
				# resto da travessia -- meio dia de voo -- não cabia no
				# orçamento de quadros: a sequência fotografava a "captura" a
				# 360 000 km, ainda a caminho. O warp só desce quando há algo
				# para ver.
				_set_warp(5),
			"until": _close_to_target,
			"limit": 9000,
			"shot": "lunar-capture",
		},
		{
			"name": "settled lunar orbit, ship against the Moon",
			"setup": func() -> void:
				_set_warp(3)
				flight.camera_rig.set_mode(CameraRig.Mode.TARGET_REFERENCE),
			# Depois de a queima ACABAR, e não durante. A primeira versão pedia só
			# uma órbita fechada em torno da Lua, e uma órbita fecha no instante em
			# que a queima a fecha -- a fotografia saía a `CAPTURE_BURN`, com o
			# motor ainda aceso e a nave apontada a retrógrado, ou seja, de costas
			# para a Lua.
			"until": _settled_in_lunar_orbit,
			"limit": 9000,
			"shot": "lunar-orbit",
		},
		{
			"name": "the ship in lunar orbit, with the Moon behind it",
			"setup": func() -> void:
				_set_warp(2)
				# O referencial do ALVO e não o do casco: assim a câmera fica do
				# lado oposto à Lua e enquadra a nave COM a Lua atrás. Com
				# `_frame_sunlit` o enquadramento seguia o Sol, que em órbita
				# lunar pode estar exatamente na direção contrária à Lua -- e a
				# fotografia final saía com a nave sozinha num campo preto.
				flight.camera_rig.set_mode(CameraRig.Mode.TARGET_REFERENCE)
				flight.camera_rig.orbit_azimuth = PI
				flight.camera_rig.orbit_elevation = 0.34
				flight.camera_rig.orbit_zoom = 2.4,
			# Esperar até a nave passar para o lado ILUMINADO da Lua. A órbita
			# demora 118 minutos e metade dela é noite lunar: fotografar sem
			# esperar dava uma nave escura contra uma superfície escura, o que é
			# uma descrição correta do lado noturno e uma péssima evidência.
			"until": _over_sunlit_moon,
			"limit": 6000,
			"shot": "lunar-orbit-external",
		},
	]


## As condições longas vivem aqui e não dentro do dicionário. Um `func` de
## várias linhas com continuação dentro de um literal de dicionário não passa
## pelo parser do GDScript -- e, mesmo que passasse, uma condição de missão com
## nome é mais fácil de ler do que uma anônima no meio de um roteiro.
func _pointed() -> bool:
	return flight.simulation.get_pointing_error_deg() < 4.0


func _engine_running() -> bool:
	return float(flight.simulation.get_snapshot().get("thrust_n", 0.0)) > 0.0


func _rcs_firing() -> bool:
	return flight.rcs_firing_count() > 0


## As cinco peças da nave, em metros no referencial do corpo.
##
## As constantes são as de `SpacecraftVisual` -- a mesma fonte que desenha o
## casco -- e não números copiados. O que se decide aqui é só de que LADO pôr as
## peças que existem em par: tanques e radiadores são dois, e marcar o que está
## do lado oposto da câmera aponta para uma peça escondida por trás do casco.
func _ship_anchors() -> Dictionary:
	var to_body: Transform3D = flight.ship_root.global_transform.affine_inverse()
	var eye: Vector3 = to_body * flight.camera_rig.near_camera.global_position
	var near_y := signf(eye.y) if absf(eye.y) > 1.0e-6 else 1.0
	var near_z := signf(eye.z) if absf(eye.z) > 1.0e-6 else 1.0
	return {
		# Afastados de propósito: dois marcadores de 5 mm a quatro metros um do
		# outro encostam-se na página.
		"cockpit": Vector3(SpacecraftVisual.NOSE_TIP - 0.7, 0.0, 0.8 * near_z),
		"habitat": Vector3(SpacecraftVisual.CORE_AFT + 1.0, 0.0,
			SpacecraftVisual.CORE_RADIUS * near_z),
		"tanks": Vector3(
			(SpacecraftVisual.TANK_FORWARD + SpacecraftVisual.TANK_AFT) * 0.5,
			SpacecraftVisual.TANK_OFFSET * near_y,
			SpacecraftVisual.TANK_RADIUS * 0.6 * near_z),
		"radiators": Vector3(-7.6, 0.0, SpacecraftVisual.RADIATOR_SPAN * 0.75 * near_z),
		"engine": Vector3(SpacecraftVisual.ENGINE_EXIT + 1.6, 0.0,
			SpacecraftVisual.BELL_EXIT_RADIUS * 0.7 * near_z),
	}


## O bico aberto que está MAIS PERTO da câmera, e a ponta do jato dele.
##
## Qual é depende do alocador, não do roteiro: o marcador do manual tem de
## seguir a decisão que a simulação tomou naquele quadro.
func _lit_thruster_anchor() -> Dictionary:
	var thrusters: Array = flight.simulation.get_rcs_thrusters()
	var throttles: PackedFloat64Array = flight.simulation.get_rcs_throttles()
	var to_body: Transform3D = flight.ship_root.global_transform.affine_inverse()
	var eye: Vector3 = (to_body * flight.camera_rig.near_camera.global_position).normalized()

	# Qual dos seis bicos abertos se VÊ. Duas condições, e as duas foram
	# aprendidas errando:
	#
	#  * o braço tem de ser RADIAL. Quatro dos doze thrusters de
	#    `RcsSystem::couples` ficam em (±2, 0, 0) -- sobre o eixo longitudinal,
	#    dentro de um casco de 1,5 m de raio. Nunca se veem, e eram justamente os
	#    que ficavam mais perto da câmera;
	#  * entre os radiais, o mais VOLTADO para a câmera. O mais próximo pode estar
	#    atrás de um tanque (braço de 2 m, tanques a 2,6 m do eixo com 1,7 m de
	#    raio), e o marcador caía num vão escuro com o jato visível a oitenta
	#    pixels dali.
	var best := -1
	var facing := -1.0
	for i in range(mini(thrusters.size(), throttles.size())):
		if throttles[i] <= RcsVisual.THRESHOLD:
			continue
		var at := Vector3(thrusters[i]["position"])
		var radial := Vector3(0.0, at.y, at.z)
		if radial.length() <= SpacecraftVisual.CORE_RADIUS:
			continue
		var towards := radial.normalized().dot(eye)
		if towards > facing:
			facing = towards
			best = i
	if best < 0:
		return {}
	var spec: Dictionary = thrusters[best]
	# A PONTA do jato: meio metro na direção da exaustão, que é o lado contrário
	# ao da força (terceira lei, a mesma inversão que `RcsVisual` faz).
	var exhaust: Vector3 = -Vector3(spec["force_direction"]).normalized()
	return {"jet": Vector3(spec["position"]) + exhaust * 0.55}


func _within_approach() -> bool:
	return flight.simulation.get_mission_phase() in [
		"APPROACH", "CAPTURE", "ORBIT_INSERTION", "COMPLETE"]


## Perto o bastante da Lua para a queima de captura ser o assunto da imagem.
##
## A esfera de Hill tem 61 500 km de raio e `get_orbit_about_target` abre-se nela
## -- que é o critério certo para "há uma órbita a reportar" e o errado para
## "isto é a captura": a primeira versão fotografou a captura a 360 000 km da
## Terra, ainda a caminho.
const CAPTURE_RANGE_M := 2.0e7


func _close_to_target() -> bool:
	if flight.simulation.get_mission_phase() in ["ORBIT_INSERTION", "COMPLETE"]:
		return true
	var orbit: Dictionary = flight.simulation.get_orbit_about_target()
	return not orbit.is_empty() and float(orbit.get("distance_m", INF)) < CAPTURE_RANGE_M


func _settled_in_lunar_orbit() -> bool:
	var orbit: Dictionary = flight.simulation.get_orbit_about_target()
	if orbit.is_empty() or not orbit.get("captured", false):
		return false
	if float(orbit.get("distance_m", INF)) >= CAPTURE_RANGE_M:
		return false
	# E com o motor apagado: uma órbita que acabou de fechar ainda está a ser
	# fechada.
	return not flight.simulation.get_plan().get("burning", false)


func step(_delta: float) -> void:
	if _step >= _steps.size():
		flight.get_tree().quit()
		return
	var current := _steps[_step]

	if _settle == 0:
		print("[shots] %d/%d  %s" % [_step + 1, _steps.size(), current["name"]])
		var setup: Callable = current["setup"]
		setup.call()
	_settle += 1

	var ready := false
	if current.has("frames"):
		ready = _settle >= int(current["frames"])
	else:
		var until: Callable = current["until"]
		var limit := int(current.get("limit", 1200))
		ready = until.call() or _settle >= limit
		if _settle >= limit and not until.call():
			# Fotografa na mesma e DIZ que a condição não foi atingida. Uma
			# sequência que abortasse aqui não deixaria imagem nenhuma do que de
			# facto aconteceu, que é justamente o que se quer ver quando algo
			# corre mal.
			print("[shots]   condition not met within %d frames -- photographing anyway" % limit)

	if not ready:
		_hold = 0
		return
	# A condição está satisfeita; agora deixar o que ela descreve chegar à tela.
	if _hold < SETTLE_FRAMES:
		_hold += 1
		return

	# Um passo pode existir só para mudar o estado -- devolver o foco à nave, por
	# exemplo -- e nesse caso não há fotografia para tirar.
	if String(current["shot"]) != "":
		_shoot(String(current["shot"]), current.get("anchors", {}))
	_step += 1
	_settle = 0
	_hold = 0


func _shoot(name: String, anchors: Variant) -> void:
	var image := flight.get_viewport().get_texture().get_image()
	var path := "%s/%s.png" % [directory, name]
	image.save_png(path)
	_write_anchors(name, anchors)
	var s: Dictionary = flight.simulation.get_snapshot()
	print("[shots]   %s.png   t+%s   alt %s   phase %s"
		% [name, Fmt.duration(s.get("elapsed_s", 0.0)),
		   Fmt.distance(s.get("altitude_m", 0.0)),
		   flight.simulation.get_mission_phase()])


## Onde cada peça da nave caiu NA IMAGEM, em percentagem do quadro.
##
## Os marcadores numerados do manual eram coordenadas escritas à mão sobre uma
## captura, e envelheciam em silêncio: bastava a câmera mudar de enquadramento
## para o número "motor principal" passar a apontar para um radiador, sem que
## nada falhasse. Aqui o ponto é dado no referencial do CORPO -- em metros, os
## mesmos de `SpacecraftVisual` -- e quem o projeta é a câmera que tirou a
## fotografia. O manual lê o JSON e não sabe de pixels.
func _write_anchors(name: String, declared: Variant) -> void:
	# Um passo pode saber as suas âncoras de antemão (um dicionário) ou só no
	# instante da fotografia (uma `Callable`) -- que é o caso do RCS: qual bico
	# está aberto é decisão do alocador, e o roteiro não a toma.
	var anchors: Dictionary = declared.call() if declared is Callable else declared
	if anchors.is_empty():
		return
	var camera: Camera3D = flight.camera_rig.near_camera
	var to_world: Transform3D = flight.ship_root.global_transform
	var frame: Vector2 = camera.get_viewport().get_visible_rect().size
	var out := {}
	for key in anchors:
		var world: Vector3 = to_world * (anchors[key] as Vector3)
		if camera.is_position_behind(world):
			continue
		var at := camera.unproject_position(world)
		out[key] = {
			"x": snappedf(at.x / frame.x * 100.0, 0.1),
			"y": snappedf(at.y / frame.y * 100.0, 0.1),
		}
	var file := FileAccess.open("%s/%s.anchors.json" % [directory, name], FileAccess.WRITE)
	if file == null:
		printerr("[shots]   cannot write anchors for %s" % name)
		return
	file.store_string(JSON.stringify(out, "  ", true, false) + "\n")
	file.close()


## Põe a câmera externa do lado iluminado, seja qual for a hora.
##
## O lado contrário ao Sol fica escuro, e tem de ficar (regra 29) -- mas um
## azimute fixo escolhido à mão põe a câmera no lado errado assim que a órbita
## avança, e a fotografia de demonstração sai preta com dois radiadores a brilhar.
## Aqui o azimute e a elevação são DERIVADOS da direção do Sol nos eixos do
## casco, que é o mesmo referencial em que `CameraRig` parametriza a órbita.
##
## Isto é enquadramento, não física: nada do que se vê muda, só de onde se vê.
func _frame_sunlit(elevation_bias: float) -> void:
	var directions: Dictionary = flight.simulation.get_flight_directions()
	if not directions.has("sun"):
		return
	var sun: Vector3 = Vector3(directions["sun"]).normalized()
	var basis: Basis = flight.simulation.get_spacecraft_basis()
	var forward := basis.x.normalized()
	var up := basis.z.normalized()
	var right := forward.cross(up).normalized()
	up = right.cross(forward).normalized()

	var along := sun.dot(forward)
	var across := sun.dot(right)
	var vertical := clampf(sun.dot(up), -1.0, 1.0)
	flight.camera_rig.orbit_azimuth = atan2(across, along)
	# A elevação do Sol, empurrada um pouco para cima: um pouco de vista de cima
	# mostra os radiadores, que é metade do que faz a nave parecer uma nave.
	flight.camera_rig.orbit_elevation = clampf(asin(vertical) * 0.6 + elevation_bias,
		-1.3, 1.3)


## Se a nave está sobre a metade iluminada do alvo.
func _over_sunlit_moon() -> bool:
	var directions: Dictionary = flight.simulation.get_flight_directions()
	if not directions.has("sun"):
		return true
	var index: int = flight.celestial.index_of(flight.simulation.get_target_body())
	if index < 0:
		return true
	var from_target: Vector3 = flight.simulation.get_spacecraft_position() \
		- flight.simulation.get_body_position(index)
	if from_target.length() < 1.0e-9:
		return true
	return from_target.normalized().dot(Vector3(directions["sun"]).normalized()) > 0.30


func _set_warp(index: int) -> void:
	flight.controls.warp_index = index
	flight.simulation.set_time_warp(flight.controls.warp())
