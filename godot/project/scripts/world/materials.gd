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
