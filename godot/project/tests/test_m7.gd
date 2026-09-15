extends SceneTree
## Os testes do Milestone 7 (regra 62).
##
## O que eles cobrem é o que o M7 acrescentou e nada mais: o snapshot que chega
## ao cockpit, a seleção de alvo, a formatação de unidades, o estado da câmera, o
## mapeamento dos jatos de RCS e da pluma, e os pontos de trajetória que o mapa
## recebe. A física continua verificada por `ctest` em C++, e este arquivo não
## repete uma única conta dela -- transformar o M7 noutra campanha de validação
## científica é exatamente o que a regra 62 proíbe.
##
## Sem tela. Nenhuma asserção aqui olha para um pixel: o que uma imagem pode
## decidir está em `scripts/m7_screenshots.sh` e é julgado por uma pessoa
## (regra 60).
##
##     scripts/godot_tests.sh

## Quantas asserções esta suíte tem de correr.
##
## ⚠️ Um `SCRIPT ERROR` no Godot não reprova nada: ele imprime, a função aborta,
## e a suíte segue e termina com "0 failed" -- com menos verificações do que
## tinha antes. Foi assim que um erro de análise no `orbit_map.gd` tirou seis
## asserções desta suíte sem que ela ficasse vermelha.
##
## Um número esperado transforma cobertura perdida em falha, que é o que ela é.
## Sobe quando se acrescentam testes; nunca desce em silêncio.
const EXPECTED_CHECKS := 156

var failures := 0
var checks := 0


func _initialize() -> void:
	InputActions.install()

	var simulation := SpaceflightSimulation.new()
	root.add_child(simulation)
	var kernels := ProjectSettings.globalize_path("res://").path_join("../../kernels/spice")
	if not simulation.configure(kernels.simplify_path(), "2026-01-01T00:00:00"):
		# Sem kernels não é uma falha, é uma ausência. Mesma política dos testes
		# de C++ e do script de GPU: 77 é Skipped no CTest, e uma máquina que não
		# pôde correr uma suíte não a reprovou.
		printerr("SKIP: no SPICE kernels (%s)" % simulation.get_last_error())
		quit(77)
		return
	simulation.start_circular_orbit(400_000.0, 51.6)
	simulation.align_attitude_to_flight(25.0)
	simulation.set_render_scale(1.0e-6)
	simulation.advance(0.016)

	_test_units()
	_test_input_map()
	_test_snapshot(simulation)
	_test_target_selection(simulation)
	_test_flight_directions(simulation)
	_test_orbit_track(simulation)
	_test_apsis_markers(simulation)
	_test_planned_trajectory_frame(simulation)
	_test_rcs_actuator_mapping(simulation)
	_test_engine_plume()
	_test_camera_modes()
	_test_cockpit_hit_test()
	_test_instrument_data_survives_a_display(simulation)

	if checks < EXPECTED_CHECKS:
		failures += 1
		printerr("\nFAIL só %d de %d verificações correram -- algo abortou a meio"
			% [checks, EXPECTED_CHECKS])
	print("\n%d checks, %d failed" % [checks, failures])
	quit(1 if failures > 0 else 0)


# --- asserções ---------------------------------------------------------------

func check(condition: bool, what: String) -> void:
	checks += 1
	if condition:
		print("  ok    %s" % what)
	else:
		failures += 1
		printerr("  FAIL  %s" % what)


func check_equal(actual: Variant, expected: Variant, what: String) -> void:
	check(actual == expected, "%s  (got %s, want %s)" % [what, actual, expected])


func check_near(actual: float, expected: float, tolerance: float, what: String) -> void:
	# `%g` NÃO existe no `%` do GDScript -- ele aceita %s %c %d %o %x %X %f %v %%
	# e mais nada. Um `%g` aqui fazia a própria mensagem de asserção falhar a
	# formatar, e o teste imprimia "ok  %s (got %.6f, ...)" com os marcadores
	# literais. A tolerância passa por `Fmt.sci`, que é o substituto do projeto.
	check(absf(actual - expected) <= tolerance,
		"%s  (got %s, want %s ± %s)"
			% [what, Fmt.sci(actual, 6), Fmt.sci(expected, 6), Fmt.sci(tolerance, 2)])


