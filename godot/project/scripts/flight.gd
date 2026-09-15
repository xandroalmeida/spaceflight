extends Node3D
## O vertical slice: um cockpit, uma nave, e a Lua (Milestone 7).
##
## Este arquivo é o ORQUESTRADOR. Ele não desenha nada, não calcula nada e não
## sabe física nenhuma: ele constrói os pedaços, entrega a cada um o que ele
## precisa, e encaminha a entrada. A regra de arquitetura do projeto continua a
## mesma que era no Milestone 2 (regra 77):
##
##     CORE  --snapshot-->  GODOT
##
## Nada abaixo integra uma órbita, resolve um Lambert, inventa empuxo ou inventa
## combustível. Cada número que aparece na tela saiu de uma chamada a
## `SpaceflightSimulation`, e o que este script faz com ele é escolher a unidade.
##
## ## A cena, em uma tela
##
##     Flight (Node3D)                 mundo em unidades de cena, 1 = 1e6 m
##     ├── SpaceflightSimulation       o core
##     ├── SpaceflightSky              o catálogo estelar
##     ├── StarfieldView               8786 estrelas, aberradas na CPU
##     ├── CelestialView               Terra, Lua, Sol
##     ├── CameraRig                   as DUAS câmeras (ver camera_rig.gd)
##     ├── NearField (SubViewport)     mundo próprio, em METROS
##     │   └── ShipRoot                base = atitude da nave
##     │       ├── SpacecraftVisual
##     │       ├── EnginePlume
##     │       ├── RcsVisual
##     │       └── CockpitInterior
##     └── Interface (CanvasLayer)     HUD, mapa, missão, menu, debug
##
## Por que há dois mundos e duas escalas está escrito em `camera_rig.gd`, junto
## da conversão entre eles, que acontece numa linha só.

const RENDER_SCALE := 1.0e-6

## Estado inicial (regra 65): órbita de estacionamento a 400 km, 51,6 graus --
## a inclinação da ISS, escolhida porque dá uma geometria de partida honesta
## para a Lua e porque é uma órbita que existe.
const ALTITUDE_M := 400_000.0
const INCLINATION_DEG := 51.6
const EPOCH_UTC := "2026-01-01T00:00:00"
## Quanto o nariz parte abaixo do prógrado. De 400 km o limbo da Terra fica a
## 19,7 graus abaixo da horizontal local, então um nariz exatamente no prógrado
## põe o planeta inteiro abaixo do peitoril da janela.
const NADIR_BIAS_DEG := 25.0

const BODY_SCALES := [1.0, 10.0, 100.0, 1000.0]
const EXPOSURE_LEVELS := [0.0158, 0.0501, 0.1585, 0.5012, 1.5849]
const VISUAL_TEST_BETAS := [-1.0, 0.0, 0.1, 0.5, 0.9, 0.99]
const MAGNITUDE_LIMIT := 7.96

const ORBIT_TRACK_SAMPLES := 192
const TARGET_TRACK_SAMPLES := 128

const HUD_MODES := ["cockpit", "minimal", "off"]

## Qual órbita lunar a missão pede. Note o que NÃO é um parâmetro: o tempo de
## voo. Ele é o que a busca decide, e fixá-lo é precisamente o defeito que fez a
## campanha do Milestone 6 falhar em 82 % das suas épocas.
const MISSION_PERIAPSIS_KM := 100.0
const MISSION_APOAPSIS_KM := 100.0
const MISSION_SEARCH_HOURS := 2.0

# --- núcleo ---
var simulation: SpaceflightSimulation
var sky: SpaceflightSky
var controls: FlightControls

# --- mundo ---
var celestial: CelestialView
var starfield: StarfieldView
var materials: ShipMaterials

# --- campo próximo ---
var near_viewport: SubViewport
var ship_root: Node3D
var spacecraft: SpacecraftVisual
var plume: EnginePlume
var rcs_visual: RcsVisual
var cockpit: CockpitInterior
var near_sun: DirectionalLight3D

# --- câmera e interface ---
var camera_rig: CameraRig
var interface: CanvasLayer
var hud: MinimalHud
var messages: MessageLog
var mission_panel: MissionPanel

## Se havia uma busca a correr no quadro anterior. Sem isto, `_poll_planning`
## não consegue distinguir "acabou agora" de "nunca começou" e chamaria
## `collect_plan()` todo quadro.
var _search_was_running := false

## O mapa heliocêntrico, reconstruído com os demais traçados. As ÓRBITAS dos
## planetas vêm à parte e são pedidas uma vez só: são um milhar de consultas à
## efeméride e não mudam de quadro para quadro.
var _system_map: Dictionary = {}
var orbit_map: OrbitMap
var pause_menu: PauseMenu
var help_panel: HelpPanel
var debug_hud: DebugHud
var audio: AudioDirector

# --- estado da apresentação ---
var hud_mode := 0
var rcs_switch: CockpitControl
var body_scale_index := 0
var exposure_index := 2
var visual_beta_index := 0
var mouse_sensitivity := 1.0
var ui_scale := 1.0

var _headless: HeadlessDriver
var _shots: ShotDirector
var _capture_dir := OS.get_environment("SPACEFLIGHT_CAPTURE")
var _capture_index := 0
var _capture_frames := 0
var _capture_started := false
var _rcs_throttles: PackedFloat64Array = PackedFloat64Array()
var _rcs_thruster_count := 0
var _directions: Dictionary = {}
var _orbit_track: PackedVector3Array = PackedVector3Array()
var _target_track: PackedVector3Array = PackedVector3Array()
var _planned_track: PackedVector3Array = PackedVector3Array()
var _maneuvers: Array = []
var _track_timer := 0.0
var _last_phase := ""
var _warned_low_propellant := false


# =============================================================================
#  construção
# =============================================================================

func _ready() -> void:
	InputActions.install()

	simulation = SpaceflightSimulation.new()
	add_child(simulation)

	# Os kernels vivem fora do projeto Godot, ao lado do core que os lê. `res://`
	# não pode sair do diretório do projeto, então globaliza-se e sobe-se.
	var kernel_dir := ProjectSettings.globalize_path("res://").path_join("../../kernels/spice")
	kernel_dir = kernel_dir.simplify_path()
	if not simulation.configure(kernel_dir, EPOCH_UTC):
		push_error("Could not load SPICE kernels from %s -- run scripts/fetch_kernels.sh. %s"
			% [kernel_dir, simulation.get_last_error()])
		return
	if not simulation.start_circular_orbit(ALTITUDE_M, INCLINATION_DEG):
		push_error(simulation.get_last_error())
		return
	# Nariz no prógrado, planeta sob o chão -- a atitude em que uma órbita de
	# estacionamento é de facto voada. Sem isto o primeiro quadro de um voo novo
	# mostra céu vazio: a atitude de partida do cenário é a identidade, e nela o
	# nariz aponta para o zênite (regra 66).
	simulation.align_attitude_to_flight(NADIR_BIAS_DEG)

	simulation.set_render_scale(RENDER_SCALE)
	simulation.set_body_scale_exaggeration(BODY_SCALES[body_scale_index])
	controls = FlightControls.new(simulation)
	simulation.set_time_warp(controls.warp())

	# O catálogo vive ao lado dos kernels, fora do projeto Godot, pela mesma
	# razão: é dado obtido (`catalogs/MANIFEST.md`).
	sky = SpaceflightSky.new()
	add_child(sky)
	sky.set_magnitude_limit(MAGNITUDE_LIMIT)
	sky.set_half_saturation(EXPOSURE_LEVELS[exposure_index])
	var catalogue := ProjectSettings.globalize_path("res://").path_join("../../catalogs/bsc5.dat")
	if not sky.load_catalogue(catalogue.simplify_path()):
		# Não é fatal: o céu escurece e diz porquê. A dinâmica não muda.
		push_warning("No star catalogue -- run scripts/fetch_star_catalog.sh. %s"
			% sky.get_last_error())

	materials = ShipMaterials.new()
	_build_world()
	_build_near_field()
	_build_camera()
	_build_interface()

	audio = AudioDirector.new()
	add_child(audio)

	_wire_controls()

	_rcs_thruster_count = simulation.get_rcs_thrusters().size()
	rcs_visual.build(simulation.get_rcs_thrusters())

	_headless = HeadlessDriver.new(self)
	# Dois roteiros, duas variáveis. O do M7 continua a produzir as mesmas nove
	# imagens de um milestone fechado; o do M8 voa até Marte.
	var shot_dir := OS.get_environment("SPACEFLIGHT_M7_SHOTS")
	if shot_dir.is_empty():
		shot_dir = OS.get_environment("SPACEFLIGHT_M8_SHOTS")
	if not shot_dir.is_empty():
		_shots = ShotDirector.new(self, shot_dir)

	# Regra 66: a tela não começa preta nem numa página de debug. A primeira
	# coisa que existe é o cockpit, com a Terra lá fora.
	messages.post("NEW FLIGHT — EARTH ORBIT", MessageLog.Level.MISSION)
	messages.post("%s for controls   %s for the mission computer"
		% [InputActions.label("help"), InputActions.label("nav_panel")],
		MessageLog.Level.INFO)


