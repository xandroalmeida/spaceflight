class_name CockpitInterior
extends Node3D
## O posto de pilotagem, em metros, no referencial do corpo
## (regras 8, 9, 49, 51, 52).
##
## Construído em código, com primitivas, porque é o que a regra 49 pede e porque
## um modelo artístico não existe. Estética: cápsula moderna -- superfícies
## grandes e lisas, poucas peças, tudo funcional. Não é um caça: há sete botões
## físicos e cada um faz uma coisa (regra 8).
##
## ## A geometria
##
##     x = 4.9  ┌──────────────────┐   ponta do cone, janela frontal
##              │   JANELAS        │
##     x = 4.0  ├──────────────────┤   quebra-luz
##              │ FLIGHT NAV TARGET│   painel principal, inclinado para o piloto
##              │  SYSTEM DISPLAY  │
##              │  botões físicos  │
##     x = 3.15 │        ●         │   olho do piloto
##     x = 2.3  └──────────────────┘   anteparo traseiro, escotilha
##
## O olho está em `CameraRig.EYE`, e a constante é a MESMA: o painel é construído
## em volta de onde a câmera está, e não ao lado dela.
##
## ## Iluminação (regra 51)
##
## Duas luzes fracas e os próprios mostradores. O interior tem de ser legível E o
## exterior tem de continuar observável, e a maneira de conseguir os dois não é
## iluminar mais: é iluminar POUCO. Os mostradores emitem, o resto é penumbra, e
## o olho do jogador lê os dois porque o monitor tem alcance dinâmico de sobra
## para a diferença entre um painel a 0,3 e a Terra a 1,0.

const EYE := Vector3(3.15, 0.0, 0.36)     ## tem de bater com CameraRig.EYE

## Onde o painel está, e o número não é uma preferência.
##
## A câmera tem 75 graus verticais (meio-ângulo 37,5) e repousa 10 graus abaixo
## da linha do nariz, de modo que a banda visível vai de +27,5 a -47,5 graus.
## Daqui, e do olho, saem os ângulos das três fileiras:
##
##     lâmpadas        -23 graus
##     FLIGHT/NAV/TGT  -28 graus
##     SYSTEMS         -43 graus
##     botões          -49 graus   (a borda de baixo: pede um olhar para baixo)
##
## A primeira versão punha o painel a 55 graus abaixo, com o que ele ficava
## inteiramente fora do quadro e o cockpit aparecia como uma caixa vazia com
## estrelas ao fundo. Os ângulos estão escritos aqui porque são o que decide se
## o instrumento existe para quem está sentado.
const PANEL_CENTRE := Vector3(3.95, 0.0, -0.30)
const PANEL_HALF_WIDTH := 0.95
const CABIN_HALF_WIDTH := 1.02
const FLOOR_Z := -0.92
const CEILING_Z := 0.98
const AFT_X := 2.30
const WINDOW_X := 4.06

## Resolução dos mostradores. Escolhida pela proporção física de cada painel,
## para que um pixel seja quadrado -- um mostrador esticado é a primeira coisa
## que se nota e a última que se procura.
## A base de uma vidraça: normal para +x (o nariz), "direita" para -y, "cima"
## para +z. É a inversa exata de `CameraRig.COCKPIT_ALIGN`, e tem de ser, ou o
## marcador que aparece à direita no mostrador aparece à esquerda na janela.
const PANE_BASIS := Basis(Vector3(0.0, -1.0, 0.0), Vector3(0.0, 0.0, 1.0),
	Vector3(1.0, 0.0, 0.0))

const SMALL_DISPLAY_SIZE := Vector2i(480, 315)
const WIDE_DISPLAY_SIZE := Vector2i(1280, 122)

var materials: ShipMaterials
var panel: Node3D

var flight_display: FlightDisplay
var nav_display: NavDisplay
var target_display: TargetDisplay
var system_display: SystemDisplay

var controls: Array[CockpitControl] = []
var indicators: Dictionary = {}

var _viewports: Array[SubViewport] = []
var _hovered: CockpitControl = null


func _init(shared_materials: ShipMaterials = null) -> void:
	materials = shared_materials if shared_materials != null else ShipMaterials.new()


func _ready() -> void:
	_build_shell()
	_build_windows()
	_build_panel()
	_build_side_consoles()
	_build_seat()
	_build_lighting()


# --- casca -------------------------------------------------------------------