# --- casos -------------------------------------------------------------------

func _test_units() -> void:
	print("\nunits (rule 74)")
	# O que a regra 74 proíbe explicitamente.
	check(not Fmt.distance(3.84e8).contains("384000000"), "384 000 km is not printed in metres")
	check_equal(Fmt.distance(842.0), "842 m", "metres below a kilometre")
	check_equal(Fmt.distance(18_400.0), "18.4 km", "kilometres")
	check(Fmt.distance(1.496e11).ends_with("AU"), "astronomical units past 0.01 AU")
	check_equal(Fmt.speed(7680.0), "7.680 km/s", "km/s")
	check(Fmt.speed(3.6e7).ends_with("c"), "a fraction of c past 0.001 c")
	check_equal(Fmt.speed(842.0), "842.0 m/s", "m/s below a km/s")
	# `%e` não existe no `%` do GDScript; `Fmt.sci` é o substituto e tem de dar
	# dígitos SIGNIFICATIVOS, não dezasseis casas.
	check_equal(Fmt.sci(4.014566457044566e-13), "4.015e-13", "four significant digits")
	check_equal(Fmt.sci(0.8072), "0.8072", "decimal inside the readable range")
	check_equal(Fmt.countdown(6137.0), "T-01:42:17", "mission countdown")
	check_equal(Fmt.duration(-1.0), "--", "a duration that does not exist says so")


func _test_input_map() -> void:
	print("\ninput map (rule 13)")
	check(InputMap.has_action("pitch_up"), "attitude actions are registered")
	check(InputMap.has_action("orbit_map"), "the orbital map has a binding")
	check(InputMap.has_action("mission_abort"), "abort has a binding (rule 68)")
	# Regra 13: nada de keycodes espalhados. O teste possível é que cada ação da
	# tabela exista no mapa e que o rótulo saia do mapa e não da tabela.
	var groups := InputActions.by_group()
	var count := 0
	for group in groups:
		for entry in groups[group]:
			count += 1
			if not InputMap.has_action(entry["action"]):
				check(false, "action %s missing from the map" % entry["action"])
	check(count == InputActions.BINDINGS.size(), "every binding reaches the map")
	check_equal(InputActions.label("point_retrograde"), "Shift+P",
		"modifiers appear in the label")


func _test_snapshot(simulation: SpaceflightSimulation) -> void:
	print("\nsnapshot reaching the cockpit (rule 19)")
	var s := simulation.get_snapshot()
	for key in ["altitude_m", "speed_ms", "acceleration_ms2", "throttle", "propellant_kg",
			"mass_kg", "apoapsis_m", "periapsis_m", "eccentricity", "inclination_deg",
			"target", "target_distance_m", "target_relative_speed_ms", "beta",
			"lorentz_factor_minus_one", "clock_difference_s", "thrust_n", "mass_flow_kg_s"]:
		check(s.has(key), "snapshot carries %s" % key)
	check_near(s["altitude_m"], 400_000.0, 2000.0, "the parking orbit is where it was asked for")
	check_near(s["inclination_deg"], 51.6, 0.01, "inclination survives the round trip")


func _test_target_selection(simulation: SpaceflightSimulation) -> void:
	print("\ntarget selection (rule 21)")
	var targets := simulation.get_selectable_targets()
	check(targets.size() > 0, "there are selectable targets")
	check(targets.has("Moon"), "the Moon is selectable")
	# Baricentros são pontos no vazio e não destinos. O catálogo distingue-os
	# pelo raio, e é o catálogo que decide -- não uma lista de nomes na interface.
	for name in targets:
		check(not String(name).contains("Barycenter"), "%s is a place, not a barycentre" % name)

	check(simulation.set_target_body("Moon"), "the Moon can be targeted")
	check_equal(simulation.get_target_body(), "Moon", "the target reads back")
	simulation.advance(0.016)
	check(simulation.get_snapshot()["target_distance_m"] > 3.0e8,
		"the Moon is roughly where the Moon is")
	check(not simulation.set_target_body("Vulcan"), "an unknown body is refused")
	check(simulation.get_last_error().contains("Vulcan"), "and the refusal says which")


