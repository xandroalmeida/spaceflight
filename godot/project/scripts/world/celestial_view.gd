class_name CelestialView
extends Node3D
## Os corpos celestes, desenhados onde o core diz que eles APARECEM.
##
## Herdado do Milestone 5 sem mudar uma conta: a posição vem de
## `get_body_observed_position()` (tempo de luz contra a efeméride real, depois
## aberração), o fator Doppler vem de `core/relativity/optics.hpp`, e o shader
## aplica o que `D` significa sem nunca saber o que é `β`.
##
## O que o Milestone 7 acrescentou é superfície: textura, nuvens, lado noturno,
## limbo -- e a rotação do corpo, que vem de `get_body_orientation()`, isto é, do
## `pxform_c` sobre os mesmos kernels. Nada disso realimenta nada. Uma Terra
## texturizada e uma Terra azul lisa produzem o mesmo estado, quadro a quadro.

const SUN_TEMPERATURE := 5772.0

var simulation: SpaceflightSimulation
var meshes: Array[MeshInstance3D] = []
var materials: Array[ShaderMaterial] = []
var sun_light: DirectionalLight3D
var sun_index := -1
var earth_index := -1

## Ligado e desligado pelo piloto (teclas de ótica), preservado do M5.
var effect_retarded := true
var effect_aberration := true
var effect_doppler := true
var effect_beaming := true
var exposure := 0.1584893

var _planck: ImageTexture
var _cloud_offset := 0.0


func build(sim: SpaceflightSimulation, planck_texture: ImageTexture) -> void:
	simulation = sim
	_planck = planck_texture

	# Uma luz direcional para o Sol, e não uma point light em escala
	# astronômica (regra 33): a 1.47e5 unidades de cena, o alcance e a queda de
	# uma omni tornam-se números que o float não guarda. A direção é atualizada
	# todo quadro a partir de onde o Sol de facto está.
	sun_light = DirectionalLight3D.new()
	sun_light.light_energy = 1.35
	sun_light.light_color = Color(1.0, 0.97, 0.92)
	# Sem sombras: a única coisa que projetaria uma é um planeta sobre outro, e
	# o mapa de sombras direcional do Godot não cobre 1e11 metros. Um eclipse é
	# geometria que o core sabe fazer e o renderizador não -- registrado no
	# backlog em vez de aproximado aqui.
	sun_light.shadow_enabled = false
	add_child(sun_light)

	for i in range(simulation.get_body_count()):
		var name := simulation.get_body_name(i)
		var mesh_instance := MeshInstance3D.new()
		var sphere := SphereMesh.new()
		sphere.radial_segments = 64
		sphere.rings = 32
		# `SphereMesh` nasce com raio 0.5 e altura 1.0, então escalar o nó pelo
		# raio desenharia um corpo de METADE do tamanho certo. De uma órbita de
		# 400 km isso é a diferença entre um planeta que enche a tela e um que
		# fica inteiramente fora dela -- que foi a tela preta que esta cena
		# mostrou da primeira vez que rodou.
		sphere.radius = 1.0
		sphere.height = 2.0
		mesh_instance.mesh = sphere

		var material := ShaderMaterial.new()
		material.shader = load("res://shaders/relativistic_body.gdshader")
		material.set_shader_parameter("reflectance", _colour_for(name))
		material.set_shader_parameter("is_self_luminous", name == "Sun")
		if _planck != null:
			material.set_shader_parameter("planck_table", _planck)
		material.set_shader_parameter("light_speed_scene", simulation.get_light_speed_scene())
		_apply_surface(name, material)
		mesh_instance.material_override = material

		add_child(mesh_instance)
		meshes.append(mesh_instance)
		materials.append(material)

		if name == "Sun":
			sun_index = i
		elif name == "Earth":
			earth_index = i


func set_planck_reference(temperature: float) -> void:
	for material in materials:
		material.set_shader_parameter("table_reference_temperature", temperature)