func _build_shell() -> void:
	var floor_panel := _box(Vector3(WINDOW_X - AFT_X, CABIN_HALF_WIDTH * 2.0, 0.06),
		Vector3((WINDOW_X + AFT_X) * 0.5, 0.0, FLOOR_Z))
	floor_panel.material_override = materials.dark_composite
	add_child(floor_panel)

	# O teto vai até a janela e não até meio metro antes dela. Na primeira
	# captura havia um triângulo de espaço no canto superior esquerdo: era o vão
	# entre a borda dianteira do teto (x = 3,56) e a janela (x = 4,06), e o
	# viewport transparente mostra o mundo em toda a parte onde não há geometria
	# -- que é o que faz a janela funcionar e o que faz um buraco no teto
	# funcionar igualmente bem.
	var ceiling := _box(Vector3(WINDOW_X - AFT_X + 0.10, CABIN_HALF_WIDTH * 2.0, 0.06),
		Vector3((WINDOW_X + AFT_X) * 0.5 + 0.05, 0.0, CEILING_Z))
	ceiling.material_override = materials.cockpit_panel
	add_child(ceiling)

	for side in [1.0, -1.0]:
		var wall := _box(Vector3(WINDOW_X - AFT_X + 0.10, 0.06, CEILING_Z - FLOOR_Z),
			Vector3((WINDOW_X + AFT_X) * 0.5 + 0.05, side * CABIN_HALF_WIDTH,
				(CEILING_Z + FLOOR_Z) * 0.5))
		wall.material_override = materials.cockpit_panel
		add_child(wall)

	# O anteparo traseiro com a escotilha para o habitat. Sem ele, olhar para trás
	# mostraria o espaço a partir de dentro da nave -- que é o que o viewport
	# transparente faz onde não há geometria, e é por isso que a casca tem de ser
	# fechada e não só um painel à frente.
	var bulkhead := _box(Vector3(0.06, CABIN_HALF_WIDTH * 2.0, CEILING_Z - FLOOR_Z),
		Vector3(AFT_X, 0.0, (CEILING_Z + FLOOR_Z) * 0.5))
	bulkhead.material_override = materials.cockpit_panel
	add_child(bulkhead)

	var hatch := MeshInstance3D.new()
	var hatch_mesh := CylinderMesh.new()
	hatch_mesh.top_radius = 0.40
	hatch_mesh.bottom_radius = 0.40
	hatch_mesh.height = 0.05
	hatch_mesh.radial_segments = 24
	hatch.mesh = hatch_mesh
	hatch.rotation_degrees = Vector3(0.0, 0.0, 90.0)
	hatch.position = Vector3(AFT_X + 0.04, 0.0, -0.25)
	hatch.material_override = materials.bare_metal
	add_child(hatch)

	# Painel superior: alguns disjuntores e nada que se possa carregar. Está aqui
	# porque uma cabine sem teto lê como um cenário aberto, e porque quando o
	# piloto olha para cima tem de haver nave e não vazio.
	var overhead := _box(Vector3(0.62, 1.40, 0.05), Vector3(3.48, 0.0, CEILING_Z - 0.10))
	overhead.material_override = materials.cockpit_panel
	add_child(overhead)
	for row in range(2):
		for column in range(8):
			var breaker := _box(Vector3(0.045, 0.045, 0.018),
				Vector3(3.30 + row * 0.13, -0.56 + column * 0.16, CEILING_Z - 0.135))
			breaker.material_override = materials.bare_metal
			add_child(breaker)