func _test_flight_directions(simulation: SpaceflightSimulation) -> void:
	print("\nflight director markers (rule 18)")
	var d := simulation.get_flight_directions()
	for key in ["prograde", "retrograde", "normal", "anti_normal", "radial_out", "radial_in",
			"nose", "target"]:
		check(d.has(key), "direction %s is published" % key)
	var prograde: Vector3 = d["prograde"]
	var retrograde: Vector3 = d["retrograde"]
	check_near(prograde.length(), 1.0, 1.0e-5, "prograde is a unit vector")
	check_near(prograde.dot(retrograde), -1.0, 1.0e-5, "retrograde is exactly the opposite")
	check_near(prograde.dot(d["normal"]), 0.0, 1.0e-5, "normal is perpendicular to the velocity")
	check_near(Vector3(d["radial_out"]).dot(d["radial_in"]), -1.0, 1.0e-5,
		"radial in and out are opposite")
	# A polarização de nadir: o nariz parte 25 graus abaixo do prógrado, e é isso
	# que põe a Terra na janela no primeiro quadro (regra 66).
	var nose: Vector3 = d["nose"]
	check_near(rad_to_deg(nose.angle_to(prograde)), 25.0, 1.0,
		"the nose starts 25 degrees below prograde")


func _test_orbit_track(simulation: SpaceflightSimulation) -> void:
	print("\ntrajectory points reaching the map (rules 54, 77)")
	var track := simulation.get_orbit_track(96)
	check_equal(track.size(), 96, "the orbit comes back sampled as asked")

	var s := simulation.get_snapshot()
	var centre := simulation.get_body_position(_index_of(simulation, s["reference"]))
	var scale: float = simulation.get_render_scale()
	var near := INF
	var far := 0.0
	for point in track:
		var r: float = (point - centre).length() / scale
		near = minf(near, r)
		far = maxf(far, r)
	# Os pontos são da MESMA elipse que o snapshot reporta. Se o mapa desenhasse
	# a sua própria órbita, seria aqui que os dois discordariam.
	check_near(near, s["periapsis_m"], s["periapsis_m"] * 1.0e-3, "the track reaches periapsis")
	check_near(far, s["apoapsis_m"], s["apoapsis_m"] * 1.0e-3, "the track reaches apoapsis")
	check((track[0] - track[track.size() - 1]).length() / scale < 5.0e4,
		"a bound orbit closes on itself")

	var moon := simulation.get_body_orbit_track(_index_of(simulation, "Moon"), 64)
	check_equal(moon.size(), 64, "the target path comes back sampled")
	var lunar_far := 0.0
	var lunar_near := INF
	for point in moon:
		var r: float = (point - centre).length() / scale
		lunar_near = minf(lunar_near, r)
		lunar_far = maxf(lunar_far, r)
	# Amostrado da EFEMÉRIDE, não de uma elipse: o perigeu e o apogeu lunares
	# reais são 363 300 e 405 500 km, e a variação entre eles é a prova de que
	# não é um círculo desenhado.
	check(lunar_near > 3.4e8 and lunar_far < 4.2e8, "the lunar path has the right size")
	check(lunar_far - lunar_near > 1.0e7, "and it is not a circle")


