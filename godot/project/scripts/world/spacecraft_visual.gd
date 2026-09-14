class_name SpacecraftVisual
extends Node3D
## A nave, em metros, no referencial do corpo (regras 5, 6, 7).
##
## Isto é APENAS aparência. Nada aqui é lido por nada: a posição, a atitude, a
## massa e o empuxo continuam vindo do `core/`, e este nó recebe uma base e a
## aplica. Se este arquivo for apagado a simulação continua idêntica -- que é o
## teste de que ele não é fonte da verdade (regra 7).
##
## Referencial: `+x` é o nariz, e é o mesmo `+x` ao longo do qual o motor
## principal empurra (`MainEngineForce`). `+z` é "cima" da nave. Os doze
## thrusters de RCS estão em (±2, 0, 0), (0, ±2, 0), (0, 0, ±2) porque é ali que
## `RcsSystem::couples(2.0, ...)` os põe -- os nacelles são desenhados NAS
## posições que a física usa, e não onde ficariam bonitos.
##
## ## Escala, e a dívida que ela deixa registrada
##
## O tensor de inércia do core é uma caixa sólida de 1000 kg e 8 × 3 × 3 m. O
## casco pressurizado desenhado aqui tem 9 m por 3 m de diâmetro, que é essa
## caixa; tanques, radiadores e motor ficam FORA dela, para trás. A inércia
## modela a distribuição de massa, não a extensão do veículo, e "distribuição de
## massa perfeita" está explicitamente fora do M7 (regra 57). A discrepância
## está registrada como PHYSICS_DEBT no relatório em vez de ser escondida
## aumentando a caixa ou encolhendo o desenho.
##
## Dimensões em docs/assets/spacecraft/dimensions.md.

const NOSE_TIP := 5.0            ## [m] ponta do cockpit, em +x
const CORE_FORWARD := 3.6        ## anteparo dianteiro do módulo de comando
const CORE_AFT := -4.0           ## anteparo traseiro do habitat
const CORE_RADIUS := 1.5
const TANK_RADIUS := 1.7
const TANK_FORWARD := -4.5
const TANK_AFT := -10.5
const TANK_OFFSET := 2.6         ## afastamento lateral do eixo dos tanques
const POWER_FORWARD := -10.0
const POWER_AFT := -12.5
const ENGINE_THROAT := -13.0
const ENGINE_EXIT := -17.0
const BELL_EXIT_RADIUS := 2.0
const RADIATOR_SPAN := 7.0       ## meia-largura, ponta a ponta 14 m
const RCS_ARM := 2.0             ## tem de bater com RcsSystem::couples

var materials: ShipMaterials
var engine_mount: Node3D         ## onde a pluma se pendura
var rcs_mounts: Dictionary = {}  ## Vector3 (posição no corpo) -> Node3D

var _navigation_lights: Array[MeshInstance3D] = []
var _light_phase := 0.0


func _init(shared_materials: ShipMaterials = null) -> void:
	materials = shared_materials if shared_materials != null else ShipMaterials.new()


func _ready() -> void:
	_build_command_module()
	_build_habitat()
	_build_truss()
	_build_tanks()
	_build_power_section()
	_build_engine()
	_build_radiators()
	_build_antennas()
	_build_rcs_pods()
	_build_navigation_lights()


func _process(delta: float) -> void:
	# A luz de navegação pisca a 1 Hz. É o único movimento da nave que não vem do
	# core, e existe porque uma estrutura absolutamente estática no escuro lê como
	# uma imagem congelada -- inclusive quando a simulação travou, que é
	# exatamente o que ela não pode esconder.
	_light_phase = fmod(_light_phase + delta, 2.0)
	var on := _light_phase < 0.12 or (_light_phase > 0.24 and _light_phase < 0.36)
	for light in _navigation_lights:
		light.visible = on


# --- módulos -----------------------------------------------------------------

