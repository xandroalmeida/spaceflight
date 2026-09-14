class_name RcsVisual
extends Node3D
## Os jatos de RCS, acesos pelo ATUADOR (regra 15).
##
## Este é o ponto da regra 15 e vale dizê-lo por inteiro: o renderizador não
## sabe que tecla foi premida. Ele pede a `SpaceflightSimulation` o vetor de
## acionamentos -- `get_rcs_throttles()`, que é o mesmo `RcsSystem::allocate`
## que o modelo de forças voa -- e acende cada bico na proporção em que aquele
## bico está aberto.
##
## Isso não é purismo. O alocador é guloso e proporcional: um comando diagonal
## abre quatro thrusters a frações diferentes, e um comando saturado abre dois
## no máximo. Desenhar a tecla mostraria quatro chamas iguais onde há duas
## fortes e duas fracas, e mostraria chama onde o alocador decidiu não abrir
## nada.
##
## A geometria também vem do core: `get_rcs_thrusters()` devolve posição e
## direção da FORÇA no referencial do corpo. A exaustão sai para o outro lado, e
## essa inversão é feita aqui, uma vez, onde está escrita.

const JET_LENGTH := 1.35          ## [m] a plena abertura
const JET_RADIUS := 0.20
const THRESHOLD := 0.002          ## abaixo disto não há chama que se veja

var _jets: Array[MeshInstance3D] = []
var _materials: Array[StandardMaterial3D] = []
var _throttles: PackedFloat64Array = PackedFloat64Array()


## `thrusters` é o que `SpaceflightSimulation.get_rcs_thrusters()` devolve.
func build(thrusters: Array) -> void:
	for jet in _jets:
		jet.queue_free()
	_jets.clear()
	_materials.clear()

	for entry in thrusters:
		var spec: Dictionary = entry
		var position: Vector3 = spec["position"]
		var force_direction: Vector3 = spec["force_direction"]
		# A exaustão vai para onde a força NÃO vai. Terceira lei, e a única
		# linha deste arquivo que precisa dela.
		var exhaust := -force_direction.normalized()

		var material := ShipMaterials.additive(Palette.RCS)
		var jet := MeshInstance3D.new()
		var mesh := CylinderMesh.new()
		mesh.top_radius = JET_RADIUS
		mesh.bottom_radius = JET_RADIUS * 0.22
		mesh.height = JET_LENGTH
		mesh.radial_segments = 10
		mesh.rings = 1
		jet.mesh = mesh
		jet.material_override = material
		# `CylinderMesh` cresce ao longo de +y; o jato tem de crescer ao longo de
		# `exhaust`, partindo da boca do bico.
		var basis_up := Vector3.UP
		if absf(exhaust.dot(basis_up)) > 0.98:
			basis_up = Vector3.RIGHT
		jet.position = position + exhaust * 0.3
		jet.look_at_from_position(jet.position, jet.position + exhaust, basis_up)
		# look_at aponta -z; a malha cresce em +y. Um quarto de volta em x resolve.
		jet.rotate_object_local(Vector3.RIGHT, -PI * 0.5)
		jet.visible = false
		add_child(jet)
		_jets.append(jet)
		_materials.append(material)


## `throttles` é `get_rcs_throttles()`: uma abertura por thruster, na mesma
## ordem de `get_rcs_thrusters()`.
func set_throttles(throttles: PackedFloat64Array) -> void:
	_throttles = throttles
	for i in range(_jets.size()):
		var open: float = _throttles[i] if i < _throttles.size() else 0.0
		var lit := open > THRESHOLD
		_jets[i].visible = lit
		if not lit:
			continue
		# O comprimento segue a abertura; o alfa também. Um bico a 12 % é uma
		# lamparina e um bico a 100 % é uma chama, e a diferença tem de ser
		# legível porque é ela que diz o que o alocador fez.
		_jets[i].scale = Vector3(0.55 + 0.45 * open, 0.35 + 0.65 * open, 0.55 + 0.45 * open)
		_materials[i].albedo_color = Palette.RCS * Color(1.0, 1.0, 1.0, 0.25 + 0.55 * open)


## Quantos bicos estão abertos, para o instrumento de RCS dizer "4 de 12" em vez
## de "ligado".
func firing_count() -> int:
	var count := 0
	for value in _throttles:
		if value > THRESHOLD:
			count += 1
	return count


func total_demand() -> float:
	var sum := 0.0
	for value in _throttles:
		sum += value
	return sum
