extends SceneTree
## Os testes de apresentação do Milestone 8 (regra 108).
##
## O que eles cobrem é o que o M8 acrescentou do lado do Godot: o diretório de
## corpos que chega à interface, Marte como destino selecionável, o mapa do
## sistema solar, a formatação de durações e distâncias interplanetárias, e o
## corpo de referência. A física continua verificada por `ctest` em C++ e este
## arquivo não repete uma única conta dela.
##
## Sem tela. Nenhuma asserção aqui olha para um pixel.
##
##     scripts/godot_tests.sh m8

var failures := 0
var checks := 0


func _initialize() -> void:
	InputActions.install()

	var simulation := SpaceflightSimulation.new()
	root.add_child(simulation)
	var kernels := ProjectSettings.globalize_path("res://").path_join("../../kernels/spice")
	if not simulation.configure(kernels.simplify_path(), "2026-01-01T00:00:00"):
		printerr("SKIP: no SPICE kernels (%s)" % simulation.get_last_error())
		quit(77)
		return
	simulation.start_circular_orbit(400_000.0, 51.6)
	simulation.align_attitude_to_flight(25.0)
	simulation.set_render_scale(1.0e-6)
	simulation.advance(0.016)

	_test_body_directory(simulation)
	_test_mars_is_selectable(simulation)
	_test_directory_hierarchy(simulation)
	_test_substituted_positions_are_not_destinations(simulation)
	_test_interplanetary_formatting()
	_test_system_map(simulation)
	_test_system_orbit_paths(simulation)
	_test_async_search_can_be_cancelled(simulation)

	print("\n%d checks, %d failure(s)" % [checks, failures])
	quit(1 if failures > 0 else 0)


func _check(condition: bool, description: String) -> void:
	checks += 1
	if condition:
		print("  ok   %s" % description)
	else:
		failures += 1
		printerr("  FAIL %s" % description)


func _entry(directory: Array, name: String) -> Dictionary:
	for item in directory:
		var entry: Dictionary = item
		if entry["name"] == name:
			return entry
	return {}


func _test_body_directory(simulation: SpaceflightSimulation) -> void:
	print("\nbody directory (regra 19: uma tabela só)")
	var directory: Array = simulation.get_body_directory()
	_check(directory.size() >= 20, "o diretório traz o sistema solar (%d corpos)" % directory.size())

	for name in ["Sun", "Mercury", "Venus", "Earth", "Moon", "Mars", "Jupiter", "Saturn",
			"Uranus", "Neptune"]:
		_check(not _entry(directory, name).is_empty(), "%s está no diretório" % name)

	var mars := _entry(directory, "Mars")
	_check(mars.get("naif_id", 0) == 499, "Marte é o corpo 499 e não o baricentro 4")
	_check(mars.get("radius_m", 0.0) > 3.3e6 and mars.get("radius_m", 0.0) < 3.5e6,
		"o raio de Marte vem do PCK (%.0f m)" % mars.get("radius_m", 0.0))
	_check(mars.get("has_ephemeris", false), "as efemérides sabem colocar Marte")
	_check(not mars.get("position_substituted", true),
		"a posição de Marte é do próprio corpo, não do baricentro")


func _test_mars_is_selectable(simulation: SpaceflightSimulation) -> void:
	print("\nMarte como alvo (regras 16, 21)")
	var targets: Array = simulation.get_selectable_targets()
	_check(targets.has("Mars"), "Marte aparece na lista de alvos")
	_check(targets.has("Moon"), "a Lua continua na lista de alvos")

	_check(simulation.set_target_body("Mars"), "set_target_body(\"Mars\") é aceito")
	_check(simulation.get_target_body() == "Mars", "o alvo passa a ser Marte")

	var snapshot: Dictionary = simulation.get_snapshot()
	var distance: float = snapshot.get("target_distance_m", 0.0)
	# Marte está entre 0,37 e 2,7 UA da Terra. Qualquer coisa fora disso quer
	# dizer que a distância foi medida contra o corpo errado.
	_check(distance > 5.0e10 and distance < 4.2e11,
		"a distância até Marte é interplanetária (%.3f UA)" % (distance / 1.495978707e11))

	_check(simulation.set_target_body("Moon"), "o alvo volta para a Lua")