func _test_apsis_markers(simulation: SpaceflightSimulation) -> void:
	## Regra 20: o instrumento diz o que sabe, e uma órbita circular NÃO SABE onde
	## está o apsis.
	##
	## ⚠️ Os pontos da órbita chegam ao mostrador em float de 32 bits e em unidades
	## de cena. Numa órbita de estacionamento a 400 km a amplitude do raio em toda
	## a volta são METROS -- da ordem do próprio ulp --, e o "ponto mais distante
	## da amostra" passa a ser decidido pelo arredondamento. Medido antes da
	## correção: o índice do apoapsis saltava entre 0, 3, 115 e 119 e o eixo do
	## desenho girava até 37 graus de um quadro para o seguinte, com o marcador
	## amarelo a dar voltas à elipse e a nave a saltar com ele.
	print("\numa órbita circular não tem apsis para apontar (regra 20)")

	var reference := _index_of(simulation, simulation.get_snapshot()["reference"])
	var scale: float = simulation.get_render_scale()
	var nav := NavDisplay.new()
	root.add_child(nav)

	var spread := func() -> float:
		var s := simulation.get_snapshot()
		return absf(float(s["apoapsis_m"]) - float(s["periapsis_m"]))
	var extent := func(track: PackedVector3Array, centre: Vector3) -> float:
		var out := 0.0
		for point in track:
			out = maxf(out, (point - centre).length())
		return out

	var centre := simulation.get_body_position(reference)
	var track := simulation.get_orbit_track(128)
	check(spread.call() < 1.0e3,
		"a órbita de partida é circular a menos de um km (%.1f m)" % spread.call())
	check(not NavDisplay.apsides_resolved(spread.call(), extent.call(track, centre), scale),
		"e o mostrador recusa-se a apontar um apsis nela")

	# O eixo do desenho tem de ser o mesmo no quadro seguinte. Ele é o que leva a
	# nave e os marcadores ao sítio; se ele roda, tudo o que está em cima dele
	# salta, mesmo que a curva desenhada seja um círculo e não se note.
	var worst := 0.0
	var previous := Vector3.ZERO
	for frame in range(20):
		simulation.advance(0.016)
		centre = simulation.get_body_position(reference)
		track = simulation.get_orbit_track(128)
		var resolved: bool = NavDisplay.apsides_resolved(
			spread.call(), extent.call(track, centre), scale)
		var axis: Vector3 = nav._plane_from(track, centre, resolved)[0]
		if previous != Vector3.ZERO:
			worst = maxf(worst, rad_to_deg(previous.angle_to(axis)))
		previous = axis
	check(worst < 0.01, "o eixo do desenho não roda entre quadros (%.4f°)" % worst)

	# E o outro lado: uma órbita com apsis a sério tem de CONTINUAR a mostrá-lo.
	# Um limiar que apagasse o marcador em toda a parte passaria a metade de cima
	# deste teste sem fazer nada de útil.
	simulation.set_pointing_mode("prograde")
	for i in range(400):
		simulation.advance(0.5)
	simulation.set_throttle(1.0)
	for i in range(600):
		simulation.advance(0.5)
	simulation.set_throttle(0.0)
	simulation.set_pointing_mode("")   # sem comando: a atitude volta ao piloto

	centre = simulation.get_body_position(reference)
	track = simulation.get_orbit_track(128)
	check(float(simulation.get_snapshot()["eccentricity"]) > 0.5,
		"a queima deixou uma elipse franca (ecc %.3f)"
			% simulation.get_snapshot()["eccentricity"])
	check(NavDisplay.apsides_resolved(spread.call(), extent.call(track, centre), scale),
		"e aí o apsis volta a ser apontável")

	# ⚠️ E não salta TAMBÉM aqui. `get_orbit_track` amostra em anomalia verdadeira
	# de −π a +π, e com 128 amostras o periapsis (ν = 0) cai no índice 63,5 --
	# exatamente entre duas. Sem interpolação, qual delas ganha a comparação é
	# arredondamento, e a elipse inteira balançava 2,83 graus, que é um
	# espaçamento de amostra.
	worst = 0.0
	previous = Vector3.ZERO
	for frame in range(20):
		simulation.advance(0.016)
		centre = simulation.get_body_position(reference)
		track = simulation.get_orbit_track(128)
		var axis: Vector3 = nav._plane_from(track, centre, true)[0]
		if previous != Vector3.ZERO:
			worst = maxf(worst, rad_to_deg(previous.angle_to(axis)))
		previous = axis
	check(worst < 0.01, "nem numa elipse o eixo salta de amostra (%.4f°)" % worst)

	nav.queue_free()
	simulation.start_circular_orbit(400_000.0, 51.6)
	simulation.align_attitude_to_flight(25.0)
	simulation.advance(0.016)