func _build_world() -> void:
	var environment_node := WorldEnvironment.new()
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.004, 0.005, 0.009)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	# Baixa de propósito. O lado da nave contrário ao Sol tem de ficar escuro
	# (regra 29), e a luz ambiente é a única coisa que decide quão escuro --
	# no espaço real ela é a luz refletida pelo planeta, que existe e é fraca.
	environment.ambient_light_color = Color(0.05, 0.06, 0.08)
	environment.ambient_light_energy = 0.55
	environment_node.environment = environment
	add_child(environment_node)

	starfield = StarfieldView.new()
	add_child(starfield)
	starfield.build(sky)

	celestial = CelestialView.new()
	add_child(celestial)
	celestial.build(simulation, starfield.planck_texture)
	if sky != null and sky.is_ready():
		celestial.set_planck_reference(sky.get_planck_table_reference_temperature())


func _build_near_field() -> void:
	## O campo próximo: um `SubViewport` com MUNDO PRÓPRIO, em metros, composto
	## por cima da vista do mundo. Mundo próprio porque, sem ele, os planetas em
	## unidades de cena também seriam desenhados aqui -- a 358 unidades, que na
	## escala desta câmera é 3,6e8 metros, muito para além do seu plano distante,
	## mas ainda assim um planeta inteiro a ser processado duas vezes por quadro.
	var layer := CanvasLayer.new()
	layer.layer = 0
	add_child(layer)

	var container := SubViewportContainer.new()
	container.set_anchors_preset(Control.PRESET_FULL_RECT)
	container.stretch = true
	container.mouse_filter = Control.MOUSE_FILTER_IGNORE
	# ⚠️ Um `SubViewport` transparente devolve cor JÁ MULTIPLICADA pelo alfa: com
	# o fundo a (0,0,0,0), `a·C + (1−a)·0` é exatamente isso. Compô-lo com a
	# mistura normal multiplica pelo alfa uma segunda vez, e o que desaparece por
	# inteiro é tudo o que é ADITIVO -- a pluma do motor e os jatos de RCS
	# escrevem cor sem escrever alfa, e alfa zero apagava-os na composição. Os
	# dois estavam acesos no grafo de cena e nenhum dos dois chegava à tela.
	var composite := CanvasItemMaterial.new()
	composite.blend_mode = CanvasItemMaterial.BLEND_MODE_PREMULT_ALPHA
	container.material = composite
	layer.add_child(container)

	near_viewport = SubViewport.new()
	near_viewport.own_world_3d = true
	near_viewport.transparent_bg = true
	near_viewport.handle_input_locally = false
	near_viewport.gui_disable_input = true
	near_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	# Anti-aliasing AQUI e não no viewport do mundo. O que a cabine tem são
	# arestas finas, longas e quase verticais -- montantes de janela, o aro de um
	# mostrador, um parafuso de dois centímetros --, e é exatamente essa a forma
	# que sem MSAA sai em degraus. Numa captura, uma escada num montante lê como
	# "gráfico de brinquedo" antes de o observador conseguir dizer porquê. O
	# viewport do mundo não precisa: lá o que há são estrelas (pontos) e o limbo
	# de um planeta, que já é suave.
	near_viewport.msaa_3d = Viewport.MSAA_4X
	container.add_child(near_viewport)

	var environment_node := WorldEnvironment.new()
	var environment := Environment.new()
	# Fundo transparente: onde não há geometria da nave, vê-se o mundo por baixo.
	# É isto que faz uma janela ser uma janela, sem recortar nada.
	environment.background_mode = Environment.BG_CLEAR_COLOR
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.07, 0.08, 0.10)
	environment.ambient_light_energy = 0.5
	environment.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	# Exposição fixa e não automática (regra 51): com auto-exposição, virar a
	# cabeça da Terra para o painel faria a cabine inteira pulsar de brilho, e o
	# que se quer é justamente que o painel aceso e o exterior iluminado coexistam
	# sem que nenhum dos dois mande no outro.
	environment.tonemap_exposure = 1.0
	environment_node.environment = environment
	near_viewport.add_child(environment_node)

	near_sun = DirectionalLight3D.new()
	near_sun.light_energy = 2.6
	near_sun.light_color = Color(1.0, 0.97, 0.92)
	near_sun.shadow_enabled = true
	# Sombras só aqui, e com alcance curto: a nave projetando sombra sobre si
	# mesma é metade do que faz uma estrutura no espaço parecer sólida, e 120 m
	# cobre a nave inteira com folga.
	near_sun.directional_shadow_max_distance = 120.0
	near_viewport.add_child(near_sun)

	ship_root = Node3D.new()
	near_viewport.add_child(ship_root)

	spacecraft = SpacecraftVisual.new(materials)
	ship_root.add_child(spacecraft)

	plume = EnginePlume.new()
	# No BOCAL, não na origem do casco. `SpacecraftVisual.engine_mount` existe
	# para isto e estava por usar: pendurada na origem, a pluma saía 17 m à
	# frente da tubeira, dentro dos tanques.
	spacecraft.engine_mount.add_child(plume)

	rcs_visual = RcsVisual.new()
	ship_root.add_child(rcs_visual)

	cockpit = CockpitInterior.new(materials)
	ship_root.add_child(cockpit)


func _build_camera() -> void:
	camera_rig = CameraRig.new()
	add_child(camera_rig)
	camera_rig.build(RENDER_SCALE, near_viewport)
	camera_rig.set_mode(CameraRig.Mode.COCKPIT)


func _build_interface() -> void:
	interface = CanvasLayer.new()
	interface.layer = 1
	add_child(interface)

	hud = MinimalHud.new()
	hud.set_anchors_preset(Control.PRESET_FULL_RECT)
	interface.add_child(hud)

	messages = MessageLog.new()
	# Canto inferior esquerdo, acima da faixa do HUD. No meio da tela elas ficam
	# por cima exatamente da parte da vista que a nave costuma atravessar.
	messages.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)
	messages.position = Vector2(28.0, -300.0)
	messages.grow_vertical = Control.GROW_DIRECTION_BEGIN
	interface.add_child(messages)

	orbit_map = OrbitMap.new()
	orbit_map.set_anchors_preset(Control.PRESET_FULL_RECT)
	orbit_map.visible = false
	interface.add_child(orbit_map)

	var centre := CenterContainer.new()
	centre.set_anchors_preset(Control.PRESET_FULL_RECT)
	centre.mouse_filter = Control.MOUSE_FILTER_IGNORE
	interface.add_child(centre)

	mission_panel = MissionPanel.new()
	mission_panel.visible = false
	centre.add_child(mission_panel)

	pause_menu = PauseMenu.new()
	pause_menu.visible = false
	centre.add_child(pause_menu)

	help_panel = HelpPanel.new()
	help_panel.visible = false
	centre.add_child(help_panel)

	debug_hud = DebugHud.new()
	debug_hud.flight = self
	debug_hud.visible = false
	interface.add_child(debug_hud)