func _build_command_module() -> void:
	# Tronco de cone do anteparo dianteiro até a base do cockpit: o módulo de
	# comando afina para frente, que é o que dá à nave um "nariz" sem lhe dar
	# aerodinâmica nenhuma (regra 5).
	var cone := _cylinder(CORE_RADIUS, 1.25, CORE_FORWARD - 1.4, CORE_FORWARD + 0.0, false)
	cone.material_override = materials.hull_paint
	add_child(cone)

	var canopy_base := _cylinder(1.25, 1.05, CORE_FORWARD, NOSE_TIP - 1.0, false)
	canopy_base.material_override = materials.dark_composite
	add_child(canopy_base)

	# A carenagem do cockpit. Desenhada por fora: por dentro dela é onde
	# `CockpitInterior` constrói o posto de pilotagem, e as duas geometrias usam
	# as mesmas constantes para que a janela de uma seja a janela da outra.
	var canopy := _cylinder(1.05, 0.72, NOSE_TIP - 1.0, NOSE_TIP, false)
	canopy.material_override = materials.hull_paint
	add_child(canopy)

	# As janelas frontais, vistas de fora: uma faixa escura envolvendo o nariz.
	var band := MeshInstance3D.new()
	var band_mesh := CylinderMesh.new()
	band_mesh.top_radius = 1.09
	band_mesh.bottom_radius = 1.19
	band_mesh.height = 0.62
	band_mesh.radial_segments = 24
	band_mesh.cap_top = false
	band_mesh.cap_bottom = false
	band.mesh = band_mesh
	band.rotation_degrees = Vector3(0.0, 0.0, 90.0)
	band.position = Vector3(NOSE_TIP - 0.78, 0.0, 0.0)
	band.material_override = materials.glass
	add_child(band)

	# Anel de acoplamento no dorso do módulo de comando (regra 5: docking).
	var ring := MeshInstance3D.new()
	var torus := TorusMesh.new()
	torus.inner_radius = 0.52
	torus.outer_radius = 0.68
	torus.rings = 20
	ring.mesh = torus
	ring.position = Vector3(2.0, 0.0, CORE_RADIUS + 0.1)
	ring.material_override = materials.bare_metal
	add_child(ring)


func _build_habitat() -> void:
	var hull := _cylinder(CORE_RADIUS, CORE_RADIUS, CORE_AFT, CORE_FORWARD, false)
	hull.material_override = materials.hull_paint
	add_child(hull)

	# Três cintas de reforço. Uma superfície lisa de nove metros não tem escala;
	# as cintas são o que faz a nave parecer do tamanho que ela tem.
	for x in [-2.6, 0.0, 2.6]:
		var hoop := MeshInstance3D.new()
		var mesh := CylinderMesh.new()
		mesh.top_radius = CORE_RADIUS + 0.06
		mesh.bottom_radius = CORE_RADIUS + 0.06
		mesh.height = 0.18
		mesh.radial_segments = 24
		hoop.mesh = mesh
		hoop.rotation_degrees = Vector3(0.0, 0.0, 90.0)
		hoop.position = Vector3(x, 0.0, 0.0)
		hoop.material_override = materials.bare_metal
		add_child(hoop)

	# Escotilha lateral e duas vigias do habitat.
	for spec in [[0.4, 1.0], [-1.8, -1.0]]:
		var port := MeshInstance3D.new()
		var mesh := CylinderMesh.new()
		mesh.top_radius = 0.26
		mesh.bottom_radius = 0.26
		mesh.height = 0.08
		mesh.radial_segments = 16
		port.mesh = mesh
		port.rotation_degrees = Vector3(90.0, 0.0, 0.0)
		port.position = Vector3(spec[0], spec[1] * (CORE_RADIUS + 0.02), 0.4)
		port.material_override = materials.glass
		add_child(port)

	# Manta térmica cobrindo o terço traseiro do habitat.
	var blanket := _cylinder(CORE_RADIUS + 0.04, CORE_RADIUS + 0.04, CORE_AFT, CORE_AFT + 1.6, false)
	blanket.material_override = materials.thermal_blanket
	add_child(blanket)