func _test_planned_trajectory_frame(simulation: SpaceflightSimulation) -> void:
	## O arco planejado e os marcadores de queima têm de estar no MESMO
	## referencial das outras curvas do mapa: relativo ao corpo de origem,
	## ancorado onde ele está agora.
	##
	## Este teste existe por causa de um defeito medido. O Milestone 6.2 devolvia
	## o arco em coordenadas absolutas, somando a posição do corpo de origem na
	## época de CADA amostra -- a resposta certa para "onde a nave esteve no
	## Sistema Solar" e a errada para qualquer coisa desenhada em torno da Terra.
	## A Terra viaja a 30 km/s: nos 4,75 dias de uma transferência translunar ela
	## anda 12,7 milhões de km, e o mapa desenhava uma viagem à Lua como uma linha
	## até 13,2 milhões de km, contra uma Lua a 361 mil.
	##
	## Planejar leva cerca de um segundo. Vale-o: é o único caminho que exercita o
	## planejador real e o único lugar onde este defeito aparece.
	print("\nthe planned arc is in the map's frame")
	var plan := simulation.plan_transfer("Moon", 100.0, 100.0, 2.0)
	if plan.is_empty() or not plan.get("valid", false):
		check(false, "the planner found a transfer (%s)" % simulation.get_last_error())
		return
	check_equal(plan.get("burns", 0), 2, "an unarmed plan already reports its burn count")
	check(not plan.get("armed", false), "planning does not arm (rule 26)")
	check(simulation.arm_plan(), "and arm_plan installs it")
	check(simulation.get_plan().get("armed", false), "which the summary then reports")

	var scale: float = simulation.get_render_scale()
	var centre := simulation.get_body_position(
		_index_of(simulation, simulation.get_snapshot()["reference"]))
	var moon_distance: float = (simulation.get_body_position(_index_of(simulation, "Moon"))
		- centre).length() / scale

	var track := simulation.get_planned_trajectory()
	check(track.size() > 16, "the arc comes back sampled")
	var near := INF
	var far := 0.0
	for point in track:
		var r: float = (point - centre).length() / scale
		near = minf(near, r)
		far = maxf(far, r)
	# Começa na órbita de estacionamento e acaba à distância da Lua. O que o
	# defeito produzia era 153 734 km no início e 13 228 253 km no fim.
	check_near(near, 6.771e6, 1.0e5, "the arc starts in the parking orbit")
	check(far > moon_distance * 0.8 and far < moon_distance * 1.3,
		"and ends at lunar distance (%.0f km, Moon at %.0f km)"
			% [far / 1000.0, moon_distance / 1000.0])

	var burns := simulation.get_maneuvers()
	check_equal(burns.size(), 2, "two burns")
	var injection: Dictionary = burns[0]
	var insertion: Dictionary = burns[1]
	check(injection["located"] and insertion["located"], "both are placed on the arc")
	check_near((Vector3(injection["position"]) - centre).length() / scale, 6.771e6, 1.0e5,
		"the injection lights in the parking orbit")
	var insertion_r: float = (Vector3(insertion["position"]) - centre).length() / scale
	check(insertion_r > moon_distance * 0.8,
		"the capture lights at the Moon (%.0f km)" % (insertion_r / 1000.0))
	check(float(injection["seconds_to_ignition"]) > 0.0, "the injection is still ahead")

	simulation.clear_plan()
	check(not simulation.has_plan(), "and the plan can be abandoned (rule 68)")