func _apply_surface(name: String, material: ShaderMaterial) -> void:
	## Substitutos procedurais enquanto as texturas definitivas não existem
	## (regra 81). Quando o arquivo chegar em `assets/textures/...` ele é carregado
	## no lugar e nada mais muda.
	match name:
		"Earth":
			material.set_shader_parameter("albedo_map",
				_texture_or("res://assets/textures/earth/earth_albedo.png",
					func() -> Texture2D: return PlanetTextures.earth_albedo()))
			material.set_shader_parameter("use_albedo_map", true)
			material.set_shader_parameter("cloud_map",
				_texture_or("res://assets/textures/earth/earth_clouds.png",
					func() -> Texture2D: return PlanetTextures.earth_clouds()))
			material.set_shader_parameter("use_cloud_map", true)
			material.set_shader_parameter("night_map",
				_texture_or("res://assets/textures/earth/earth_night.png",
					func() -> Texture2D: return PlanetTextures.earth_night()))
			material.set_shader_parameter("use_night_map", true)
			# O limbo: espalhamento de Rayleigh é azul porque o céu é azul, e a
			# intensidade é um número de desenho. Não há física atmosférica aqui
			# e a regra 31 diz que não deve haver.
			material.set_shader_parameter("atmosphere_strength", 0.55)
			material.set_shader_parameter("atmosphere_colour", Color(0.30, 0.52, 0.95))
		"Moon":
			material.set_shader_parameter("albedo_map",
				_texture_or("res://assets/textures/moon/moon_albedo.png",
					func() -> Texture2D: return PlanetTextures.moon_albedo()))
			material.set_shader_parameter("use_albedo_map", true)
			# O relevo, quando existir. Ele é DERIVADO da topografia medida pelo
			# LOLA (`scripts/make_moon_normal.py`) e não pintado: a primeira
			# imagem entregue parecia um normal map e tinha os canais R e G a
			# seguir o albedo em vez da inclinação.
			var relief := _texture("res://assets/textures/moon/moon_normal.png")
			if relief != null:
				material.set_shader_parameter("normal_map", relief)
				material.set_shader_parameter("use_normal_map", true)
		"Mars":
			material.set_shader_parameter("albedo_map",
				_texture_or("res://assets/textures/mars/mars_albedo.png",
					func() -> Texture2D: return PlanetTextures.mars_albedo()))
			material.set_shader_parameter("use_albedo_map", true)
			# A atmosfera de Marte tem 0,6 % da pressão da terrestre e o limbo que
			# ela produz é um fio ocre, não um halo azul. 0,12 e não 0,55: é
			# visível na silhueta e some no disco, que é o que a imagem mostra.
			#
			# ⚠️ Isto é DESENHO e não física. Não há modelo atmosférico neste
			# projeto e a regra 56 diz que não há aerocaptura: a captura em Marte
			# é exclusivamente propulsiva, e este parâmetro não toca em nada
			# além do shader.
			material.set_shader_parameter("atmosphere_strength", 0.12)
			material.set_shader_parameter("atmosphere_colour", Color(0.85, 0.62, 0.45))


func _texture(path: String) -> Texture2D:
	if not ResourceLoader.exists(path):
		return null
	var loaded := load(path)
	return loaded if loaded is Texture2D else null


func _texture_or(path: String, fallback: Callable) -> Texture2D:
	var loaded := _texture(path)
	return loaded if loaded != null else fallback.call()


func update(camera_position: Vector3) -> void:
	if simulation == null:
		return

	var sun_direction := Vector3(1.0, 0.0, 0.0)
	if sun_index >= 0:
		var to_sun: Vector3 = meshes[sun_index].position - camera_position
		if to_sun.length() > 0.0:
			sun_direction = to_sun.normalized()
			# A luz vem DO Sol, então a direcional aponta para o lado oposto.
			# O lado contrário ao Sol fica escuro por consequência e não por
			# ajuste (regra 29).
			sun_light.look_at_from_position(Vector3.ZERO, -sun_direction,
				Vector3.UP if absf(sun_direction.z) < 0.98 else Vector3.RIGHT)

	for i in range(meshes.size()):
		var mesh_instance := meshes[i]
		mesh_instance.position = simulation.get_body_observed_position(
			i, effect_retarded, effect_aberration)
		var radius: float = simulation.get_body_radius(i)
		mesh_instance.visible = radius > 0.0
		if radius <= 0.0:
			continue
		# A orientação vem dos kernels; a escala vem do raio. As duas juntas são
		# a `Basis`, e a ordem importa: rodar depois de escalar daria um
		# elipsoide girado quando a exageração de escala estiver ligada.
		mesh_instance.basis = _mesh_basis(simulation.get_body_orientation(i)) \
			.scaled(Vector3.ONE * radius)

		var material := materials[i]
		var physical_doppler: float = simulation.get_body_doppler(i)
		material.set_shader_parameter("doppler", physical_doppler if effect_doppler else 1.0)
		material.set_shader_parameter("beaming_doppler",
			physical_doppler if effect_beaming else 1.0)
		material.set_shader_parameter("relative_velocity_scene",
			simulation.get_body_relative_velocity_scene(i))
		material.set_shader_parameter("light_speed_scene", simulation.get_light_speed_scene())
		material.set_shader_parameter("apply_light_time", effect_retarded)
		material.set_shader_parameter("half_saturation", exposure)
		material.set_shader_parameter("observer_position_scene", camera_position)
		material.set_shader_parameter("sun_direction_scene", sun_direction)
		if i == earth_index:
			material.set_shader_parameter("cloud_offset", _cloud_offset)