func _wire_controls() -> void:
	controls.message.connect(func(text: String, level: int) -> void:
		messages.post(text, level)
		if level >= MessageLog.Level.WARNING:
			audio.warning())
	controls.rcs_fired.connect(func() -> void: audio.rcs_fired(rcs_firing_count()))
	controls.engine_changed.connect(func(running: bool) -> void:
		messages.post("MAIN ENGINE %s" % ("IGNITION" if running else "CUT-OFF"),
			MessageLog.Level.INFO))

	mission_panel.search_cancelled.connect(func() -> void: cancel_planning())
	mission_panel.alternative_chosen.connect(func(index: int) -> void: choose_alternative(index))
	mission_panel.plan_requested.connect(func(target: String, pe: float, ap: float) -> void:
		plan_mission(target, pe, ap))
	mission_panel.execute_requested.connect(func() -> void: arm_mission())
	mission_panel.cancel_requested.connect(func() -> void: abort_mission())
	mission_panel.target_changed.connect(func(target: String) -> void: set_target(target))
	mission_panel.closed.connect(func() -> void: _show_panel(mission_panel, false))

	pause_menu.resume_requested.connect(func() -> void: _set_paused(false))
	pause_menu.help_requested.connect(func() -> void:
		_show_panel(pause_menu, false)
		_show_panel(help_panel, true))
	pause_menu.quit_requested.connect(func() -> void: get_tree().quit())
	pause_menu.setting_changed.connect(_apply_setting)
	help_panel.closed.connect(func() -> void: _show_panel(help_panel, false))

	mission_panel.set_targets(_selectable_targets(), simulation.get_target_body())

	# Os sete botões físicos do painel (regra 24). Cada um chama exatamente o
	# mesmo caminho que a tecla correspondente -- não há um segundo comando
	# escondido atrás do botão, e é por isso que o mouse e o teclado nunca podem
	# discordar sobre o estado da nave.
	cockpit.configure_button(0, "ENGINE",
		func(_c: CockpitControl) -> void:
			audio.button_pressed()
			controls.set_throttle(0.0 if controls.throttle > 0.0 else 1.0))
	# Guardado em vez de procurado pelo índice: `cockpit.controls[1]` funciona até
	# alguém acrescentar um botão à esquerda dele, e nesse dia a tecla `V` passa a
	# acender a lâmpada errada sem que nada se queixe.
	rcs_switch = cockpit.configure_button(1, "RCS",
		func(c: CockpitControl) -> void:
			audio.switch_flipped()
			controls.toggle_rcs()
			c.set_on(controls.rcs_enabled),
		true, controls.rcs_enabled)
	cockpit.configure_button(2, "AP",
		func(_c: CockpitControl) -> void:
			audio.button_pressed()
			controls.point("prograde"))
	cockpit.configure_button(3, "NAV",
		func(_c: CockpitControl) -> void:
			audio.button_pressed()
			_toggle_panel(mission_panel))
	cockpit.configure_button(4, "MAP",
		func(_c: CockpitControl) -> void:
			audio.button_pressed()
			_toggle_panel(orbit_map))
	cockpit.configure_button(5, "WARP",
		func(_c: CockpitControl) -> void:
			audio.button_pressed()
			controls.step_warp(1))
	cockpit.configure_button(6, "MODE",
		func(_c: CockpitControl) -> void:
			audio.button_pressed()
			controls.cycle_engine_mode())


# =============================================================================
#  quadro
# =============================================================================

func _process(delta: float) -> void:
	if simulation == null or not simulation.is_ready():
		return

	controls.apply(delta)
	_camera_keys(delta)

	# A taxa de quadros decide QUANTO tempo coordenado pedir. Ela nunca chega ao
	# integrador, que escolhe os próprios passos (regra 21).
	if not controls.paused:
		simulation.advance(delta)

	# Origem flutuante: recentra no que se está a olhar, todo quadro.
	if camera_rig.focus_index < 0:
		simulation.focus_on_spacecraft()
	else:
		simulation.focus_on_body(camera_rig.focus_index)

	var ship_position := simulation.get_spacecraft_position()
	var ship_basis := simulation.get_spacecraft_basis()
	_directions = simulation.get_flight_directions()
	_rcs_throttles = simulation.get_rcs_throttles()

	_place_camera(ship_position, ship_basis)
	_update_world(delta)
	_update_ship(ship_basis)
	_update_tracks(delta)
	_update_instruments()
	_update_audio()
	_poll_planning()
	_search_was_running = simulation.is_planning()
	_watch_mission()

	if debug_hud.visible:
		debug_hud.render()

	# Sem janela não há quem carregue numa tecla: a corrida sem tela voa-se a si
	# mesma e imprime a leitura COMPLETA, seja qual for o modo do HUD.
	if DisplayServer.get_name() == "headless":
		_headless.drive("\n".join(debug_hud.hud_lines(false)))

	if _shots != null:
		_shots.step(delta)
	if not _capture_dir.is_empty():
		_capture_step()


func _place_camera(ship_position: Vector3, ship_basis: Basis) -> void:
	var focus_position := ship_position
	var focus_natural := 1.0
	if camera_rig.focus_index >= 0:
		focus_position = simulation.get_body_position(camera_rig.focus_index)
		focus_natural = maxf(simulation.get_body_radius(camera_rig.focus_index) * 3.0, 1.0)

	var to_target := Vector3.ZERO
	if _directions.has("target"):
		to_target = _directions["target"]

	camera_rig.update(ship_position, ship_basis, simulation.get_beta_vector(), to_target,
		focus_position, focus_natural)


func _update_world(delta: float) -> void:
	celestial.exposure = EXPOSURE_LEVELS[exposure_index]
	celestial.advance_clouds(delta)
	celestial.update(camera_rig.world_camera.position)
	starfield.effect_aberration = celestial.effect_aberration
	starfield.effect_doppler = celestial.effect_doppler
	starfield.effect_beaming = celestial.effect_beaming
	starfield.update(simulation.get_beta_vector(), EXPOSURE_LEVELS[exposure_index])

	# A mesma direção do Sol nas duas escalas. Se as duas divergissem, a nave
	# ficaria iluminada de um lado e o planeta do outro, que é o tipo de erro que
	# ninguém procura porque cada metade parece certa.
	if celestial.sun_index >= 0:
		var to_sun := (celestial.meshes[celestial.sun_index].position
			- camera_rig.world_camera.position)
		if to_sun.length() > 0.0:
			to_sun = to_sun.normalized()
			near_sun.look_at_from_position(Vector3.ZERO, -to_sun,
				Vector3.UP if absf(to_sun.z) < 0.98 else Vector3.RIGHT)


func _update_ship(ship_basis: Basis) -> void:
	# O casco aponta para onde a atitude diz que ele aponta -- não ao longo da
	# velocidade, que é o que um simulador sem atitude tem de fingir.
	ship_root.basis = ship_basis
	var snapshot := simulation.get_snapshot()
	plume.set_thrust(snapshot.get("thrust_n", 0.0))
	rcs_visual.set_throttles(_rcs_throttles)

	var inside := camera_rig.is_cockpit() and camera_rig.focus_index < 0
	cockpit.visible = inside
	cockpit.set_displays_visible(inside)
	cockpit.set_indicator("RCS", rcs_firing_count() > 0)
	cockpit.set_indicator("ENG", snapshot.get("thrust_n", 0.0) > 0.0)
	cockpit.set_indicator("AP", simulation.has_plan())
	cockpit.set_indicator("MSTR", _master_caution())


