class_name EnginePlume
extends Node3D
## A pluma do motor principal (regra 16).
##
## A intensidade vem do EMPUXO REAL -- `snapshot.thrust_n`, que é o que
## `MainEngineForce` está aplicando ao estado neste instante -- e não da tecla
## que o piloto segurou. A diferença é observável: com o tanque vazio a tecla
## continua funcionando e o empuxo é zero, e a pluma tem de apagar.
##
## Não é plasma. É um cone aditivo com um núcleo mais claro, um disco de choque e
## uma luz. O M7 não pede física de exaustão (regra 16), pede que o motor ligado
## pareça um motor ligado -- e que apague quando o motor apaga.

const IMPULSE_REFERENCE_N := 200_000.0   ## empuxo de referência do modo IMPULSE
const MAX_LENGTH := 14.0                 ## [m] a plena potência, ~3,5 diâmetros de sino
const CORE_FRACTION := 0.42

var _cone: MeshInstance3D
var _core: MeshInstance3D
var _shock: MeshInstance3D
var _light: OmniLight3D
var _cone_material: StandardMaterial3D
var _core_material: StandardMaterial3D
var _shock_material: StandardMaterial3D
var _intensity := 0.0
var _flicker := 0.0


func _ready() -> void:
	_cone_material = ShipMaterials.additive(Palette.ENGINE * 0.55)
	_core_material = ShipMaterials.additive(Color(1.0, 0.97, 0.92))
	_shock_material = ShipMaterials.additive(Color(0.85, 0.92, 1.0))

	_cone = _make_cone(1.0, 0.35, _cone_material)
	add_child(_cone)
	_core = _make_cone(0.9, 0.12, _core_material)
	add_child(_core)

	_shock = MeshInstance3D.new()
	var shock_mesh := SphereMesh.new()
	shock_mesh.radius = 0.75
	shock_mesh.height = 1.5
	shock_mesh.radial_segments = 16
	shock_mesh.rings = 8
	_shock.mesh = shock_mesh
	_shock.material_override = _shock_material
	add_child(_shock)

	# A luz da pluma ilumina o próprio casco, que é como se percebe que ela está
	# atrás da nave e não desenhada por cima dela.
	_light = OmniLight3D.new()
	_light.light_color = Palette.ENGINE
	_light.omni_range = 34.0
	_light.light_energy = 0.0
	add_child(_light)

	set_intensity(0.0)


## `thrust_n` é o empuxo instantâneo do core. A referência é o ponto de operação
## IMPULSE (200 kN); no modo CRUISE o empuxo é 18 vezes menor e a pluma é
## proporcionalmente menor, o que é a verdade sobre os dois modos e não um bug.
func set_thrust(thrust_n: float) -> void:
	set_intensity(clampf(thrust_n / IMPULSE_REFERENCE_N, 0.0, 1.0))


func set_intensity(value: float) -> void:
	_intensity = clampf(value, 0.0, 1.0)
	var lit := _intensity > 0.0005
	visible = lit
	if not lit:
		_light.light_energy = 0.0
		return

	# Raiz quadrada e não linear: metade do empuxo não dá metade do comprimento
	# em nenhum motor, e a raiz é o que faz um acelerador a 10 % ainda mostrar
	# alguma coisa em vez de nada.
	var length := MAX_LENGTH * sqrt(_intensity)
	var width := 1.0 + 0.7 * _intensity

	_stretch(_cone, length, width)
	_stretch(_core, length * CORE_FRACTION, width * 0.55)
	_shock.position = Vector3(-length * 0.13, 0.0, 0.0)
	_shock.scale = Vector3(0.5, width * 0.5, width * 0.5)
	# A luz vive no MEIO da pluma e não na tubeira: é assim que ela ilumina o
	# casco por trás, que é o que faz a pluma ler como estando atrás da nave.
	_light.position = Vector3(-length * 0.35, 0.0, 0.0)

	_cone_material.albedo_color = _glow(Palette.ENGINE, 0.10 + 0.30 * _intensity)
	_core_material.albedo_color = _glow(Color(1.0, 0.97, 0.92), 0.20 + 0.40 * _intensity)
	_shock_material.albedo_color = _glow(Color(0.85, 0.92, 1.0), 0.10 + 0.24 * _intensity)
	_light.light_energy = 3.5 * _intensity


func _process(delta: float) -> void:
	if not visible:
		return
	# Cintilação de ±4 %. Sem ela a pluma é um cone sólido e lê como geometria;
	# com mais que isso vira fogo de câmpo. O valor NÃO realimenta nada: é
	# escala de desenho, e o empuxo que a simulação usa continua constante.
	_flicker = fmod(_flicker + delta * 11.0, TAU)
	var jitter := 1.0 + 0.04 * sin(_flicker) + 0.02 * sin(_flicker * 2.7)
	_stretch(_cone, MAX_LENGTH * sqrt(_intensity) * jitter, 1.0 + 0.7 * _intensity)


## Uma cor aditiva a `level` de intensidade.
##
## ⚠️ Com `BLEND_MODE_ADD` quem manda é o **RGB**, não o alfa: o que se soma ao
## que está por baixo é a cor. Baixar o alfa de 0,50 para 0,18 não escureceu a
## pluma um pixel -- o cone continuava a sair como um cone de tinta opaco. Estes
## números só puderam ser calibrados depois de a composição do viewport
## transparente ser corrigida; enquanto a pluma não chegava à tela, podiam ser o
## que fosse, e eram.
static func _glow(colour: Color, level: float) -> Color:
	return Color(colour.r * level, colour.g * level, colour.b * level, level)


## Estica um cone ao longo de -x, do bocal para fora.
##
## ⚠️ A escala de um `Node3D` aplica-se ANTES da rotação: `basis = R · S`. A
## malha cresce em +y, logo quem a ALONGA é `scale.y`. Escalar `scale.x`
## alargava-a de través -- a pluma a 100 % era um disco de 26 m de largura por
## 2,6 m de comprimento, e a posição, fixada uma vez na construção, deixava-a
## para trás do que ela devia cobrir.
static func _stretch(node: MeshInstance3D, length: float, width: float) -> void:
	node.scale = Vector3(width, length, width)
	node.position = Vector3(-length * 0.5, 0.0, 0.0)


func _make_cone(exit_radius: float, throat_radius: float,
		material: Material) -> MeshInstance3D:
	## Um metro de malha, esticado por `_stretch`. Um quarto de volta em z leva
	## +y para -x, que é por onde a exaustão sai; o TOPO da malha fica portanto no
	## extremo afastado do bocal, e é ele que tem de levar o raio maior -- uma
	## exaustão expande-se ao sair.
	var node := MeshInstance3D.new()
	var mesh := CylinderMesh.new()
	mesh.top_radius = exit_radius
	mesh.bottom_radius = throat_radius
	mesh.height = 1.0
	mesh.radial_segments = 20
	mesh.rings = 1
	node.mesh = mesh
	node.rotation_degrees = Vector3(0.0, 0.0, 90.0)
	node.material_override = material
	return node