func _build_windows() -> void:
	## Três painéis: um frontal e dois laterais inclinados. A moldura é sólida e
	## o vidro é quase transparente (regra 52) -- o objetivo é pilotar, e uma
	## janela que reflete bonito e esconde a Terra é uma janela que falhou.
	var panes := [
		[Vector3(WINDOW_X + 0.48, 0.0, 0.42), 0.0, Vector2(1.46, 0.94)],
		[Vector3(WINDOW_X + 0.16, 0.90, 0.38), 54.0, Vector2(0.80, 0.80)],
		[Vector3(WINDOW_X + 0.16, -0.90, 0.38), -54.0, Vector2(0.80, 0.80)],
	]
	for pane in panes:
		var glass := MeshInstance3D.new()
		var mesh := QuadMesh.new()
		mesh.size = pane[2]
		glass.mesh = mesh
		glass.position = pane[0]
		# O quad nasce no plano XY virado para +z, e tem de ficar virado para +x,
		# que é para onde o piloto olha. A base é escrita por extenso em vez de
		# montada com ângulos de Euler: três rotações compostas numa ordem que o
		# Godot escolhe (YXZ) são um convite a uma janela virada para o lado, e
		# uma janela virada para o lado ainda parece uma janela.
		glass.basis = PANE_BASIS.rotated(Vector3(0.0, 0.0, 1.0), deg_to_rad(pane[1]))
		glass.material_override = materials.glass
		add_child(glass)

		# A moldura: quatro barras à volta do vidro. Ela é o que dá à vista uma
		# escala -- um espaço sem nada em primeiro plano não tem tamanho.
		_frame_around(glass.position, glass.basis, pane[2])

	# Quebra-luz: a aba escura entre as janelas e o painel. Impede que o painel
	# aceso apareça refletido no vidro, e é por isso que ela existe nas cápsulas
	# de verdade.
	# ⚠️ O quebra-luz tem de ficar ACIMA dos mostradores, e o "acima" é medido e
	# não estimado: a primeira posição pôs uma placa de 34 cm inclinada com a
	# borda traseira em (4,11; −0,16), que é exatamente onde a fileira
	# FLIGHT/NAV/TARGET está — e o painel apareceu na captura como uma tábua
	# escura sem mostrador nenhum. Agora ele está em z = +0,06, dez centímetros
	# acima da borda de cima do painel.
	var shield := _box(Vector3(0.26, 2.0, 0.030), Vector3(WINDOW_X + 0.30, 0.0, 0.06))
	shield.rotation_degrees = Vector3(0.0, -30.0, 0.0)
	shield.material_override = materials.dark_composite
	add_child(shield)


func _frame_around(at: Vector3, pane_basis: Basis, pane: Vector2) -> void:
	## Quatro barras no plano do vidro, em coordenadas do PRÓPRIO vidro: o quad
	## vive no seu XY, então a moldura também, e a mesma base leva as duas ao
	## lugar. Sem isso, cada janela precisaria da sua própria conta.
	var bar := 0.055
	var holder := Node3D.new()
	holder.position = at
	holder.basis = pane_basis
	add_child(holder)
	for spec in [
			[Vector3(0.0, (pane.y + bar) * 0.5, 0.0), Vector3(pane.x + bar * 2.0, bar, bar)],
			[Vector3(0.0, -(pane.y + bar) * 0.5, 0.0), Vector3(pane.x + bar * 2.0, bar, bar)],
			[Vector3((pane.x + bar) * 0.5, 0.0, 0.0), Vector3(bar, pane.y + bar * 2.0, bar)],
			[Vector3(-(pane.x + bar) * 0.5, 0.0, 0.0), Vector3(bar, pane.y + bar * 2.0, bar)]]:
		var piece := _box(spec[1], spec[0])
		piece.material_override = materials.cockpit_panel
		holder.add_child(piece)


# --- painel principal --------------------------------------------------------

func _build_panel() -> void:
	panel = Node3D.new()
	panel.position = PANEL_CENTRE
	# A normal do painel aponta para o olho do piloto, calculada e não estimada:
	# um painel inclinado "por gosto" fica a ler-se de esguelha exatamente da
	# posição de onde ele é lido.
	var normal := (EYE - PANEL_CENTRE).normalized()
	var across := Vector3(0.0, -1.0, 0.0)       # direita, do ponto de vista do piloto
	var up := normal.cross(across).normalized()
	panel.basis = Basis(across, up, normal)
	add_child(panel)

	var backing := MeshInstance3D.new()
	var backing_mesh := BoxMesh.new()
	backing_mesh.size = Vector3(PANEL_HALF_WIDTH * 2.0 + 0.08, 1.16, 0.05)
	backing.mesh = backing_mesh
	backing.position = Vector3(0.0, -0.09, -0.028)
	backing.material_override = materials.cockpit_panel
	panel.add_child(backing)

	flight_display = FlightDisplay.new()
	nav_display = NavDisplay.new()
	target_display = TargetDisplay.new()
	system_display = SystemDisplay.new()

	_mount_display(flight_display, Vector3(-0.620, 0.210, 0.0), Vector2(0.58, 0.380),
		SMALL_DISPLAY_SIZE)
	_mount_display(nav_display, Vector3(0.0, 0.210, 0.0), Vector2(0.58, 0.380),
		SMALL_DISPLAY_SIZE)
	_mount_display(target_display, Vector3(0.620, 0.210, 0.0), Vector2(0.58, 0.380),
		SMALL_DISPLAY_SIZE)
	_mount_display(system_display, Vector3(0.0, -0.050, 0.0), Vector2(1.84, 0.175),
		WIDE_DISPLAY_SIZE)

	_build_button_strip()
	_build_indicators()