## Dos eixos do corpo para os eixos da MALHA.
##
## `get_body_orientation()` dá uma base cujas colunas são os eixos fixos ao
## corpo: `+x` o meridiano zero, `+z` o polo norte. `SphereMesh` do Godot não usa
## essa convenção: o polo dela é `+y` local, e a coordenada `u` da textura anda
## de `+z` local em `u = 0`, no sentido em que `+x` local cai em `u = 0,25`.
##
## Para uma textura equirretangular com longitude −180 na borda esquerda:
##
##     `u = 0`    ↔ longitude −180 ↔ corpo −x     ⇒ malha +z = −(corpo x)
##     `u = 0,25` ↔ longitude  −90 ↔ corpo −y     ⇒ malha +x = −(corpo y)
##     `v = 0`    ↔ polo norte     ↔ corpo +z     ⇒ malha +y = +(corpo z)
##
## O determinante continua +1 (duas trocas de sinal), então é uma rotação e não
## uma reflexão -- o que importa, porque uma reflexão espelharia os continentes
## e a imagem continuaria parecendo plausível.
##
## Sem isto, o polo da textura ia parar ao eixo `+y` do corpo: as calotas polares
## no equador. Com uma esfera de cor lisa isso era invisível; é a textura que o
## torna verificável, e `tests/probe_orientation.gd` verifica-o.
static func _mesh_basis(orientation: Basis) -> Basis:
	return Basis(-orientation.y, orientation.z, -orientation.x)


func advance_clouds(delta: float) -> void:
	## Uma volta a cada quarenta minutos de tempo de parede. É DESENHO: não há
	## circulação atmosférica neste projeto, e a alternativa -- nuvens
	## absolutamente paradas sobre um planeta que gira -- lê como um erro de
	## renderização, que é o que seria.
	_cloud_offset = fmod(_cloud_offset + delta / 2400.0, 1.0)


func warn_if_camera_is_inside_a_body(camera_position: Vector3, scale_label: String) -> void:
	for i in range(meshes.size()):
		var radius: float = simulation.get_body_radius(i)
		if radius <= 0.0:
			continue
		var distance: float = (meshes[i].position - camera_position).length()
		if radius > distance:
			push_warning("camera is inside %s: drawn radius %.2f > distance %.2f units (%s)"
				% [simulation.get_body_name(i), radius, distance, scale_label])


func index_of(name: String) -> int:
	for i in range(meshes.size()):
		if simulation.get_body_name(i) == name:
			return i
	return -1


static func _colour_for(name: String) -> Color:
	## A cor de base, usada onde não há textura. Com textura ela ainda serve ao
	## `reflectance` do shader nos corpos sem mapa.
	##
	## Regra 69: os gigantes e os planetas interiores usam material simples com
	## cores plausíveis. Elas não são inventadas -- são a cor média do disco em
	## imagens de sonda -- mas também não são medidas fotometricamente, e nenhuma
	## delas bloqueia o Milestone 8.
	match name:
		"Sun": return Color(1.0, 0.92, 0.6)
		"Mercury": return Color(0.55, 0.52, 0.49)
		"Venus": return Color(0.90, 0.80, 0.55)
		"Earth": return Color(0.25, 0.45, 0.85)
		"Moon": return Color(0.72, 0.72, 0.70)
		"Mars": return Color(0.72, 0.44, 0.28)
		"Phobos", "Deimos": return Color(0.42, 0.38, 0.35)
		"Jupiter": return Color(0.85, 0.72, 0.55)
		"Saturn": return Color(0.89, 0.81, 0.62)
		"Uranus": return Color(0.62, 0.84, 0.86)
		"Neptune": return Color(0.30, 0.44, 0.78)
		"Pluto": return Color(0.68, 0.60, 0.52)
		_: return Color(0.6, 0.6, 0.65)