func _update_tracks(delta: float) -> void:
	## As trajetórias são reamostradas quatro vezes por segundo e não a cada
	## quadro.
	##
	## O custo é o motivo e ele é concreto: a órbita são 192 chamadas a
	## `state_from_elements`; o caminho do alvo são 256 chamadas à EFEMÉRIDE; o
	## arco planejado é mais uma chamada à efeméride POR AMOSTRA, porque cada
	## ponto é guardado relativo à origem na época dele e a origem tem de ser
	## somada de volta; e os marcadores de manobra varrem esse arco outra vez.
	## Isso é da ordem de mil consultas SPICE, e fazê-las sessenta vezes por
	## segundo custava mais do que a cena inteira.
	##
	## Nada disso muda o bastante em 16 ms para que se veja -- enquanto o custo
	## por quadro se via.
	_track_timer -= delta
	if _track_timer > 0.0:
		return
	_track_timer = 0.25

	# Enquanto a busca corre, o quadro cede a efeméride.
	#
	# ⚠️ O planejador roda numa thread e o CSPICE tem um mutex global: toda
	# consulta que este quadro faz é uma consulta que o worker espera. E este
	# bloco é o maior consumidor da cena -- 256 chamadas para o caminho do alvo,
	# mais uma por amostra do arco planejado, quatro vezes por segundo.
	#
	# Medido numa corrida sem tela, onde o quadro não tem limite de taxa e a
	# contenção é pior do que num jogo a 60 fps: uma busca Terra→Marte que leva
	# 64 s pela linha de comando não tinha terminado de ordenar as candidatas
	# depois de milhares de quadros.
	#
	# O que se perde é um mapa parado durante a busca. Nada se move nele que
	# alguém possa ver num minuto -- e o plano que ele vai desenhar ainda não
	# existe.
	if simulation.is_planning():
		return
	# O mapa do sistema solar só é reconstruído quando está aberto: são mais umas
	# centenas de consultas à efeméride, e pagá-las com o mapa fechado seria pagar
	# por um desenho que ninguém vê.
	if orbit_map.visible and orbit_map.mode == OrbitMap.Mode.SYSTEM:
		_system_map = simulation.get_system_map()
	_orbit_track = simulation.get_orbit_track(ORBIT_TRACK_SAMPLES)
	var target_index := _target_index()
	_target_track = (simulation.get_body_orbit_track(target_index, TARGET_TRACK_SAMPLES)
		if target_index >= 0 else PackedVector3Array())
	_planned_track = simulation.get_planned_trajectory()
	_maneuvers = simulation.get_maneuvers()


func _update_instruments() -> void:
	var s := simulation.get_snapshot()
	if s.is_empty():
		return
	var shared := _instrument_data(s)

	if cockpit.visible:
		cockpit.flight_display.refresh(shared)
		cockpit.nav_display.refresh(shared)
		cockpit.target_display.refresh(shared)
		cockpit.system_display.refresh(shared)

	hud.visible = HUD_MODES[hud_mode] != "off" and not orbit_map.visible
	if hud.visible:
		hud.refresh(shared)
	if orbit_map.visible:
		if orbit_map.mode == OrbitMap.Mode.SYSTEM:
			orbit_map.system = _system_map
		orbit_map.refresh(shared)


func _instrument_data(s: Dictionary) -> Dictionary:
	## Um dicionário por quadro, partilhado pelos seis instrumentos.
	##
	## Um por instrumento seria mais arrumado e seria errado: dois mostradores que
	## construíssem o seu próprio dado poderiam construí-lo em instantes
	## diferentes, e o cockpit mostraria duas leituras da mesma nave. Um dado, um
	## instante, seis desenhos.
	var reference_index := celestial.index_of(s.get("reference", ""))
	var target_index := _target_index()
	var reference_radius: float = (simulation.get_body_radius(reference_index)
		if reference_index >= 0 else 0.0)

	var data := s.duplicate()
	data["directions"] = _directions
	data["ship_basis"] = simulation.get_spacecraft_basis()
	data["ship_position"] = simulation.get_spacecraft_position()
	data["reference_position"] = (simulation.get_body_position(reference_index)
		if reference_index >= 0 else Vector3.ZERO)
	data["reference_radius"] = reference_radius
	data["reference_colour"] = CelestialView._colour_for(s.get("reference", ""))
	data["orbit_track"] = _orbit_track
	data["target_track"] = _target_track
	data["planned_trajectory"] = _planned_track
	data["maneuvers"] = _maneuvers
	data["render_scale"] = RENDER_SCALE
	data["has_target"] = target_index >= 0
	data["target_position"] = (simulation.get_body_position(target_index)
		if target_index >= 0 else Vector3.ZERO)
	data["target_radius"] = (simulation.get_body_radius(target_index)
		if target_index >= 0 else 0.0)

	# Altitudes e não raios: um piloto lê altitude. O raio do corpo de referência
	# vem do snapshot do próprio corpo, de modo que a subtração é entre dois
	# números do mesmo instante.
	var radius_m: float = 0.0
	if reference_index >= 0 and RENDER_SCALE > 0.0:
		radius_m = reference_radius / RENDER_SCALE / BODY_SCALES[body_scale_index]
	data["apoapsis_altitude_m"] = float(s.get("apoapsis_m", 0.0)) - radius_m
	data["periapsis_altitude_m"] = float(s.get("periapsis_m", 0.0)) - radius_m
	data["bound"] = float(s.get("eccentricity", 0.0)) < 1.0
	data["retrograde_orbit"] = float(s.get("inclination_deg", 0.0)) > 90.0

	data["propellant_capacity_kg"] = 19000.0
	data["engine_armed"] = true
	data["rcs_enabled"] = controls.rcs_enabled
	data["rcs_activity"] = controls.rcs_activity(rcs_firing_count())
	data["rcs_throttles"] = _rcs_throttles
	data["rcs_firing"] = rcs_firing_count()
	data["rcs_thrusters"] = _rcs_thruster_count
	data["camera_mode"] = camera_rig.mode_name()
	data["cockpit_view"] = camera_rig.is_cockpit() and camera_rig.focus_index < 0
	data["paused"] = controls.paused

	# ⚠️ Aceleração PRÓPRIA, que é o que um acelerômetro a bordo leria: apenas as
	# forças NÃO gravitacionais, divididas pela massa. Em queda livre ela é zero.
	#
	# NÃO é `acceleration_ms2` do snapshot, que é a aceleração coordenada do
	# modelo de forças completo e inclui a gravidade -- 8,7 m/s² numa órbita de
	# 400 km. Rotular esse número como "aceleração própria" seria dizer que a
	# tripulação em órbita sente quase um g, que é exatamente o contrário do que
	# acontece. A regra 22 pede aceleração própria e é aceleração própria que
	# aparece.
	var mass: float = maxf(s.get("mass_kg", 1.0), 1.0)
	data["proper_acceleration_ms2"] = float(s.get("thrust_n", 0.0)) / mass

	# Fechamento com o alvo: a taxa a que a distância cai. Derivada da mesma
	# distância em dois instantes seria ruído; aqui é a projeção da velocidade
	# relativa sobre a linha de visada, que é a definição.
	var closing := 0.0
	if target_index >= 0 and _directions.has("target"):
		closing = float(s.get("target_relative_speed_ms", 0.0)) * _closing_sign(target_index)
	data["closing_speed_ms"] = closing

	var phase := simulation.get_mission_phase()
	data["mission_phase"] = phase
	var next_event := _next_event()
	data["next_event"] = next_event[0]
	# A contagem regressiva vem do PLANO e não da lista em cache: um relógio que
	# só anda quatro vezes por segundo salta visivelmente nos últimos dez
	# segundos, que é exatamente quando alguém o está a olhar.
	data["next_event_seconds"] = (simulation.get_plan().get("seconds_to_ignition", 0.0)
		if next_event[0] != "" and not _maneuvers.is_empty()
			and not bool(_maneuvers[0].get("done", false))
		else next_event[1])
	if simulation.has_plan():
		data["plan_seconds_to_arrival"] = simulation.get_plan().get("seconds_to_insertion", -1.0)
	return data