func _test_rcs_actuator_mapping(simulation: SpaceflightSimulation) -> void:
	print("\nthe jets light what the ACTUATOR lit (rule 15)")
	var thrusters := simulation.get_rcs_thrusters()
	check_equal(thrusters.size(), 12, "twelve thrusters, as the core lays them out")
	for entry in thrusters:
		var spec: Dictionary = entry
		check_near(Vector3(spec["position"]).length(), 2.0, 1.0e-6,
			"%s sits on the 2 m arm the core uses" % spec["name"])
		check_near(Vector3(spec["force_direction"]).length(), 1.0, 1.0e-6,
			"%s pushes along a unit vector" % spec["name"])

	var visual := RcsVisual.new()
	root.add_child(visual)
	visual.build(thrusters)

	# Sem comando, nada acende. É o caso que uma implementação guiada pela tecla
	# acertaria por acidente, e está aqui para fixar o outro lado.
	simulation.set_manual_torque(Vector3.ZERO)
	simulation.set_manual_translation(Vector3.ZERO)
	simulation.set_pointing_mode("")
	simulation.advance(0.016)
	visual.set_throttles(simulation.get_rcs_throttles())
	check_equal(visual.firing_count(), 0, "no demand, no flame")

	# Um torque puro em +x: o alocador abre os dois bicos do binário de +x e
	# mais nenhum. Dois, não quatro e não doze.
	simulation.set_manual_torque(Vector3(400.0, 0.0, 0.0))
	simulation.advance(0.016)
	var pure := simulation.get_rcs_throttles()
	visual.set_throttles(pure)
	check_equal(visual.firing_count(), 2, "a pure +x couple opens exactly two thrusters")
	check(pure[0] > 0.0 and pure[1] > 0.0, "and they are the +x pair the core declares first")

	# Um torque diagonal abre mais bicos, a frações DIFERENTES. É esta linha que
	# um visual movido pela tecla não consegue reproduzir.
	simulation.set_manual_torque(Vector3(400.0, 400.0, 0.0))
	simulation.advance(0.016)
	var diagonal := simulation.get_rcs_throttles()
	visual.set_throttles(diagonal)
	check(visual.firing_count() > 2, "a diagonal command opens more than a pure one")
	var distinct := {}
	for value in diagonal:
		if value > 0.002:
			distinct[snappedf(value, 0.001)] = true
	check(visual.total_demand() > 0.0, "the diagonal command produces demand")

	# Translação: força sem torque. O layout em binários permite-o, e é o que os
	# controles de translação da regra 13 comandam.
	simulation.set_manual_torque(Vector3.ZERO)
	simulation.set_manual_translation(Vector3(0.0, 0.0, 360.0))
	simulation.advance(0.016)
	visual.set_throttles(simulation.get_rcs_throttles())
	check(visual.firing_count() >= 2, "a translation command opens thrusters too")
	simulation.set_manual_translation(Vector3.ZERO)
	visual.queue_free()


func _test_engine_plume() -> void:
	print("\nthe plume follows the THRUST, not the key (rule 16)")
	var plume := EnginePlume.new()
	root.add_child(plume)
	plume._ready()
	plume.set_thrust(0.0)
	check(not plume.visible, "no thrust, no plume")
	plume.set_thrust(200_000.0)
	check(plume.visible, "full thrust lights it")
	var full: float = plume._intensity
	plume.set_thrust(50_000.0)
	check(plume._intensity < full, "a quarter of the thrust is a smaller plume")
	# A GEOMETRIA, e não só o `visible`. A escala de um `Node3D` aplica-se ANTES
	# da rotação: a versão que esticava `scale.x` de uma malha que cresce em +y
	# desenhava um disco de 26 m de LARGURA por 2,6 m de comprimento, e a
	# fotografia da demonstração saiu sem pluma nenhuma com o motor a 200 kN.
	# Quem decide é a AABB transformada, que é o que o renderizador desenha.
	plume.set_thrust(200_000.0)
	var box: AABB = plume._cone.transform * plume._cone.get_aabb()
	check_near(-box.position.x, EnginePlume.MAX_LENGTH, 0.6,
		"a pluma cheia estende-se 26 m para TRÁS do bocal")
	check(box.end.x <= 0.3, "e começa no bocal, não à frente dele")
	check(maxf(box.size.y, box.size.z) < box.size.x * 0.5,
		"não é um disco: é mais comprida do que larga")
	# O caso que separa o empuxo da tecla: acelerador cheio, tanque vazio.
	plume.set_thrust(0.0)
	check(not plume.visible, "an empty tank puts it out even at full throttle")
	plume.queue_free()


