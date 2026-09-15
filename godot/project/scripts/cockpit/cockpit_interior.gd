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
##     x = 4.58  ╭────────────────╮   linha da janela, no nariz chanfrado
##               │    JANELAS     │
##     x = 4.32  ├────────────────┤   quebra-luz
##               │FLIGHT NAV TARGET   painel principal, inclinado para o piloto
##               │ SYSTEM DISPLAY │
##               │ botões físicos │
##     x = 3.15  │       ●        │   olho do piloto
##     x = 2.30  ╰────────────────╯   anteparo traseiro, escotilha
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

## A linha da janela, vista de cima: quatro montantes, três vidraças, e os cantos
## são PARTILHADOS entre vidraças vizinhas.
##
##     y = +1.02   P0 ╮                  x = 4.10, contra a parede lateral
##                     ╲   lateral
##     y = +0.70   P1   ╯                x = 4.58, canto dianteiro
##                      │   frontal
##     y = −0.70   P2   ╮
##                     ╱   lateral
##     y = −1.02   P3 ╯
##
## ⚠️ A versão anterior punha três vidraças SOLTAS, cada uma com a sua moldura de
## quatro barras por cima do vidro. As barras de uma não encontravam as da outra,
## os topos estavam a alturas diferentes (0,89 à frente contra 0,78 aos lados) e
## a metade traseira das laterais ficava ENTERRADA na parede -- o que se via era
## um esqueleto de vigas desencontradas e, aos lados, dois trapézios de parede
## onde devia estar o espaço. Agora existe UMA linha, e tudo -- vidro, montante,
## testeira, peitoril, chão e teto -- é construído a partir dela. Mexer num
## número aqui move a cabine inteira, junta.
const CANOPY_ROOT_X := 4.10
const CANOPY_NOSE_X := 4.58
const CANOPY_NOSE_HALF_WIDTH := 0.70
const SILL_Z := 0.00
const HEAD_Z := 0.88

## A espessura do casco no vão da janela. Ela não é decoração: uma janela real é
## um furo num casco COM espessura, e é o rebaixo -- a parede do furo, que fica
## em sombra de um lado e iluminada do outro -- que dá a ler "janela". Uma barra
## colada à frente do vidro dá a ler "barra".
const REVEAL := 0.055
const REVEAL_DEPTH := 0.09
const SKIN := 0.10

## Resolução dos mostradores. Escolhida pela proporção física de cada painel,
## para que um pixel seja quadrado -- um mostrador esticado é a primeira coisa
## que se nota e a última que se procura.
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

## A planta da cabine, vista de cima: o retângulo pressurizado atrás e o nariz
## chanfrado à frente, terminando exatamente na linha da janela.
##
## Chão e teto são POLÍGONOS e não caixas. Com caixas, os cantos dianteiros
## sobravam para fora da linha da janela -- duas lajes a flutuar do lado de fora,
## visíveis pelas vidraças laterais -- e encurtá-las abria uma fresta de espaço
## entre o fim da parede e a testeira. Um polígono com a forma certa não tem nem
## uma coisa nem a outra.
static func _cabin_outline() -> PackedVector2Array:
	return PackedVector2Array([
		Vector2(AFT_X, CABIN_HALF_WIDTH),
		Vector2(CANOPY_ROOT_X, CABIN_HALF_WIDTH),
		Vector2(CANOPY_NOSE_X, CANOPY_NOSE_HALF_WIDTH),
		Vector2(CANOPY_NOSE_X, -CANOPY_NOSE_HALF_WIDTH),
		Vector2(CANOPY_ROOT_X, -CABIN_HALF_WIDTH),
		Vector2(AFT_X, -CABIN_HALF_WIDTH),
	])


