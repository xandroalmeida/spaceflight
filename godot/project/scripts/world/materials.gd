class_name ShipMaterials
extends RefCounted
## Os materiais da nave e do cockpit, criados uma vez e compartilhados (regra 50).
##
## Compartilhados de verdade: um `StandardMaterial3D` por objeto multiplicaria
## por trinta o número de pipelines que o renderizador tem de trocar, e não há
## nenhum objeto aqui cuja aparência dependa de qual objeto ele é. Quando um
## material precisar variar -- a pluma, que muda com o empuxo -- ele é único por
## construção e está em outro arquivo.
##
## A escolha estética inteira está neste arquivo: cinza-grafite, alumínio nu,
## compósito escuro, manta térmica dourada, radiador branco. Aeroespacial
## moderno, industrial, sem néon (regra 71).

var hull_paint: StandardMaterial3D
var bare_metal: StandardMaterial3D
var dark_composite: StandardMaterial3D
var glass: StandardMaterial3D
var display_glass: StandardMaterial3D
var cockpit_panel: StandardMaterial3D
var cabin_shell: StandardMaterial3D
var frame_alloy: StandardMaterial3D
var machined_trim: StandardMaterial3D
var window_seal: StandardMaterial3D
var radiator: StandardMaterial3D
var thermal_blanket: StandardMaterial3D
var engine_bell: StandardMaterial3D
var solar_cell: StandardMaterial3D
var indicator_off: StandardMaterial3D


## Uma textura, se ela existir no caminho esperado; `null` se não (regra 81).
##
## `uv1_scale` é quantas vezes o ladrilho cabe na peça, e é por material e não
## por malha: o casco pressurizado tem 9,4 m de circunferência e os tanques
## 10,7 m, então um número serve os dois a menos de 12 % de densidade de texel.
## Um material por peça daria densidade exata e trinta pipelines em vez de um --
## a troca está feita do lado da regra 50, e a consequência é esta.
static func _texture(path: String) -> Texture2D:
	if not ResourceLoader.exists(path):
		return null
	var loaded := load(path)
	return loaded if loaded is Texture2D else null


static func _apply(material: StandardMaterial3D, path: String, scale: Vector3) -> void:
	var texture := _texture(path)
	if texture == null:
		return
	material.albedo_texture = texture
	material.uv1_scale = scale
	# O albedo fica branco: a cor passa a vir da textura, e multiplicar as duas
	# escureceria a peça em relação ao que a imagem mostra.
	material.albedo_color = Color.WHITE