func _build_truss() -> void:
	# Quatro longarinas ligando o casco pressurizado à seção de propulsão. Uma
	# treliça e não um tubo porque é isso que se constrói quando não há ar: não
	# há carga aerodinâmica para carenar.
	for offset in [Vector3(0.0, 0.9, 0.9), Vector3(0.0, -0.9, 0.9),
			Vector3(0.0, 0.9, -0.9), Vector3(0.0, -0.9, -0.9)]:
		var beam := _box(Vector3(POWER_AFT - CORE_AFT, 0.16, 0.16),
			Vector3((CORE_AFT + POWER_AFT) * 0.5, offset.y, offset.z))
		beam.material_override = materials.bare_metal
		add_child(beam)

	for x in [-5.5, -8.0, -10.5]:
		for pair in [[Vector3(0.0, 0.9, 0.9), Vector3(0.0, -0.9, 0.9)],
				[Vector3(0.0, 0.9, -0.9), Vector3(0.0, -0.9, -0.9)]]:
			var strut := _box(Vector3(0.10, 1.8, 0.10), Vector3(x, 0.0, pair[0].z))
			strut.material_override = materials.bare_metal
			add_child(strut)


func _build_tanks() -> void:
	for side in [1.0, -1.0]:
		var barrel := _cylinder(TANK_RADIUS, TANK_RADIUS, TANK_AFT + 0.9, TANK_FORWARD - 0.9)
		barrel.position = Vector3(0.0, side * TANK_OFFSET, 0.0)
		barrel.material_override = materials.hull_paint
		add_child(barrel)

		# Calotas hemisféricas: um tanque de pressão não tem tampa plana, e a
		# silhueta arredondada é metade do que faz a peça ler como tanque.
		for cap_x in [TANK_FORWARD - 0.9, TANK_AFT + 0.9]:
			var cap := MeshInstance3D.new()
			var sphere := SphereMesh.new()
			sphere.radius = TANK_RADIUS
			sphere.height = TANK_RADIUS * 2.0
			sphere.radial_segments = 20
			sphere.rings = 10
			cap.mesh = sphere
			cap.position = Vector3(cap_x, side * TANK_OFFSET, 0.0)
			cap.scale = Vector3(0.55, 1.0, 1.0)
			cap.material_override = materials.hull_paint
			add_child(cap)

		# Anel de manta térmica no meio de cada tanque.
		var wrap := _cylinder(TANK_RADIUS + 0.05, TANK_RADIUS + 0.05, -8.4, -6.6)
		wrap.position = Vector3(0.0, side * TANK_OFFSET, 0.0)
		wrap.material_override = materials.thermal_blanket
		add_child(wrap)

		# Linha de alimentação até a seção de potência.
		var feed := _cylinder(0.14, 0.14, POWER_AFT, TANK_AFT)
		feed.position = Vector3(0.0, side * TANK_OFFSET * 0.45, -0.7)
		feed.material_override = materials.bare_metal
		add_child(feed)


func _build_power_section() -> void:
	# O tambor do reator, com blindagem de sombra voltada para a tripulação: o
	# disco na face dianteira é a razão pela qual o habitat fica a dez metros
	# daqui, e desenhá-lo torna essa razão visível.
	var drum := _cylinder(1.3, 1.3, POWER_AFT, POWER_FORWARD)
	drum.material_override = materials.dark_composite
	add_child(drum)

	var shadow_shield := _cylinder(1.85, 1.85, POWER_FORWARD - 0.05, POWER_FORWARD + 0.25)
	shadow_shield.material_override = materials.bare_metal
	add_child(shadow_shield)

	for angle in range(0, 360, 60):
		var fin := _box(Vector3(1.8, 0.08, 0.9), Vector3((POWER_AFT + POWER_FORWARD) * 0.5, 0.0, 1.7))
		fin.material_override = materials.radiator
		fin.rotate_x(deg_to_rad(float(angle)))
		add_child(fin)