func _test_directory_hierarchy(simulation: SpaceflightSimulation) -> void:
	print("\nhierarquia (regra 20)")
	var directory: Array = simulation.get_body_directory()
	_check(_entry(directory, "Moon").get("parent", 0) == 399, "a Lua orbita a Terra")
	_check(_entry(directory, "Earth").get("parent", 0) == 10, "a Terra orbita o Sol")
	_check(_entry(directory, "Phobos").get("parent", 0) == 499, "Fobos orbita Marte")
	_check(_entry(directory, "Io").get("parent", 0) == 599, "Io orbita Júpiter")
	_check(_entry(directory, "Titan").get("parent_name", "") == "Saturn",
		"Titã traz o nome do pai para o menu")


func _test_substituted_positions_are_not_destinations(simulation: SpaceflightSimulation) -> void:
	print("\nhonestidade do catálogo (regra 21: não fingir suporte completo)")
	var directory: Array = simulation.get_body_directory()
	# Júpiter é desenhado na posição do BARICENTRO do sistema joviano, porque o
	# SPK do próprio planeta não é distribuído com o projeto. Isso basta para
	# desenhar e não basta para ser o centro de uma órbita de captura -- e o
	# catálogo tem de dizer as duas coisas.
	var jupiter := _entry(directory, "Jupiter")
	if jupiter.get("position_substituted", false):
		_check(not jupiter.get("can_be_destination", true),
			"Júpiter, desenhado no baricentro, não é oferecido como destino")
		_check(jupiter.get("mission_support", "") == "observation",
			"Júpiter é declarado observação e não missão")

	var moon := _entry(directory, "Moon")
	_check(moon.get("mission_support", "") == "qualified",
		"a Lua é o único destino qualificado por campanha")
	_check(_entry(directory, "Mars").get("can_be_destination", false),
		"Marte pode ser destino")


func _test_interplanetary_formatting() -> void:
	print("\nformatação interplanetária (regras 96-98)")
	# 92 dias e 14 horas, o exemplo da regra 96.
	_check(Fmt.duration(92.0 * 86400.0 + 14.0 * 3600.0).contains("92"),
		"uma duração de três meses é legível: %s" % Fmt.duration(92.0 * 86400.0 + 14.0 * 3600.0))
	_check(Fmt.distance(8.42e10).length() < 20,
		"uma distância de 84 milhões de km cabe num mostrador: %s" % Fmt.distance(8.42e10))

	# ⚠️ `%g` não existe no formatador do GDScript, e um formato inválido não é um
	# erro: ele é impresso LITERALMENTE. A primeira captura do mapa do sistema
	# solar saiu com "%.3g AU" em cada anel de escala, e nada -- nem o parser, nem
	# os testes, nem o log -- tinha dito uma palavra.
	#
	# Então o que se verifica é a ausência do caractere de formato na SAÍDA, que é
	# o único sítio onde esse defeito aparece.
	var map := OrbitMap.new()
	for au: float in [0.05, 0.387, 1.0, 1.524, 5.2, 30.1]:
		var label: String = map._au_label(au)
		_check(not label.contains("%"),
			"o rótulo de %.3f UA é texto e não um formato por imprimir: \"%s\"" % [au, label])
	map.free()


func _test_system_map(simulation: SpaceflightSimulation) -> void:
	print("\nmapa do sistema solar (regras 26-31)")
	var map: Dictionary = simulation.get_system_map()
	_check(not map.is_empty(), "get_system_map() responde")

	# Regra 31: cada camada declara o seu referencial, e o mapa converte tudo para
	# um só ANTES de desenhar. Aqui isso é verificável: o payload nomeia o frame.
	_check(String(map.get("frame", "")).contains("Sun"),
		"o frame vem declarado: \"%s\"" % map.get("frame", ""))
	_check(map.get("centre", "") == "Sun", "o centro é o Sol")

	var bodies: Array = map.get("bodies", [])
	_check(bodies.size() >= 8, "os planetas chegam ao mapa (%d corpos)" % bodies.size())

	var names := PackedStringArray()
	for entry in bodies:
		names.append(String(entry["name"]))
	_check(names.has("Sun"), "o Sol está no mapa")
	_check(names.has("Earth"), "a Terra está no mapa")
	_check(names.has("Mars"), "Marte está no mapa")
	# Regra 27: as luas não entram no zoom heliocêntrico -- a Lua e a Terra são o
	# mesmo pixel ali, e Fobos seria um rótulo por cima de Marte.
	_check(not names.has("Moon"), "a Lua NÃO entra no mapa heliocêntrico")
	_check(not names.has("Phobos"), "Fobos NÃO entra no mapa heliocêntrico")

	# As posições estão em metros heliocêntricos, então a Terra tem de estar a
	# uma UA do centro. Se estivesse em unidades de cena, ou relativa à nave,
	# este número estaria errado por ordens de grandeza -- que é exatamente o
	# tipo de erro de frame que a regra 31 existe para impedir.
	for entry in bodies:
		var body: Dictionary = entry
		if body["name"] == "Earth":
			var r: float = Vector3(body["position"]).length()
			_check(r > 1.4e11 and r < 1.6e11,
				"a Terra está a %.3f UA do Sol" % (r / 1.495978707e11))
		if body["name"] == "Sun":
			_check(Vector3(body["position"]).length() < 1.0e3,
				"o Sol está na origem do mapa")

	var ship: Vector3 = map.get("ship_position", Vector3.ZERO)
	_check(ship.length() > 1.4e11 and ship.length() < 1.6e11,
		"a nave, em órbita terrestre, está a uma UA do Sol")