func _init() -> void:
	hull_paint = _pbr(Color(0.78, 0.79, 0.80), 0.55, 0.0)
	_apply(hull_paint, "res://assets/textures/spacecraft/hull_panels.png",
		Vector3(5.0, 4.0, 1.0))
	bare_metal = _pbr(Color(0.62, 0.64, 0.67), 0.28, 1.0)
	dark_composite = _pbr(Color(0.13, 0.14, 0.16), 0.75, 0.0)
	# Dourada e rugosa: a manta multicamadas é a superfície que mais diz "isto
	# opera no vácuo" em uma imagem, e é a única cor saturada na nave inteira.
	thermal_blanket = _pbr(Color(0.86, 0.68, 0.26), 0.62, 0.85)
	# O radiador é quase branco e um pouco emissivo, porque é o que ele faz:
	# rejeitar calor. A emissão é pequena -- 0.08 -- para não virar uma lâmpada.
	radiator = _pbr(Color(0.90, 0.91, 0.92), 0.40, 0.0)
	radiator.emission_enabled = true
	# 0,04 e não 0,08: a 0,08 os painéis eram a coisa mais brilhante da nave no
	# lado escuro e liam-se como dois retângulos castanhos a flutuar. Um radiador
	# a rejeitar calor emite no infravermelho, que não se vê; o que se desenha
	# aqui é uma sugestão, e uma sugestão não pode dominar a imagem.
	radiator.emission = Color(0.42, 0.26, 0.20)
	radiator.emission_energy_multiplier = 0.04
	# O sino do motor: nióbio escurecido pelo uso, muito metálico e liso o
	# bastante para pegar o brilho do Sol na borda.
	engine_bell = _pbr(Color(0.34, 0.30, 0.29), 0.34, 1.0)
	solar_cell = _pbr(Color(0.08, 0.10, 0.20), 0.25, 0.4)

	# O vidro tem de parecer vidro E deixar pilotar (regra 52). Transparência
	# alta, quase sem tinta, e uma especularidade discreta -- nada de azul forte
	# nem de reflexo de ambiente, que é o que costuma tornar uma janela de
	# simulador inútil exatamente quando há algo lá fora para ver.
	glass = StandardMaterial3D.new()
	glass.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	glass.albedo_color = Color(0.72, 0.78, 0.82, 0.032)
	glass.roughness = 0.03
	glass.metallic = 0.0
	glass.cull_mode = BaseMaterial3D.CULL_DISABLED
	glass.shading_mode = BaseMaterial3D.SHADING_MODE_PER_PIXEL

	# O vidro dos displays é o mesmo vidro mais escuro, para que um display
	# apagado leia como apagado e não como uma superfície branca.
	display_glass = _pbr(Color(0.03, 0.035, 0.04), 0.12, 0.0)

	# Rugosidade 0,92 e não 0,72: a 0,72 a luz do painel deixava um halo especular
	# redondo e brilhante no meio da superfície, que lê como um defeito de
	# renderização. Uma superfície de cockpit é fosca.
	cockpit_panel = _pbr(Palette.PANEL, 0.92, 0.0)
	_apply(cockpit_panel, "res://assets/textures/cockpit/panel_surface.png",
		Vector3(2.0, 2.0, 1.0))

	# O chão e o teto da cabine são polígonos de uma face só -- o nariz é
	# chanfrado e uma caixa não o é. Sem cull, a ordem dos vértices deixa de
	# decidir se a superfície existe, e as coordenadas de textura são calculadas
	# em metros do mundo, então a escala do material fica em 1.
	cabin_shell = _pbr(Palette.PANEL, 0.92, 0.0)
	cabin_shell.cull_mode = BaseMaterial3D.CULL_DISABLED
	_apply(cabin_shell, "res://assets/textures/cockpit/panel_surface.png",
		Vector3(1.0, 1.0, 1.0))

	# A estrutura da janela: alumínio maquinado, cinzento-frio, metálico o
	# bastante para que uma aresta apanhe luz e a face ao lado dela não. É o que
	# separa "uma moldura" de "uma tábua" -- a mesma caixa, num material difuso e
	# claro sob uma luz quente, lê como madeira, e foi exatamente isso que a
	# primeira cabine mostrou.
	#
	# Escuro e pouco polido de propósito. A 0,40 de albedo com 0,38 de rugosidade
	# os montantes ficavam a coisa mais clara da cabine e liam-se como caixilho de
	# alumínio polido -- e um montante de janela de nave é pintado de cinzento
	# fosco justamente para NÃO devolver luz aos olhos de quem pilota.
	frame_alloy = _pbr(Color(0.27, 0.285, 0.30), 0.50, 0.80)

	# As arestas e os parafusos do aro de retenção.
	#
	# ⚠️ Metálico 0,35 e NÃO 1,0. Com metálico 1,0 -- que é o que `bare_metal` é --
	# a superfície não tem componente difusa nenhuma: tudo o que ela mostra é o
	# que reflete, e aqui não há sonda de reflexão nem céu, só uma cor ambiente a
	# 0,07. O resultado foi uma quina que era PRETA em toda a parte menos onde o
	# lóbulo especular da luz batia, e aí era branca estourada: uma tira de crómio
	# a cortar a janela ao meio. Uma peça pequena precisa de difusa para se ler.
	machined_trim = _pbr(Color(0.46, 0.47, 0.49), 0.45, 0.35)

	# A junta entre o vidro e a moldura. Preta e fosca: sem ela a vidraça lê como
	# um retângulo desenhado NA moldura, em vez de uma peça assente nela.
	window_seal = _pbr(Color(0.045, 0.047, 0.052), 0.95, 0.0)

	indicator_off = _pbr(Color(0.10, 0.11, 0.12), 0.5, 0.0)


func _pbr(albedo: Color, roughness: float, metallic: float) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = albedo
	material.roughness = roughness
	material.metallic = metallic
	return material


## Um material auto-luminoso para indicadores e letreiros: a cor é o estado.
static func emissive(colour: Color, energy: float = 1.6) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = colour
	material.emission_enabled = true
	material.emission = colour
	material.emission_energy_multiplier = energy
	material.roughness = 0.5
	return material


## Aditivo e sem escrita de profundidade: o que uma chama é. Usado pela pluma do
## motor e pelos jatos de RCS.
static func additive(colour: Color) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = colour
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	material.no_depth_test = false
	material.disable_receive_shadows = true
	return material