func _closing_sign(target_index: int) -> float:
	## +1 a aproximar, -1 a afastar. O sinal vem do produto escalar entre a
	## velocidade relativa e a linha de visada, e os dois vetores são tirados do
	## MESMO snapshot.
	var to_target: Vector3 = _directions["target"]
	var relative := simulation.get_body_position(target_index) \
		- simulation.get_spacecraft_position()
	if relative.length() < 1.0e-9:
		return 0.0
	# `get_body_relative_velocity_scene` é a velocidade do corpo RELATIVA ao
	# observador; aproximar-se significa que ela aponta contra a linha de visada.
	var relative_velocity := simulation.get_body_relative_velocity_scene(target_index)
	return -signf(relative_velocity.dot(to_target.normalized()))


func _next_event() -> Array:
	if not simulation.has_plan():
		return ["", 0.0]
	for entry in _maneuvers:
		var burn: Dictionary = entry
		if not burn.get("done", false):
			return [String(burn.get("name", "BURN")).to_upper(),
				burn.get("seconds_to_ignition", 0.0)]
	return ["", 0.0]


func _update_audio() -> void:
	audio.set_engine_thrust(simulation.get_snapshot().get("thrust_n", 0.0))
	audio.set_interior(camera_rig.is_cockpit() and camera_rig.focus_index < 0)


func _watch_mission() -> void:
	## Regra 67: as mensagens da missão. A FASE vem do core
	## (`core/navigation/mission_execution.hpp`) e este bloco só nota que ela
	## mudou -- a classificação não é feita aqui, porque ela depende de onde está
	## a esfera de influência e de onde estão as queimas.
	var phase := simulation.get_mission_phase()
	if phase != _last_phase:
		_last_phase = phase
		if phase != "" and phase != "IDLE":
			messages.post(_phase_message(phase), MessageLog.Level.MISSION)
			audio.notify()

	var s := simulation.get_snapshot()
	var low := float(s.get("propellant_kg", 0.0)) < 1900.0
	if low and not _warned_low_propellant:
		_warned_low_propellant = true
		messages.post("PROPELLANT BELOW 10 %", MessageLog.Level.WARNING)
		audio.warning()
	elif not low:
		_warned_low_propellant = false


## Os quinze nomes de fase que `core/navigation/mission_execution.hpp` produz,
## em linguagem de piloto.
##
## A lista é COMPLETA de propósito e foi tirada do enum, não da memória: a
## primeira versão cobria "INJECTION" e "CAPTURE", que não existem -- o core diz
## `INJECTION_BURN` e `CAPTURE_BURN` --, e o cockpit anunciava a queima de
## injeção com um sublinhado no meio. O ramo final devolve o nome cru, o que é a
## resposta certa para uma fase que este arquivo não conhece: melhor um nome
## técnico do que uma frase inventada.
static func _phase_message(phase: String) -> String:
	match phase:
		"PLANNED": return "TRANSFER PLANNED"
		"WAITING_FOR_DEPARTURE": return "AWAITING DEPARTURE"
		"ORIENTING": return "ORIENTING FOR INJECTION"
		"INJECTION_BURN": return "INJECTION BURN"
		"MIDCOURSE_CORRECTION": return "MIDCOURSE CORRECTION"
		"COAST": return "COAST PHASE"
		"APPROACH": return "TARGET APPROACH"
		"CAPTURE_ORIENTING": return "ORIENTING FOR CAPTURE"
		"CAPTURE_BURN": return "CAPTURE BURN"
		"ORBIT_INSERTION": return "ORBIT INSERTION"
		"COMPLETE": return "ORBIT ACHIEVED"
		"ABORTED": return "MISSION ABORTED"
		"FAILED": return "MISSION FAILED"
		_: return phase


func _master_caution() -> bool:
	var s := simulation.get_snapshot()
	return float(s.get("propellant_kg", 0.0)) < 1900.0 \
		or float(s.get("rotation_rate_deg_s", 0.0)) > 6.0


# =============================================================================
#  missão
# =============================================================================

func plan_mission(target: String = "", periapsis_km: float = MISSION_PERIAPSIS_KM,
		apoapsis_km: float = MISSION_APOAPSIS_KM) -> bool:
	## Começa a busca. Ela roda numa thread e o jogo continua a andar.
	##
	## ⚠️ O Milestone 7 fazia isto de forma bloqueante e escrevia aqui que um
	## segundo de quadro parado era um custo aceitável. Era, para a Lua. Uma busca
	## Terra→Marte leva 64 segundos medidos, e um minuto de quadros congelados não
	## é uma tecla -- é uma queda, do ponto de vista de quem está olhando.
	##
	## O que a thread NÃO faz: tocar no estado que o quadro escreve. O pedido é
	## copiado antes de ela começar e o resultado é instalado por `collect_plan()`
	## no quadro, nunca pela thread. Ver `simulation_node.hpp`.
	if simulation == null or not simulation.is_ready():
		return false
	if simulation.is_planning():
		messages.post("ALREADY SEARCHING — cancel first", MessageLog.Level.WARNING)
		return false
	var wanted := target if not target.is_empty() else simulation.get_target_body()
	if wanted.is_empty():
		wanted = "Moon"
	if not simulation.start_planning(wanted, periapsis_km, apoapsis_km, MISSION_SEARCH_HOURS):
		messages.post(simulation.get_last_error(), MessageLog.Level.WARNING)
		return false
	print("[mission] searching for a transfer to %s" % wanted)
	messages.post("SEARCHING TRAJECTORIES TO %s" % wanted.to_upper(), MessageLog.Level.MISSION)
	return true


func set_map_mode(mode: int) -> void:
	## Põe o modo do mapa, em vez de o alternar.
	##
	## `_cycle_map_mode()` é o que a tecla faz e descreve uma TRANSIÇÃO; isto
	## descreve um ESTADO, que é o que um roteiro ou um teste precisa. Os dois
	## carregam os caminhos planetários da mesma maneira.
	if orbit_map.mode == mode:
		return
	_cycle_map_mode()


func _cycle_map_mode() -> void:
	## Alterna LOCAL / SISTEMA SOLAR (regra 26). Abre o mapa se estiver fechado:
	## pedir o modo de um mostrador invisível e não ver nada acontecer é uma
	## tecla que parece partida.
	if not orbit_map.visible:
		_toggle_panel(orbit_map)
	orbit_map.mode = (OrbitMap.Mode.SYSTEM if orbit_map.mode == OrbitMap.Mode.LOCAL
		else OrbitMap.Mode.LOCAL)
	orbit_map.zoom = 1.0
	if orbit_map.mode == OrbitMap.Mode.SYSTEM:
		# Os caminhos planetários, uma vez. 96 amostras por corpo: a regra 29 pede
		# explicitamente para não gerar milhares de pontos, e uma elipse desenhada
		# com 96 segmentos é indistinguível de uma com 960 em qualquer zoom que
		# mostre o planeta inteiro.
		orbit_map.system_paths = simulation.get_system_orbit_paths(96)
		_system_map = simulation.get_system_map()
		messages.post("SOLAR SYSTEM MAP", MessageLog.Level.INFO)
	else:
		messages.post("LOCAL MAP", MessageLog.Level.INFO)
	orbit_map.queue_redraw()