func _build_shell() -> void:
	var outline := _cabin_outline()

	var floor_panel := _slab(outline, FLOOR_Z, true)
	floor_panel.material_override = materials.cabin_shell
	add_child(floor_panel)

	# O teto vai até a linha da janela e não até meio metro antes dela. Na
	# primeira captura havia um triângulo de espaço no canto superior esquerdo:
	# era o vão entre a borda dianteira do teto e a janela, e o viewport
	# transparente mostra o mundo em toda a parte onde não há geometria -- que é o
	# que faz a janela funcionar e o que faz um buraco no teto funcionar
	# igualmente bem.
	var ceiling := _slab(outline, CEILING_Z, false)
	ceiling.material_override = materials.cabin_shell
	add_child(ceiling)

	# As paredes acabam ONDE a janela começa (x = 4,10), e não meio metro à frente
	# disso. Quando passavam da linha, a metade traseira das vidraças laterais
	# dava para a própria parede: dois trapézios cinzentos a ocupar o lugar do
	# espaço, que foi o que a captura mostrou.
	for side in [1.0, -1.0]:
		var wall := _box(Vector3(CANOPY_ROOT_X - AFT_X, 0.06, CEILING_Z - FLOOR_Z),
			Vector3((CANOPY_ROOT_X + AFT_X) * 0.5, side * CABIN_HALF_WIDTH,
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


## Uma superfície plana e horizontal com a forma de `outline`, em leque a partir
## do primeiro vértice -- a planta é convexa, e para uma planta convexa o leque é
## a triangulação.
##
## As normais são escritas à mão em vez de deduzidas da ordem dos vértices: o
## material tem o cull desligado, então a ordem já não decide se a face existe, e
## deixá-la decidir a iluminação seria guardar a mesma armadilha num sítio onde
## ela não se vê -- um teto iluminado pela face de cima é um teto preto.
func _slab(outline: PackedVector2Array, z: float, facing_up: bool) -> MeshInstance3D:
	var vertices := PackedVector3Array()
	var normals := PackedVector3Array()
	var uvs := PackedVector2Array()
	var normal := Vector3(0.0, 0.0, 1.0 if facing_up else -1.0)
	for i in range(1, outline.size() - 1):
		for point in [outline[0], outline[i], outline[i + 1]]:
			vertices.append(Vector3(point.x, point.y, z))
			normals.append(normal)
			# Em metros do mundo: o ladrilho do chão alinha com o da parede, que é
			# o que se espera de um painel aparafusado a uma estrutura.
			uvs.append(point * 0.5)
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	var node := MeshInstance3D.new()
	node.mesh = mesh
	return node


# --- a janela ----------------------------------------------------------------

func _build_windows() -> void:
	## Três vãos entre quatro montantes, e os montantes ficam nos cantos
	## PARTILHADOS: é a diferença entre uma janela e um conjunto de barras.
	var line := [
		Vector2(CANOPY_ROOT_X, CABIN_HALF_WIDTH),
		Vector2(CANOPY_NOSE_X, CANOPY_NOSE_HALF_WIDTH),
		Vector2(CANOPY_NOSE_X, -CANOPY_NOSE_HALF_WIDTH),
		Vector2(CANOPY_ROOT_X, -CABIN_HALF_WIDTH),
	]
	var normals: Array[Vector2] = []
	for i in range(line.size() - 1):
		normals.append(_outward(line[i], line[i + 1]))
		_build_bay(line[i], line[i + 1])

	# Cada montante fica na BISSETRIZ dos dois vãos que ele separa -- é o que um
	# perfil esquadriado a unir duas chapas em ângulo faz, e é o que faz os dois
	# rebaixos encostarem nele sem deixar aresta solta. Nas pontas, onde só há um
	# vão, a bissetriz é a própria normal desse vão.
	for i in range(line.size()):
		var before: Vector2 = normals[maxi(i - 1, 0)]
		var after: Vector2 = normals[mini(i, normals.size() - 1)]
		_build_post(line[i], (before + after).normalized())

	_build_glareshield()


## A normal de um vão, virada para fora da cabine.
static func _outward(a: Vector2, b: Vector2) -> Vector2:
	var along := (b - a).normalized()
	return Vector2(-along.y, along.x)


## A base de um vão: Z local é a normal (para fora), Y local é o "cima" da
## cabine, X local corre ao longo do vão.
##
## As colunas são escritas por extenso porque três rotações de Euler compostas
## numa ordem que o Godot escolhe (YXZ) são um convite a uma janela virada para o
## lado -- e uma janela virada para o lado ainda parece uma janela.
static func _facing(normal: Vector2) -> Basis:
	return Basis(Vector3(-normal.y, normal.x, 0.0), Vector3(0.0, 0.0, 1.0),
		Vector3(normal.x, normal.y, 0.0))


func _build_bay(a: Vector2, b: Vector2) -> void:
	## Um vão: a casca acima e abaixo, o rebaixo do furo, a junta e o vidro. Tudo
	## em coordenadas do PRÓPRIO vão -- o vão frontal e os dois laterais correm o
	## mesmo código, e é por isso que os três acabam à mesma altura.
	var mid := (a + b) * 0.5
	var span := a.distance_to(b)
	var opening := Vector2(span, HEAD_Z - SILL_Z)
	var centre_z := (HEAD_Z + SILL_Z) * 0.5

	var bay := Node3D.new()
	bay.position = Vector3(mid.x, mid.y, 0.0)
	bay.basis = _facing(_outward(a, b))
	add_child(bay)

	# Testeira (entre o topo do vão e o teto) e peitoril (do vão até ao chão). São
	# a casca do nariz, e é ela que fecha a cabine: o viewport é transparente, e
	# onde não houver geometria vê-se o espaço -- inclusive por cima da cabeça.
	for band in [[HEAD_Z, CEILING_Z], [FLOOR_Z, SILL_Z]]:
		var skin := _box(Vector3(span, band[1] - band[0], SKIN),
			Vector3(0.0, (band[0] + band[1]) * 0.5, -SKIN * 0.5))
		skin.material_override = materials.cockpit_panel
		bay.add_child(skin)

	# A parede do furo, recuada para dentro: é o que um casco com espessura
	# mostra, e é a única peça daqui que produz uma face em sombra ao lado de uma
	# face iluminada -- que é o que o olho lê como profundidade.
	_ring(bay, opening, REVEAL, REVEAL_DEPTH, centre_z, 0.015 - REVEAL_DEPTH * 0.5,
		materials.frame_alloy)

	var pane := Vector2(span - REVEAL * 2.0, HEAD_Z - SILL_Z - REVEAL * 2.0)
	var glass := MeshInstance3D.new()
	var mesh := QuadMesh.new()
	mesh.size = pane
	glass.mesh = mesh
	glass.position = Vector3(0.0, centre_z, 0.010)
	glass.material_override = materials.glass
	bay.add_child(glass)

	# A junta, por cima da borda do vidro: 28 mm de elastómero preto entre o vidro
	# e o metal. É a peça mais pequena da cabine e a que mais decide se aquilo lê
	# como uma vidraça assente num casco ou como um retângulo pintado na moldura.
	_ring(bay, pane, 0.028, 0.018, centre_z, 0.016, materials.window_seal)

	_bolt_ring(bay, opening, centre_z)


## Os parafusos do aro de retenção, à volta do vão.
##
## Num `MultiMeshInstance3D` e não em oitenta `MeshInstance3D`: são a peça mais
## pequena e mais repetida da cabine, e oitenta nós seriam oitenta chamadas de
## desenho por quadro para desenhar uma coisa que o olho lê como uma textura.
##
## Valem o trabalho: é o detalhe que diz "isto foi montado". Uma vidraça sem nada
## à volta lê como um recorte; a mesma vidraça com uma carreira de cabeças de
## parafuso regulares lê como uma peça aparafusada a uma estrutura.
func _bolt_ring(parent: Node3D, opening: Vector2, centre_z: float) -> void:
	var inset := REVEAL * 0.5
	var half := Vector2(opening.x * 0.5 - inset, opening.y * 0.5 - inset)
	var spacing := 0.125
	var places: Array[Vector2] = []
	var across := int(round(opening.x / spacing))
	for i in range(across + 1):
		var x := -half.x + (half.x * 2.0) * float(i) / float(across)
		places.append(Vector2(x, half.y))
		places.append(Vector2(x, -half.y))
	var down := int(round(opening.y / spacing))
	for i in range(1, down):
		var y := -half.y + (half.y * 2.0) * float(i) / float(down)
		places.append(Vector2(half.x, y))
		places.append(Vector2(-half.x, y))

	var head := CylinderMesh.new()
	head.top_radius = 0.009
	head.bottom_radius = 0.011
	head.height = 0.010
	head.radial_segments = 8
	# O cilindro nasce ao longo do Y local; aqui ele tem de sair da face do aro na
	# direção do PILOTO, ou seja ao longo do −Z local do vão.
	#
	# ⚠️ A primeira versão pôs as cabeças na face de fora (+Z) e do lugar de quem
	# pilota não se via nada: um parafuso de doze milímetros visto de perfil, a um
	# metro e meio e contra a Terra, é zero pixels. Um aro de retenção aparafusa-se
	# por dentro -- que é também onde alguém lhe mexeria.
	var facing_in := Basis(Vector3(1.0, 0.0, 0.0), Vector3(0.0, 0.0, 1.0),
		Vector3(0.0, -1.0, 0.0))

	var multi := MultiMesh.new()
	multi.transform_format = MultiMesh.TRANSFORM_3D
	multi.mesh = head
	multi.instance_count = places.size()
	for i in range(places.size()):
		multi.set_instance_transform(i, Transform3D(facing_in,
			Vector3(places[i].x, centre_z + places[i].y, -REVEAL_DEPTH * 0.5 - 0.035)))

	var node := MultiMeshInstance3D.new()
	node.multimesh = multi
	node.material_override = materials.machined_trim
	parent.add_child(node)


## Quatro peças a formar um retângulo oco de `thickness` de espessura, no plano
## local de `parent`, com a abertura externa `opening`.
func _ring(parent: Node3D, opening: Vector2, thickness: float, depth: float,
		centre_z: float, at_z: float, material: Material) -> void:
	var inner := Vector2(opening.x - thickness * 2.0, opening.y - thickness * 2.0)
	for spec in [
			[Vector3(0.0, (opening.y - thickness) * 0.5, 0.0),
				Vector3(opening.x, thickness, depth)],
			[Vector3(0.0, -(opening.y - thickness) * 0.5, 0.0),
				Vector3(opening.x, thickness, depth)],
			[Vector3((opening.x - thickness) * 0.5, 0.0, 0.0),
				Vector3(thickness, inner.y, depth)],
			[Vector3(-(opening.x - thickness) * 0.5, 0.0, 0.0),
				Vector3(thickness, inner.y, depth)]]:
		var piece := _box(spec[1], spec[0] + Vector3(0.0, centre_z, at_z))
		piece.material_override = material
		parent.add_child(piece)


func _build_post(at: Vector2, outward: Vector2) -> void:
	## Um montante: o perfil estrutural entre dois vãos. Vai do peitoril à
	## testeira, com um centímetro de folga em cada ponta para não deixar uma
	## linha de luz onde as peças se tocam.
	var post := Node3D.new()
	post.position = Vector3(at.x, at.y, 0.0)
	post.basis = _facing(outward)
	add_child(post)

	var height := HEAD_Z - SILL_Z + 0.09
	var centre := (HEAD_Z + SILL_Z) * 0.5
	var shaft := _box(Vector3(0.085, height, 0.15), Vector3(0.0, centre, -0.055))
	shaft.material_override = materials.frame_alloy
	post.add_child(shaft)

	# Uma aresta viva virada para dentro da cabine. Uma caixa lisa iluminada de
	# frente lê como uma tábua -- não há nada na silhueta que diga de que material
	# ela é. Uma quina estreita e mais metálica apanha a luz de raspão numa linha
	# fina, e é essa linha que faz a peça ler como perfil maquinado.
	var edge := _box(Vector3(0.024, height, 0.024), Vector3(0.0, centre, -0.128))
	edge.material_override = materials.machined_trim
	post.add_child(edge)


func _build_glareshield() -> void:
	## O quebra-luz: a aba escura entre a janela e o painel. Impede que o painel
	## aceso apareça refletido no vidro, e é por isso que ela existe nas cápsulas
	## de verdade.
	##
	## ⚠️ A aba tem de ficar ABAIXO da linha de visão que vai do olho à borda de
	## baixo da vidraça frontal -- e "abaixo" é medido, não estimado. Uma versão
	## anterior punha uma placa inclinada com a borda traseira exatamente onde
	## está a fileira FLIGHT/NAV/TARGET, e o painel apareceu na captura como uma
	## tábua escura sem mostrador nenhum. Daqui (x = 4,32) aquela linha passa em
	## z = 0,09; a aba acaba em 0,06.
	var shield := _box(Vector3(0.32, CANOPY_NOSE_HALF_WIDTH * 2.4, 0.028),
		Vector3(4.32, 0.0, -0.03))
	shield.rotation_degrees = Vector3(0.0, -22.0, 0.0)
	shield.material_override = materials.dark_composite
	add_child(shield)

	# Um friso claro na aresta da frente. A faixa entre o peitoril e o painel é a
	# única superfície grande da cabine sem nada em cima dela, e sem uma aresta a
	# separar as duas ela lê como um vazio preto e não como uma peça. Uma linha
	# chega: é onde a luz do painel morre.
	var lip := _box(Vector3(0.022, CANOPY_NOSE_HALF_WIDTH * 2.4, 0.022),
		Vector3(4.465, 0.0, -0.088))
	lip.material_override = materials.machined_trim
	add_child(lip)


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
	# ⚠️ Quase branca, e não cor de lâmpada incandescente. A 1,00/0,88/0,72 -- um
	# âmbar de 2700 K -- e com energia 1,15, esta luz pintava de castanho-claro
	# tudo o que apanhava, e o que a captura mostrava eram montantes de MADEIRA.
	# A cor de uma luz interior não é um detalhe de ambiente: ela decide de que
	# material o jogador acha que a cabine é feita. Painéis de LED de cabine são
	# brancos, com um resto de calor, e é isso que está aqui.
	flood.light_color = Color(0.98, 0.96, 0.93)
	flood.light_energy = 0.75
	flood.omni_range = 3.6
	add_child(flood)

	var panel_light := OmniLight3D.new()
	# Debaixo do quebra-luz e apontada para o painel, como nas cápsulas de
	# verdade. Alcance largo e energia baixa: uma luz forte e próxima produz um
	# halo no meio do painel, que foi o que a primeira captura mostrou.
	panel_light.position = Vector3(CANOPY_NOSE_X - 0.24, 0.0, 0.02)
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