func _test_camera_modes() -> void:
	print("\ncamera modes (rule 11)")
	var rig := CameraRig.new()
	root.add_child(rig)
	var viewport := SubViewport.new()
	root.add_child(viewport)
	rig.build(1.0e-6, viewport)

	check(rig.is_cockpit(), "a new flight starts in the cockpit")
	var seen := {}
	for i in range(CameraRig.MODE_NAMES.size()):
		seen[rig.mode_name()] = true
		rig.cycle_mode()
	check_equal(seen.size(), CameraRig.MODE_NAMES.size(), "cycling reaches every mode")
	check(rig.is_cockpit(), "and comes back round to the cockpit")

	rig.set_mode(CameraRig.Mode.EXTERNAL_ORBIT)
	check(rig.at_preset(), "entering a mode puts the camera at its preset")
	rig.orbit_azimuth += 0.6
	check(not rig.at_preset(), "and the readout stops claiming the preset once you leave it")
	rig.recentre()
	check(rig.at_preset(), "recentre brings it back")

	# Foco num corpo: a câmera tem de ficar FORA dele.
	#
	# A primeira versão construía o referencial com `Vector3.FORWARD` e o polo
	# J2000 como "cima", e o produto vetorial dos dois é o vetor nulo -- a câmera
	# ficava no centro do planeta e a tela ficava preta, sem erro nenhum. Uma
	# distância de zero é a assinatura disso.
	rig.focus_index = 0
	rig.update(Vector3(6.771, 0.0, 0.0), Basis.IDENTITY, Vector3(0.0, 1.0e-4, 0.0),
		Vector3.ZERO, Vector3.ZERO, 19.1)
	var distance := rig.world_camera.position.length()
	check(distance > 1.0, "focusing a body puts the camera outside it (%.2f units)" % distance)
	check(rig.world_camera.basis.z.length() > 0.5, "and gives it an orientation")
	rig.focus_index = -1

	var before := rig.orbit_zoom
	rig.zoom(1.0)
	check(rig.orbit_zoom > before, "the wheel zooms out")
	for i in range(80):
		rig.zoom(1.0)
	check(rig.orbit_zoom <= CameraRig.ZOOM_MAX, "zoom is bounded")
	rig.queue_free()
	viewport.queue_free()


func _test_cockpit_hit_test() -> void:
	print("\nclickable cockpit controls (rules 23, 24)")
	var pressed := [0]
	var button := CockpitControl.button("TEST", Vector3(0.0, 0.0, 0.0), 0.24,
		func(_c: CockpitControl) -> void: pressed[0] += 1)
	root.add_child(button)
	button._ready()

	# Um raio vindo de -z, de frente para a face: acerta.
	check(button.hit_test(Vector3(0.0, 0.0, -1.0), Vector3(0.0, 0.0, 1.0)),
		"a ray through the middle hits")
	# Fora da borda: não acerta. 0.13 > metade da largura 0.12.
	check(not button.hit_test(Vector3(0.13, 0.0, -1.0), Vector3(0.0, 0.0, 1.0)),
		"a ray past the edge misses")
	# Atrás do painel, apontando para longe: não acerta. Sem esta verificação,
	# clicar no céu à frente acenderia um botão que está nas costas do piloto.
	check(not button.hit_test(Vector3(0.0, 0.0, 1.0), Vector3(0.0, 0.0, 1.0)),
		"a control behind the pointer is not hit")
	# Paralelo ao painel: não acerta, e não divide por zero.
	check(not button.hit_test(Vector3(0.0, 0.0, -1.0), Vector3(1.0, 0.0, 0.0)),
		"a ray parallel to the panel misses")

	button.press()
	check_equal(pressed[0], 1, "pressing calls back once")
	var switch := CockpitControl.switch("RCS", Vector3.ZERO, 0.24, false,
		func(_c: CockpitControl) -> void: pass)
	root.add_child(switch)
	switch._ready()
	switch.press()
	check(switch.on, "a switch toggles before it calls back")
	switch.press()
	check(not switch.on, "and toggles back")
	button.queue_free()
	switch.queue_free()


func _test_instrument_data_survives_a_display(simulation: SpaceflightSimulation) -> void:
	print("\ninstruments draw without a display (rule 62)")
	# Um instrumento sem dado nenhum não pode rebentar: é o estado em que ele
	# está no primeiro quadro, antes de o primeiro snapshot chegar.
	for instrument in [FlightDisplay.new(), NavDisplay.new(), TargetDisplay.new(),
			SystemDisplay.new(), MinimalHud.new(), OrbitMap.new()]:
		root.add_child(instrument)
		instrument.size = Vector2(480, 320)
		instrument.refresh({})
		instrument.refresh(simulation.get_snapshot())
		check(true, "%s accepts an empty and a full snapshot" % instrument.get_class())
		instrument.queue_free()


func _index_of(simulation: SpaceflightSimulation, name: String) -> int:
	for i in range(simulation.get_body_count()):
		if simulation.get_body_name(i) == name:
			return i
	return -1