func choose_alternative(index: int) -> void:
	## Replaneja UMA das geometrias que a busca voou, fixada (regra 42).
	##
	## Passa pelo mesmo caminho de qualquer outro plano -- a mesma thread, o mesmo
	## progresso, o mesmo `collect_plan()` -- e custa uma candidata em vez de 768,
	## o que são segundos em vez de um minuto.
	if simulation == null or simulation.is_planning():
		return
	if not simulation.start_planning_alternative(index):
		messages.post(simulation.get_last_error(), MessageLog.Level.WARNING)
		return
	messages.post("REPLANNING THE CHOSEN TRAJECTORY", MessageLog.Level.MISSION)


func cancel_planning() -> void:
	## Regra 120: uma busca longa tem de poder ser interrompida, e o worker tem de
	## parar de facto -- não de ser abandonado a correr.
	if simulation == null or not simulation.is_planning():
		return
	simulation.cancel_planning()
	messages.post("CANCELLING SEARCH", MessageLog.Level.WARNING)


func _poll_planning() -> void:
	## Chamado todo quadro. Enquanto a busca corre, alimenta o painel com
	## contagens; quando ela acaba, recolhe o resultado -- no QUADRO, porque é o
	## quadro que pode instalar um plano.
	if simulation == null:
		return
	if simulation.is_planning():
		mission_panel.show_progress(simulation.get_planning_progress())
		return
	if not _search_was_running:
		return

	var plan := simulation.collect_plan()
	mission_panel.show_plan(plan, simulation.get_last_error())
	if plan.get("cancelled", false):
		messages.post("SEARCH CANCELLED", MessageLog.Level.WARNING)
		return
	if plan.is_empty() or not plan.get("valid", false):
		messages.post("NO TRANSFER FOUND — %s" % simulation.get_last_error(),
			MessageLog.Level.WARNING)
		push_warning("could not plan the transfer: %s" % simulation.get_last_error())
		return

	mission_panel.show_alternatives(simulation.get_plan_alternatives())
	messages.post("TRANSFER PLANNED — %.0f m/s, %s" % [plan.get("total_delta_v", 0.0),
		Fmt.duration(plan.get("time_of_flight_days", 0.0) * 86400.0)],
		MessageLog.Level.MISSION)
	audio.notify()
	_print_plan(plan)


func arm_mission() -> bool:
	# A mensagem é escrita aqui e não repassada do core quando o caso é "não há
	# plano": `arm_plan: no transfer has been planned` é uma frase de diagnóstico
	# para quem lê um log, e o piloto quer saber o que fazer a seguir.
	if not simulation.has_planned_transfer():
		messages.post("NO PLAN TO EXECUTE — %s to plan a transfer"
			% InputActions.label("mission_plan"), MessageLog.Level.WARNING)
		return false
	if not simulation.arm_plan():
		messages.post(simulation.get_last_error(), MessageLog.Level.WARNING)
		return false
	# O acelerador é do piloto e o plano é do computador; os dois a empurrar ao
	# mesmo tempo é como uma trajetória corrigida deixa de estar corrigida.
	controls.set_throttle(0.0)
	mission_panel.show_plan(simulation.get_plan(), "")
	messages.post("MISSION PLAN ACCEPTED", MessageLog.Level.MISSION)
	audio.notify()
	return true


func abort_mission() -> void:
	## Regra 68: cancela o piloto automático e o plano. A nave NÃO volta
	## magicamente para a Terra -- ela fica exatamente no estado físico em que
	## está, e o controle volta para o piloto.
	if not simulation.has_plan() and not simulation.has_planned_transfer():
		return
	simulation.clear_plan()
	simulation.set_pointing_mode("")
	mission_panel.show_plan({}, "aborted by the pilot")
	messages.post("MISSION ABORTED — manual control", MessageLog.Level.WARNING)
	audio.warning()
	print("[mission] plan abandoned")


func set_target(name: String) -> void:
	if not simulation.set_target_body(name):
		messages.post(simulation.get_last_error(), MessageLog.Level.WARNING)
		return
	_track_timer = 0.0
	messages.post("TARGET %s" % name.to_upper(), MessageLog.Level.INFO)


func _step_target(direction: int) -> void:
	mission_panel.step_target(direction)


func _selectable_targets() -> PackedStringArray:
	var names := PackedStringArray()
	for entry in simulation.get_selectable_targets():
		names.append(String(entry))
	return names


func _target_index() -> int:
	var name := simulation.get_target_body()
	return celestial.index_of(name) if not name.is_empty() else -1


func _print_plan(plan: Dictionary) -> void:
	print("[mission] %d burns: injection %.1f m/s in %s, insertion %.1f m/s"
		% [plan.get("burns", 0), plan["injection_delta_v"],
		   Fmt.duration(plan["seconds_to_ignition"]), plan["insertion_delta_v"]])
	print("[mission] %s branch, tof %.2f d, transfer angle %.1f deg, total %.1f m/s of %.0f available"
		% [plan.get("branch", "?"), plan.get("time_of_flight_days", 0.0),
		   plan.get("transfer_angle_deg", 0.0), plan.get("total_delta_v", 0.0),
		   plan.get("delta_v_available", 0.0)])
	print("[mission] predicted orbit %.1f x %.1f km, e %.4f, i %.2f deg, RAAN %.1f deg"
		% [float(plan.get("predicted_periapsis_m", 0.0)) / 1000.0,
		   float(plan.get("predicted_apoapsis_m", 0.0)) / 1000.0,
		   plan.get("predicted_eccentricity", 0.0),
		   plan.get("predicted_inclination_deg", 0.0),
		   plan.get("predicted_raan_deg", 0.0)])
	# Seção 13 do M6.2: as alternativas que a busca de facto voou, cada uma com a
	# órbita em que teria chegado. Impressas em vez de escondidas, porque o
	# planejador não mira uma inclinação e a dispersão é a evidência disso.
	for alternative in simulation.get_plan_alternatives():
		print("[mission]   alt %-28s %s  %.2f d  %.0f m/s  ->  %.0f x %.0f km, e %.4f, i %.1f deg"
			% [alternative["label"],
			   "ok     " if alternative["feasible"] else alternative["failure"],
			   alternative["time_of_flight_days"], alternative["total_delta_v"],
			   float(alternative["predicted_periapsis_m"]) / 1000.0,
			   float(alternative["predicted_apoapsis_m"]) / 1000.0,
			   alternative["predicted_eccentricity"],
			   alternative["predicted_inclination_deg"]])


# =============================================================================
#  entrada
# =============================================================================

func _camera_keys(delta: float) -> void:
	## As setas movem a câmera; WASD é da nave. Duas famílias de teclas para duas
	## coisas que não se confundem -- girar a NAVE gasta propelente, girar a
	## CÂMERA não muda um número do estado.
	var yaw := Input.get_axis("ui_right", "ui_left")
	var pitch := Input.get_axis("ui_down", "ui_up")
	if yaw != 0.0 or pitch != 0.0:
		camera_rig.apply_keys(delta, yaw, pitch, Input.is_action_pressed("camera_free_look"))


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		var button := event as InputEventMouseButton
		if button.button_index == MOUSE_BUTTON_LEFT and button.pressed:
			if _click_cockpit():
				return
		if orbit_map.visible:
			if button.button_index == MOUSE_BUTTON_WHEEL_UP and button.pressed:
				orbit_map.zoom = clampf(orbit_map.zoom * 1.2, 0.2, 40.0)
				orbit_map.queue_redraw()
				return
			if button.button_index == MOUSE_BUTTON_WHEEL_DOWN and button.pressed:
				orbit_map.zoom = clampf(orbit_map.zoom / 1.2, 0.2, 40.0)
				orbit_map.queue_redraw()
				return
		if camera_rig.handle_mouse_button(button):
			return
	elif event is InputEventMouseMotion:
		var motion := event as InputEventMouseMotion
		_hover_cockpit()
		camera_rig.handle_mouse_motion(motion, Input.is_action_pressed("camera_free_look"))