func _mount_display(instrument: Instrument, at: Vector3, physical: Vector2,
		resolution: Vector2i) -> void:
	var viewport := SubViewport.new()
	viewport.size = resolution
	viewport.transparent_bg = false
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	# Sem entrada, sem áudio, sem 3D: é uma superfície de desenho. Dizê-lo poupa
	# ao Godot montar um mundo inteiro por mostrador, quatro vezes.
	viewport.disable_3d = true
	viewport.gui_disable_input = true
	viewport.audio_listener_enable_2d = false
	add_child(viewport)
	_viewports.append(viewport)

	viewport.add_child(instrument)
	# Tamanho explícito, âncoras no canto superior esquerdo (o padrão).
	#
	# Duas variantes erradas, ambas testadas numa captura:
	#   `size` ANTES de entrar na árvore  -- nem sempre sobrevive;
	#   âncoras FULL_RECT e mais nada     -- o `Control` fica com tamanho zero até
	#       à primeira passagem de layout, `_draw` não pinta nada, e o que se vê é
	#       a cor de limpeza do `SubViewport`: quatro retângulos cinzentos onde
	#       deviam estar os mostradores.
	# FULL_RECT *e* `size` funciona e faz o Godot avisar que um vai sobrescrever o
	# outro. Isto é a combinação que funciona sem aviso.
	instrument.position = Vector2.ZERO
	instrument.size = Vector2(resolution)

	var screen := MeshInstance3D.new()
	var mesh := QuadMesh.new()
	mesh.size = physical
	screen.mesh = mesh
	# Dez milímetros à frente do painel, e o número importa: o bisel abaixo é uma
	# CAIXA de 12 mm de espessura centrada em z = 0, ou seja, ela ocupa de −6 a
	# +6 mm. A primeira versão pôs a tela a +4 mm -- DENTRO da caixa -- e o bisel
	# desenhou-se por cima dela. Na captura o painel aparecia como uma tábua
	# escura com quatro lâmpadas e nenhum mostrador.
	screen.position = at + Vector3(0.0, 0.0, 0.010)
	var material := StandardMaterial3D.new()
	material.albedo_texture = viewport.get_texture()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	# Emissivo, com a mesma textura: um mostrador acende, e é essa luz que a
	# regra 51 pede que exista sem que ela lave o resto da cabine.
	material.emission_enabled = true
	material.emission_texture = viewport.get_texture()
	material.emission_energy_multiplier = 0.85
	screen.material_override = material
	panel.add_child(screen)

	# A moldura do vidro do mostrador, meio centímetro à frente: dá borda ao
	# painel e é o que distingue "um mostrador" de "uma imagem colada".
	var bezel := MeshInstance3D.new()
	var bezel_mesh := BoxMesh.new()
	bezel_mesh.size = Vector3(physical.x + 0.045, physical.y + 0.045, 0.012)
	bezel.mesh = bezel_mesh
	bezel.position = at
	bezel.material_override = materials.display_glass
	panel.add_child(bezel)


func _build_button_strip() -> void:
	## Sete botões, e cada um faz exatamente uma coisa (regra 24). A lista é
	## preenchida por `Flight`, que é quem sabe o que cada um comanda; aqui só
	## estão as posições.
	var width := 0.24
	var gap := 0.265
	for i in range(7):
		var at := Vector3(-gap * 3.0 + gap * i, -0.152, 0.010)
		var placeholder := CockpitControl.button("", at, width, Callable())
		placeholder.enabled = false
		panel.add_child(placeholder)
		controls.append(placeholder)


func _build_indicators() -> void:
	## Quatro lâmpadas na aba do quebra-luz, onde ficam no campo de visão sem
	## estar no caminho. Nomes curtos, porque uma lâmpada que precisa de uma
	## frase não é uma lâmpada.
	var names := ["MSTR", "RCS", "ENG", "AP"]
	var colours := [Palette.CRITICAL, Palette.NAV, Palette.ENGINE, Palette.PLAN]
	for i in range(names.size()):
		var lamp := CockpitControl.indicator(names[i],
			Vector3(-0.42 + i * 0.28, 0.425, 0.008), colours[i])
		panel.add_child(lamp)
		controls.append(lamp)
		indicators[names[i]] = lamp


# --- consoles, assento, luz --------------------------------------------------

