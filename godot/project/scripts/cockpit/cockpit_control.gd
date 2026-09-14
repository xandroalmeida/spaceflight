class_name CockpitControl
extends Node3D
## Um controle físico no painel: botão, interruptor, seletor, lâmpada
## (regras 23, 24).
##
## Os quatro são o mesmo objeto com aparências diferentes, porque são a mesma
## coisa: uma superfície retangular no painel, com um estado, que responde ao
## ponteiro. Separá-los em quatro classes duplicaria o teste de acerto quatro
## vezes e é o teste de acerto que é a parte difícil.
##
## ## Por que o acerto não usa o servidor de física
##
## O interior do cockpit vive num `SubViewport` com mundo próprio, e apanhar o
## clique por `Area3D` exigiria um espaço de física nesse mundo só para
## interseções com retângulos conhecidos. O teste raio-plano abaixo tem seis
## linhas, não depende de servidor nenhum, funciona sem tela -- e é por isso que
## `tests/godot/test_cockpit_controls.gd` consegue verificá-lo.

enum Kind { BUTTON, SWITCH, ROTARY, INDICATOR }

const BEZEL := 0.006

var kind: int = Kind.BUTTON
var label := ""
var caption := ""
var half_size := Vector2(0.06, 0.02)
var on := false
var enabled := true
var indicator_colour: Color = Palette.OK
var hovered := false

## Chamado no clique. `CockpitButton` dispara e volta; `CockpitSwitch` alterna
## `on` antes de disparar, para que quem recebe leia o estado NOVO.
var pressed_callback: Callable = Callable()

var _face: MeshInstance3D
var _material: StandardMaterial3D
var _caption: Label3D
var _flash := 0.0


static func button(control_label: String, at: Vector3, width: float,
		callback: Callable) -> CockpitControl:
	var control := CockpitControl.new()
	control.kind = Kind.BUTTON
	control.label = control_label
	control.half_size = Vector2(width * 0.5, 0.019)
	control.position = at
	control.pressed_callback = callback
	return control


static func switch(control_label: String, at: Vector3, width: float, initial: bool,
		callback: Callable) -> CockpitControl:
	var control := CockpitControl.new()
	control.kind = Kind.SWITCH
	control.label = control_label
	control.half_size = Vector2(width * 0.5, 0.019)
	control.position = at
	control.on = initial
	control.pressed_callback = callback
	return control


static func indicator(control_label: String, at: Vector3, colour: Color) -> CockpitControl:
	var control := CockpitControl.new()
	control.kind = Kind.INDICATOR
	control.label = control_label
	control.half_size = Vector2(0.026, 0.012)
	control.position = at
	control.indicator_colour = colour
	return control


func _ready() -> void:
	_face = MeshInstance3D.new()
	var mesh := QuadMesh.new()
	mesh.size = half_size * 2.0
	_face.mesh = mesh
	_material = ShipMaterials.emissive(_resting_colour(), 0.35)
	_face.material_override = _material
	add_child(_face)

	_caption = Label3D.new()
	_caption.text = label
	_caption.font_size = 96
	# Um rótulo desenhado pelo Godot e não embutido numa textura (regra 44): o
	# texto fica nítido em qualquer resolução e pode ser traduzido.
	_caption.pixel_size = half_size.y / 130.0
	_caption.modulate = Palette.SECONDARY
	_caption.outline_size = 12
	_caption.outline_modulate = Color(0.0, 0.0, 0.0, 0.9)
	_caption.position = Vector3(0.0, 0.0, 0.002)
	_caption.billboard = BaseMaterial3D.BILLBOARD_DISABLED
	_caption.no_depth_test = false
	add_child(_caption)

	_refresh()


func _process(delta: float) -> void:
	if _flash <= 0.0:
		return
	# O retorno visual do clique (regra 24): a face acende e apaga em 180 ms.
	# Curto de propósito -- um botão que fica aceso meio segundo parece travado.
	_flash = maxf(_flash - delta / 0.18, 0.0)
	_refresh()


## O teste de acerto. `origin` e `direction` no referencial do MUNDO do viewport
## próximo -- que é onde `Camera3D.project_ray_origin` os devolve, e onde este nó
## tem o seu `global_transform`.
##
## ⚠️ Os dois têm de ser o MESMO referencial, e a primeira versão convertia o raio
## para o referencial do corpo antes de chamar isto -- contra um plano que
## continuava em coordenadas de mundo. Com a nave apontada para qualquer lado que
## não o eixo x de J2000, os botões ficavam a alguns metros de onde eram
## desenhados. O teste passava porque, fora da árvore, `global_transform` devolve
## a identidade e os dois referenciais coincidem por acidente.
func hit_test(origin: Vector3, direction: Vector3) -> bool:
	if not enabled or kind == Kind.INDICATOR:
		return false
	# Fora da árvore não existe transformação global; a local é a única que há, e
	# é a resposta certa em vez de uma identidade silenciosa.
	var transform := global_transform if is_inside_tree() else self.transform
	var normal := transform.basis.z.normalized()
	var denominator := normal.dot(direction)
	if absf(denominator) < 1.0e-6:
		return false                       # raio paralelo ao painel
	var distance := normal.dot(transform.origin - origin) / denominator
	if distance <= 0.0:
		return false                       # o painel está atrás do observador
	var local := transform.affine_inverse() * (origin + direction * distance)
	return absf(local.x) <= half_size.x and absf(local.y) <= half_size.y


func press() -> void:
	if not enabled:
		return
	if kind == Kind.SWITCH:
		on = not on
	_flash = 1.0
	_refresh()
	if pressed_callback.is_valid():
		pressed_callback.call(self)


func set_on(value: bool) -> void:
	if on == value:
		return
	on = value
	_refresh()


func set_hovered(value: bool) -> void:
	if hovered == value:
		return
	hovered = value
	_refresh()


func set_caption(text: String) -> void:
	if caption == text:
		return
	caption = text
	if _caption != null:
		_caption.text = label if caption.is_empty() else "%s\n%s" % [label, caption]


func _refresh() -> void:
	if _material == null:
		return
	var colour := _resting_colour()
	# Realce DISCRETO no hover (regra 24). 18 % mais claro: o suficiente para
	# dizer "isto responde" e pouco o bastante para não piscar quando o ponteiro
	# atravessa o painel.
	if hovered:
		colour = colour.lightened(0.18)
	if _flash > 0.0:
		colour = colour.lerp(Palette.PRIMARY, _flash * 0.75)
	_material.albedo_color = colour
	_material.emission = colour
	_material.emission_energy_multiplier = 0.25 + (0.9 if on else 0.0) + _flash * 0.8
	if _caption != null:
		_caption.modulate = Palette.PRIMARY if on else Palette.SECONDARY


func _resting_colour() -> Color:
	if not enabled:
		return Palette.PANEL.darkened(0.3)
	match kind:
		Kind.INDICATOR:
			return indicator_colour if on else Palette.PANEL_EDGE.darkened(0.4)
		Kind.SWITCH:
			return Palette.NAV_DIM if on else Palette.PANEL_EDGE
		_:
			return Palette.PANEL_EDGE