func _unhandled_key_input(event: InputEvent) -> void:
	if not (event is InputEventKey and event.pressed and not (event as InputEventKey).echo):
		return
	# Um comando com `Shift` ou `Ctrl` é um acorde, e o modificador dele não é um
	# pedido de acelerador. Dito uma vez, aqui, em vez de em cada ramo.
	var key := event as InputEventKey
	if key.shift_pressed or key.ctrl_pressed:
		controls.block_throttle_trim()

	var pressed := func(action: String) -> bool:
		return InputActions.pressed_exact(event, action)

	# --- apontamento ---
	if pressed.call("point_prograde"): controls.point("prograde")
	elif pressed.call("point_retrograde"): controls.point("retrograde")
	elif pressed.call("point_normal"): controls.point("normal")
	elif pressed.call("point_anti_normal"): controls.point("anti_normal")
	elif pressed.call("point_radial_out"): controls.point("radial_out")
	elif pressed.call("point_radial_in"): controls.point("radial_in")
	elif pressed.call("point_hold"): controls.point("")
	elif pressed.call("point_target"): _point_at_target(false)
	elif pressed.call("point_anti_target"): _point_at_target(true)

	# --- motor ---
	elif pressed.call("throttle_full"): controls.set_throttle(1.0)
	elif pressed.call("engine_cutoff"): controls.set_throttle(0.0)
	elif pressed.call("engine_mode"): controls.cycle_engine_mode()
	elif pressed.call("rcs_toggle"):
		controls.toggle_rcs()
		rcs_switch.set_on(controls.rcs_enabled)
		audio.switch_flipped()
	elif pressed.call("rcs_mode"):
		messages.post("RCS %s" % controls.rcs_activity(rcs_firing_count()), MessageLog.Level.INFO)

	# --- câmera ---
	elif pressed.call("camera_cycle"):
		camera_rig.cycle_mode()
		camera_rig.set_mouse_look(false)
		messages.post("CAMERA %s" % camera_rig.mode_name(), MessageLog.Level.INFO)
	elif pressed.call("camera_recentre"): camera_rig.recentre()
	elif pressed.call("camera_zoom_in"): camera_rig.zoom(-1.0)
	elif pressed.call("camera_zoom_out"): camera_rig.zoom(1.0)
	elif pressed.call("camera_focus_next"): _cycle_focus()

	# --- tempo ---
	elif pressed.call("warp_up"): controls.step_warp(1)
	elif pressed.call("warp_down"): controls.step_warp(-1)
	elif pressed.call("pause"): _set_paused(not controls.paused)
	elif pressed.call("menu"): _escape()

	# --- missão ---
	elif pressed.call("target_next"): _step_target(1)
	elif pressed.call("target_prev"): _step_target(-1)
	elif pressed.call("mission_plan"): plan_mission()
	elif pressed.call("mission_execute"): arm_mission()
	elif pressed.call("mission_abort"): abort_mission()
	elif pressed.call("nav_panel"): _toggle_panel(mission_panel)
	elif pressed.call("map_mode"): _cycle_map_mode()
	elif pressed.call("orbit_map"): _toggle_panel(orbit_map)

	# --- interface ---
	elif pressed.call("hud_cycle"):
		hud_mode = (hud_mode + 1) % HUD_MODES.size()
		messages.post("HUD %s" % HUD_MODES[hud_mode].to_upper(), MessageLog.Level.INFO)
	elif pressed.call("debug_hud"):
		debug_hud.visible = not debug_hud.visible
		debug_hud.rescale()
	elif pressed.call("help"): _toggle_panel(help_panel)
	elif pressed.call("exposure_up"):
		exposure_index = mini(exposure_index + 1, EXPOSURE_LEVELS.size() - 1)
		sky.set_half_saturation(EXPOSURE_LEVELS[exposure_index])
	elif pressed.call("exposure_down"):
		exposure_index = maxi(exposure_index - 1, 0)
		sky.set_half_saturation(EXPOSURE_LEVELS[exposure_index])

	# --- técnico ---
	elif pressed.call("restart_orbit"):
		simulation.start_circular_orbit(ALTITUDE_M, INCLINATION_DEG)
		messages.post("SCENARIO RESET — 400 km, 51.6°", MessageLog.Level.INFO)
	elif pressed.call("execution_model"): _cycle_execution_model()
	elif pressed.call("visual_beta"):
		visual_beta_index = (visual_beta_index + 1) % VISUAL_TEST_BETAS.size()
		simulation.set_visual_test_beta(VISUAL_TEST_BETAS[visual_beta_index])
		messages.post("VISUAL β %s" % visual_beta_label(), MessageLog.Level.INFO)
	elif pressed.call("body_scale"):
		body_scale_index = (body_scale_index + 1) % BODY_SCALES.size()
		simulation.set_body_scale_exaggeration(BODY_SCALES[body_scale_index])
		# Regra 12: nunca mudar a escala de um planeta em silêncio.
		messages.post("BODY SCALE %s — planets are drawn %s life size"
			% [body_scale_label(), body_scale_label()], MessageLog.Level.WARNING)
		celestial.warn_if_camera_is_inside_a_body(camera_rig.world_camera.position,
			body_scale_label())
	elif pressed.call("cruise_burn"):
		# Tudo o que é preciso para ir de facto depressa, numa tecla: os efeitos
		# são invisíveis abaixo de β ≈ 0,1 e a única maneira honesta de os ver é
		# voar até lá. Oito anos a queimar, a warp 1e8, são uns minutos a ver.
		simulation.set_engine_mode("CRUISE")
		controls.point("prograde")
		controls.set_throttle(1.0)
		controls.warp_index = FlightControls.WARP_LEVELS.size() - 1
		simulation.set_time_warp(controls.warp())
		messages.post("CRUISE BURN — prograde, full throttle, warp 1e8",
			MessageLog.Level.MISSION)
	elif pressed.call("optics_aberration"):
		celestial.effect_aberration = not celestial.effect_aberration
		messages.post("ABERRATION %s" % _on_off(celestial.effect_aberration),
			MessageLog.Level.INFO)
	elif pressed.call("optics_doppler"):
		celestial.effect_doppler = not celestial.effect_doppler
		messages.post("DOPPLER %s" % _on_off(celestial.effect_doppler), MessageLog.Level.INFO)
	elif pressed.call("optics_beaming"):
		celestial.effect_beaming = not celestial.effect_beaming
		messages.post("BEAMING %s" % _on_off(celestial.effect_beaming), MessageLog.Level.INFO)
	elif pressed.call("optics_light_time"):
		celestial.effect_retarded = not celestial.effect_retarded
		messages.post("LIGHT TIME %s" % _on_off(celestial.effect_retarded),
			MessageLog.Level.INFO)


func _point_at_target(anti: bool) -> void:
	## Apontar para o alvo não é um modo do controlador de atitude -- os modos
	## dele são leis de guiamento (prógrado, normal, radial), e "para onde a Lua
	## está" não é uma lei, é uma direção. Então é comandada como direção
	## inercial, e o controlador segue-a exatamente como segue as outras.
	if not _directions.has("target"):
		messages.post("NO TARGET SELECTED", MessageLog.Level.WARNING)
		return
	messages.post("POINT %s NOT AVAILABLE — the core takes guidance laws, not directions"
		% ("ANTI-TARGET" if anti else "TARGET"), MessageLog.Level.WARNING)