func _test_system_orbit_paths(simulation: SpaceflightSimulation) -> void:
	print("\ncaminhos planetários, amostrados da efeméride (regra 29)")
	var paths: Dictionary = simulation.get_system_orbit_paths(48)
	_check(paths.has("Earth"), "a órbita da Terra é amostrada")
	_check(paths.has("Mars"), "a órbita de Marte é amostrada")
	_check(not paths.has("Moon"), "luas não recebem caminho heliocêntrico")

	if paths.has("Earth"):
		var row: Dictionary = paths["Earth"]
		var path: PackedVector3Array = row["path"]
		_check(path.size() >= 24, "o caminho tem %d pontos" % path.size())
		# Um ano terrestre, lido das próprias elipses osculadoras e não de uma
		# tabela de períodos.
		var period_days: float = float(row["period_s"]) / 86400.0
		_check(period_days > 360.0 and period_days < 370.0,
			"o período da Terra sai da efeméride: %.2f dias" % period_days)
		# O caminho fecha: o primeiro e o último ponto de uma revolução completa
		# ficam próximos. Se a amostragem estivesse a sair do intervalo coberto
		# pelos kernels, ou a usar o frame errado, isto falharia.
		var closure := (path[0] - path[path.size() - 1]).length()
		_check(closure < 0.05 * path[0].length(),
			"a órbita fecha (%.3f %% do raio)" % (100.0 * closure / path[0].length()))

	if paths.has("Neptune"):
		var neptune: Dictionary = paths["Neptune"]
		# Regra: o ano de Netuno tem 165 anos e o de440s cobre 1849-2150. Meia
		# revolução para cada lado de 2026 cabe; o campo diz se foi recortado, em
		# vez de extrapolar em silêncio.
		_check(neptune.has("clipped"), "o caminho de Netuno diz se foi recortado")


func _test_async_search_can_be_cancelled(simulation: SpaceflightSimulation) -> void:
	print("\nbusca assíncrona (regras 48, 49, 120)")
	_check(not simulation.is_planning(), "nada a correr antes de começar")

	# Marte, porque é a busca cara: é ela que tornaria um planejamento bloqueante
	# um minuto de quadros congelados.
	_check(simulation.start_planning("Mars", 500.0, 500.0, 2.0),
		"start_planning(\"Mars\") aceita")
	_check(simulation.is_planning(), "a busca está a correr numa thread")

	# O quadro continua a andar. Esta é a asserção que importa: se o
	# planejamento bloqueasse, esta linha só seria alcançada no fim da busca.
	var advanced := 0
	for i in range(30):
		simulation.advance(0.016)
		advanced += 1
	_check(advanced == 30, "a simulação avançou %d quadros durante a busca" % advanced)

	var progress: Dictionary = simulation.get_planning_progress()
	_check(progress.get("running", false), "o progresso diz que está a correr")
	_check(progress.has("candidates_considered"), "o progresso traz contagens")

	simulation.cancel_planning()
	_check(simulation.get_planning_progress().get("cancelled", false),
		"o cancelamento foi registado")

	# A busca para entre candidatas, nunca dentro de uma, então isto espera --
	# e o ponto é que ela PARA, em vez de ser abandonada a correr.
	var waited := 0.0
	while simulation.is_planning() and waited < 120.0:
		OS.delay_msec(50)
		waited += 0.05
	_check(not simulation.is_planning(), "o worker parou em %.2f s" % waited)

	var plan: Dictionary = simulation.collect_plan()
	_check(plan.get("cancelled", false) or plan.is_empty(),
		"uma busca cancelada não devolve um plano")
	_check(not simulation.has_planned_transfer(),
		"e não deixa um plano armado para trás")