func _build_side_consoles() -> void:
	for side in [1.0, -1.0]:
		var console := _box(Vector3(0.80, 0.30, 0.09),
			Vector3(3.62, side * 0.80, -0.62))
		console.rotation_degrees = Vector3(side * 16.0, 0.0, 0.0)
		console.material_override = materials.cockpit_panel
		add_child(console)
		for i in range(4):
			var knob := MeshInstance3D.new()
			var mesh := CylinderMesh.new()
			mesh.top_radius = 0.028
			mesh.bottom_radius = 0.034
			mesh.height = 0.035
			mesh.radial_segments = 12
			knob.mesh = mesh
			knob.position = Vector3(3.40 + i * 0.16, side * 0.80, -0.575)
			knob.material_override = materials.bare_metal
			add_child(knob)

	# Manche lateral direito. Não é interativo: quem pilota é o teclado, e um
	# manche que se mexesse sem comandar nada seria decoração a fingir ser
	# instrumento.
	var stick := _box(Vector3(0.05, 0.05, 0.18), Vector3(3.30, -0.76, -0.47))
	stick.material_override = materials.dark_composite
	add_child(stick)
	var grip := MeshInstance3D.new()
	var grip_mesh := SphereMesh.new()
	grip_mesh.radius = 0.048
	grip_mesh.height = 0.096
	grip.mesh = grip_mesh
	grip.position = Vector3(3.30, -0.76, -0.38)
	grip.material_override = materials.dark_composite
	add_child(grip)


func _build_seat() -> void:
	## Só o que se vê do assento de onde se está sentado: os apoios de braço e a
	## borda do encosto. Modelar a cadeira inteira desenharia geometria dentro da
	## câmera.
	for side in [1.0, -1.0]:
		var armrest := _box(Vector3(0.52, 0.09, 0.06), Vector3(3.02, side * 0.42, -0.14))
		armrest.material_override = materials.dark_composite
		add_child(armrest)
	var back := _box(Vector3(0.07, 0.62, 0.50), Vector3(AFT_X + 0.24, 0.0, 0.02))
	back.material_override = materials.dark_composite
	add_child(back)


func _build_lighting() -> void:
	# Luz de inundação fraca vinda de trás do piloto, quente, e uma luz branca
	# rasante sobre o painel. Duas, não cinco: o que faz a cabine legível é o
	# contraste entre o painel aceso e a penumbra, e mais luz destrói justamente
	# isso (regra 51).
	var flood := OmniLight3D.new()
	flood.position = Vector3(2.85, 0.0, CEILING_Z - 0.16)
	flood.light_color = Color(1.0, 0.88, 0.72)
	flood.light_energy = 1.15
	flood.omni_range = 4.0
	add_child(flood)

	var panel_light := OmniLight3D.new()
	# Debaixo do quebra-luz e apontada para o painel, como nas cápsulas de
	# verdade. Alcance largo e energia baixa: uma luz forte e próxima produz um
	# halo no meio do painel, que foi o que a primeira captura mostrou.
	panel_light.position = Vector3(WINDOW_X + 0.26, 0.0, 0.03)
	panel_light.light_color = Color(0.82, 0.89, 1.0)
	panel_light.light_energy = 0.30
	panel_light.omni_range = 4.5
	add_child(panel_light)


# --- interação ---------------------------------------------------------------

## Um raio vindo da câmera próxima, em metros, no referencial do corpo.
## Devolve o controle sob o ponteiro, ou `null`.
func pick(origin: Vector3, direction: Vector3) -> CockpitControl:
	for control in controls:
		if control.hit_test(origin, direction):
			return control
	return null


func set_hover(control: CockpitControl) -> void:
	if _hovered == control:
		return
	if _hovered != null:
		_hovered.set_hovered(false)
	_hovered = control
	if _hovered != null:
		_hovered.set_hovered(true)


func hovered() -> CockpitControl:
	return _hovered


func configure_button(index: int, label: String, callback: Callable,
		is_switch: bool = false, initial: bool = false) -> CockpitControl:
	var control := controls[index]
	control.label = label
	control.kind = CockpitControl.Kind.SWITCH if is_switch else CockpitControl.Kind.BUTTON
	control.on = initial
	control.enabled = true
	control.pressed_callback = callback
	control.set_caption("")
	if control.is_node_ready():
		control._caption.text = label
		control._refresh()
	return control


func set_indicator(name: String, lit: bool) -> void:
	if indicators.has(name):
		(indicators[name] as CockpitControl).set_on(lit)


func set_displays_visible(value: bool) -> void:
	for viewport in _viewports:
		viewport.render_target_update_mode = (SubViewport.UPDATE_ALWAYS if value
			else SubViewport.UPDATE_DISABLED)


func _box(size: Vector3, at: Vector3) -> MeshInstance3D:
	var node := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = size
	node.mesh = mesh
	node.position = at
	return node