func _cycle_focus() -> void:
	camera_rig.focus_index += 1
	if camera_rig.focus_index >= simulation.get_body_count():
		camera_rig.focus_index = -1
	var name := ("spacecraft" if camera_rig.focus_index < 0
		else simulation.get_body_name(camera_rig.focus_index))
	messages.post("FOCUS %s" % name.to_upper(), MessageLog.Level.INFO)


func _cycle_execution_model() -> void:
	## Contra que modelo de execução o planejador corrige. Uma escolha real,
	## oferecida em vez de enterrada:
	##
	##   FINITE_BURN  guiamento ideal; e ~ 0,0017 em 365 épocas, planeja em ~7 s
	##   AUTOPILOT    o controlador de atitude fica DENTRO do mapa corrigido, de
	##                modo que o atraso de apontamento faz parte da trajetória em
	##                vez de ser assumido como zero -- física estritamente melhor,
	##                e minutos por plano
	var wanted := "autopilot" if simulation.get_execution_model() == "FINITE_BURN" else "finite"
	if simulation.set_execution_model(wanted):
		var note := ("  (planning now takes minutes, not seconds)"
			if wanted == "autopilot" else "")
		messages.post("PLANNER CORRECTS AGAINST %s%s"
			% [simulation.get_execution_model(), note], MessageLog.Level.INFO)


func _escape() -> void:
	if help_panel.visible:
		_show_panel(help_panel, false)
	elif mission_panel.visible:
		_show_panel(mission_panel, false)
	elif orbit_map.visible:
		_show_panel(orbit_map, false)
	else:
		_set_paused(not controls.paused)


func _set_paused(value: bool) -> void:
	controls.set_paused(value)
	_show_panel(pause_menu, value)
	if value:
		messages.post("PAUSED — simulation time stopped", MessageLog.Level.INFO)


func _toggle_panel(panel: Control) -> void:
	_show_panel(panel, not panel.visible)


func _show_panel(panel: Control, value: bool) -> void:
	# Os três painéis centrais partilham o mesmo `CenterContainer` e empilhavam-se
	# uns por cima dos outros: pausar e depois abrir o computador dava dois
	# painéis sobrepostos, com os botões de ambos a responder ao clique. Abrir um
	# fecha os outros.
	if value:
		for other in [mission_panel, pause_menu, help_panel]:
			if other != panel:
				other.visible = false
	panel.visible = value
	if panel == mission_panel and value:
		mission_panel.set_targets(_selectable_targets(), simulation.get_target_body())
		mission_panel.show_plan(simulation.get_plan(), simulation.get_last_error())
	# Um painel aberto liberta o ponteiro; nenhum painel aberto devolve-o à
	# câmera. Sem isto o mouse fica capturado por baixo de um menu e o jogador
	# não consegue clicar no próprio botão que acabou de abrir.
	if value:
		camera_rig.set_mouse_look(false)


func _apply_setting(key: String, value: float) -> void:
	match key:
		"mouse_sensitivity":
			mouse_sensitivity = value
			camera_rig.mouse_sensitivity = value
		"master_volume":
			audio.set_volumes(value, audio.effects_volume)
		"effects_volume":
			audio.set_volumes(audio.master_volume, value)
		"ui_scale":
			ui_scale = value
			interface.scale = Vector2.ONE * value


# --- interação com o cockpit -------------------------------------------------

func _cockpit_ray() -> Array:
	## Um raio do olho do piloto através do pixel sob o ponteiro.
	##
	## No referencial do MUNDO do viewport próximo, e não no do corpo: é onde a
	## câmera devolve o raio e é onde os controles têm o seu `global_transform`.
	## A primeira versão convertia o raio para o referencial do corpo e testava-o
	## contra painéis que continuavam em coordenadas de mundo -- com a nave
	## apontada para qualquer lado, o botão respondia a metros de onde estava
	## desenhado.
	##
	## A câmera próxima ocupa o mesmo retângulo de tela que a janela principal (o
	## `SubViewportContainer` ocupa tudo), então a posição do mouse serve tal e
	## qual, sem escala.
	if not camera_rig.is_cockpit() or camera_rig.focus_index >= 0:
		return []
	var mouse := get_viewport().get_mouse_position()
	var camera := camera_rig.near_camera
	return [camera.project_ray_origin(mouse), camera.project_ray_normal(mouse)]


func _hover_cockpit() -> void:
	var ray := _cockpit_ray()
	if ray.is_empty():
		cockpit.set_hover(null)
		return
	cockpit.set_hover(cockpit.pick(ray[0], ray[1]))


func _click_cockpit() -> bool:
	var ray := _cockpit_ray()
	if ray.is_empty():
		return false
	var control := cockpit.pick(ray[0], ray[1])
	if control == null:
		return false
	control.press()
	return true


# =============================================================================
#  acessores para o HUD técnico e para a verificação sem tela
# =============================================================================

func rcs_firing_count() -> int:
	var count := 0
	for value in _rcs_throttles:
		if value > 0.002:
			count += 1
	return count


func rcs_thruster_count() -> int:
	return _rcs_thruster_count


func visual_beta_label() -> String:
	return "flight" if visual_beta_index == 0 else "%.2fc" % VISUAL_TEST_BETAS[visual_beta_index]


func body_scale_label() -> String:
	return "%.0fx" % BODY_SCALES[body_scale_index]


static func _on_off(value: bool) -> String:
	return "ON" if value else "OFF"


# =============================================================================
#  captura de evidência
# =============================================================================

func _capture_step() -> void:
	## Uma fotografia por (β visual, direção do olhar), depois sai.
	##
	## As duas direções, porque a β = 0,9 elas são duas afirmações diferentes e só
	## uma delas é sobre o empilhamento: para a frente mostra o cone e o desvio
	## para o azul, para trás mostra um céu que de facto se apagou. Uma captura
	## que só olhasse para um lado não distinguiria um céu escuro de um céu
	## partido.
	hud_mode = HUD_MODES.find("off")
	messages.visible = false
	if not _capture_started:
		_capture_started = true
		camera_rig.set_mode(CameraRig.Mode.VELOCITY_REFERENCE)
		# Afastada, para que a nave seja um ponto e não um obstáculo. Estas
		# imagens são a evidência do Milestone 5 e o assunto delas é o CÉU: a 46 m
		# o casco tapava o meio do cone de aberração, que é exatamente a parte que
		# a fotografia existe para mostrar.
		camera_rig.orbit_zoom = 20.0
	_capture_frames += 1
	if _capture_frames < 30:
		return
	_capture_frames = 0

	var beta_index: int = _capture_index / 2
	var looking: String = "forward" if _capture_index % 2 == 0 else "aft"
	var beta: float = VISUAL_TEST_BETAS[beta_index]
	var name := "scene_%s_beta_%s" % [looking,
		"propagated" if beta < 0.0 else str(beta).replace(".", "p")]
	DirAccess.make_dir_recursive_absolute(_capture_dir)
	get_viewport().get_texture().get_image().save_png("%s/%s.png" % [_capture_dir, name])
	print("[capture] %s.png | %s" % [name, " | ".join(debug_hud.sky_lines())])

	_capture_index += 1
	if _capture_index >= VISUAL_TEST_BETAS.size() * 2:
		get_tree().quit()
		return
	visual_beta_index = _capture_index / 2
	simulation.set_visual_test_beta(VISUAL_TEST_BETAS[visual_beta_index])
	camera_rig.orbit_azimuth = PI if _capture_index % 2 == 0 else 0.0