func _build_engine() -> void:
	var neck := _cylinder(0.95, 0.85, ENGINE_THROAT, POWER_AFT)
	neck.material_override = materials.bare_metal
	add_child(neck)

	# O sino. `CylinderMesh` com raios diferentes dá o tronco de cone; o sino de
	# verdade é uma parábola, e a diferença a esta distância é de um pixel.
	var bell := _cylinder(0.85, BELL_EXIT_RADIUS, ENGINE_EXIT, ENGINE_THROAT)
	bell.material_override = materials.engine_bell
	add_child(bell)

	var lip := MeshInstance3D.new()
	var torus := TorusMesh.new()
	torus.inner_radius = BELL_EXIT_RADIUS - 0.10
	torus.outer_radius = BELL_EXIT_RADIUS + 0.04
	torus.rings = 24
	lip.mesh = torus
	lip.rotation_degrees = Vector3(0.0, 0.0, 90.0)
	lip.position = Vector3(ENGINE_EXIT, 0.0, 0.0)
	lip.material_override = materials.bare_metal
	add_child(lip)

	engine_mount = Node3D.new()
	engine_mount.position = Vector3(ENGINE_EXIT, 0.0, 0.0)
	add_child(engine_mount)


func _build_radiators() -> void:
	# Dois painéis, para cima e para baixo, na sombra da blindagem e longe das
	# janelas. Finos -- 6 cm -- porque um radiador é uma superfície, e desenhá-lo
	# como uma placa grossa é a diferença entre uma nave e um brinquedo.
	for side in [1.0, -1.0]:
		var panel := _box(Vector3(6.6, 0.06, RADIATOR_SPAN - 1.6),
			Vector3(-7.6, 0.0, side * (1.6 + (RADIATOR_SPAN - 1.6) * 0.5)))
		panel.material_override = materials.radiator
		add_child(panel)

		for x in [-10.2, -8.6, -7.0, -5.4]:
			var rib := _box(Vector3(0.10, 0.10, RADIATOR_SPAN - 1.6),
				Vector3(x, 0.0, side * (1.6 + (RADIATOR_SPAN - 1.6) * 0.5)))
			rib.material_override = materials.bare_metal
			add_child(rib)

		var spar := _box(Vector3(6.8, 0.14, 0.14), Vector3(-7.6, 0.0, side * 1.7))
		spar.material_override = materials.bare_metal
		add_child(spar)


func _build_antennas() -> void:
	# Antena de alto ganho: o prato mais o mastro. Apontado para frente e para
	# cima, e ESTÁTICO -- um prato que rastreasse a Terra seria uma afirmação
	# sobre um sistema de comunicações que não existe no core.
	var mast := _cylinder(0.07, 0.07, 1.4, 2.9)
	mast.rotation_degrees = Vector3(0.0, -55.0, 0.0)
	mast.position = Vector3(1.6, 0.0, CORE_RADIUS)
	mast.material_override = materials.bare_metal
	add_child(mast)

	var dish := MeshInstance3D.new()
	var dish_mesh := CylinderMesh.new()
	dish_mesh.top_radius = 1.15
	dish_mesh.bottom_radius = 0.12
	dish_mesh.height = 0.34
	dish_mesh.radial_segments = 24
	dish.mesh = dish_mesh
	dish.rotation_degrees = Vector3(0.0, 0.0, -55.0)
	dish.position = Vector3(2.55, 0.0, CORE_RADIUS + 1.35)
	dish.material_override = materials.hull_paint
	add_child(dish)

	# Duas antenas de baixo ganho, uma de cada lado.
	for side in [1.0, -1.0]:
		var whip := _cylinder(0.035, 0.02, 0.0, 1.1)
		whip.rotation_degrees = Vector3(0.0, 0.0, side * 62.0)
		whip.position = Vector3(-1.2, side * CORE_RADIUS, 0.6)
		whip.material_override = materials.bare_metal
		add_child(whip)

	# Painel solar auxiliar: a nave é nuclear, mas a carga de emergência não é.
	for side in [1.0, -1.0]:
		var panel := _box(Vector3(2.4, 0.05, 1.3), Vector3(-2.4, side * 2.6, 0.0))
		panel.material_override = materials.solar_cell
		add_child(panel)
		var arm := _box(Vector3(0.10, 1.2, 0.10), Vector3(-2.4, side * 1.9, 0.0))
		arm.material_override = materials.bare_metal
		add_child(arm)


func _build_rcs_pods() -> void:
	## Os nacelles vão para as seis posições em que `RcsSystem::couples(2.0)` põe
	## os thrusters, e `rcs_mounts` guarda um nó por posição para que
	## `RcsVisual` pendure os jatos exatamente ali. Se o braço mudar no core, a
	## nave muda com ele -- a constante daqui é a mesma constante.
	for axis in [Vector3.RIGHT, Vector3.LEFT, Vector3.UP, Vector3.DOWN,
			Vector3.BACK, Vector3.FORWARD]:
		var at: Vector3 = axis * RCS_ARM
		var mount := Node3D.new()
		mount.position = at
		add_child(mount)
		rcs_mounts[at] = mount

		var pod := MeshInstance3D.new()
		var mesh := BoxMesh.new()
		mesh.size = Vector3(0.62, 0.62, 0.42)
		pod.mesh = mesh
		# O pod encosta na superfície: os de ±y e ±z ficam sobre o casco
		# cilíndrico, os de ±x na cintura, e todos se afastam do eixo o bastante
		# para que o bico fique do lado de fora.
		pod.position = at.normalized() * 0.18
		pod.material_override = materials.dark_composite
		mount.add_child(pod)


func _build_navigation_lights() -> void:
	for spec in [[Vector3(3.2, CORE_RADIUS + 0.1, 0.9), Palette.OK],
			[Vector3(3.2, -CORE_RADIUS - 0.1, 0.9), Palette.CRITICAL],
			[Vector3(-11.0, 0.0, 1.5), Palette.PRIMARY]]:
		var light := MeshInstance3D.new()
		var mesh := SphereMesh.new()
		mesh.radius = 0.09
		mesh.height = 0.18
		mesh.radial_segments = 8
		mesh.rings = 4
		light.mesh = mesh
		light.position = spec[0]
		light.material_override = ShipMaterials.emissive(spec[1], 3.0)
		add_child(light)
		_navigation_lights.append(light)


# --- primitivas --------------------------------------------------------------

func _cylinder(radius_aft: float, radius_forward: float, x_aft: float,
		x_forward: float, caps: bool = true) -> MeshInstance3D:
	## Um cilindro/tronco deitado ao longo de +x, entre duas estações.
	##
	## `CylinderMesh` nasce em pé ao longo de +y com o topo em +y, então "top" é o
	## lado de +x depois da rotação de -90 graus em z. Escrever isso aqui uma vez
	## evita a nave inteira sair ao contrário e ninguém notar, porque um tronco de
	## cone invertido continua parecendo um tronco de cone.
	##
	## ⚠️ `caps` existe por uma razão medida: `CylinderMesh` nasce COM tampas, e a
	## tampa traseira da carenagem do cockpit é um disco de 1,05 m de raio a 85 cm
	## do olho do piloto. A 68 graus de campo de visão isso subtende 51 graus de
	## meio-ângulo contra os 34 da câmera -- ou seja, tapa a tela inteira. A
	## primeira captura deste milestone foi um retângulo castanho uniforme, e era
	## isto: o piloto estava a olhar para o interior de uma tampa.
	##
	## As seções em que a câmera pode estar DENTRO são construídas sem tampas. As
	## outras -- tanques, tambor do reator, sino -- ficam com elas, porque delas
	## só se vê o exterior.
	var node := MeshInstance3D.new()
	var mesh := CylinderMesh.new()
	mesh.top_radius = radius_forward
	mesh.bottom_radius = radius_aft
	mesh.height = absf(x_forward - x_aft)
	mesh.radial_segments = 24
	mesh.rings = 1
	mesh.cap_top = caps
	mesh.cap_bottom = caps
	node.mesh = mesh
	node.rotation_degrees = Vector3(0.0, 0.0, -90.0)
	node.position = Vector3((x_forward + x_aft) * 0.5, 0.0, 0.0)
	return node


func _box(size: Vector3, at: Vector3) -> MeshInstance3D:
	var node := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = size
	node.mesh = mesh
	node.position = at
	return node


## Quanto da nave cabe numa esfera em torno do centro de massa, em metros. A
## câmera externa usa isto para escolher a distância de enquadramento, em vez de
## um número escolhido à mão que ficaria errado assim que a nave mudasse.
static func bounding_radius() -> float:
	return maxf(absf(ENGINE_EXIT), RADIATOR_SPAN) + 1.0
