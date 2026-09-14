class_name StarfieldView
extends Node3D
## O campo de estrelas do Milestone 5, movido de arquivo e não reescrito
## (regra 34).
##
## 8786 estrelas reais do Yale BSC5, desenhadas como pontos, coloridas e
## apagadas por um shader que recebe o fator Doppler pronto e nunca fica sabendo
## o que é `β`. A aberração acontece na CPU, em
## `core/render/relativistic_sky.cpp`; este arquivo carrega um vetor de lá para
## cá e arrays de cá para uma malha.
##
## A malha é reconstruída a cada quadro porque é a CPU que aberra o céu. A
## alternativa -- passar `β` como uniforme e aberrar no vertex shader --
## pouparia o upload e moveria a física para GLSL; o custo medido da troca está
## em `docs/architecture/relativistic-shaders.md` seção 7.

## A esfera de estrelas fica dentro do plano distante da câmera (2e5) e para
## além do Sol a 1.47e5, de modo que o Sol ainda a oculta.
const SKY_RADIUS := 1.9e5

var sky: SpaceflightSky
var mesh_instance: MeshInstance3D
var array_mesh: ArrayMesh
var planck_texture: ImageTexture

var effect_aberration := true
var effect_doppler := true
var effect_beaming := true


func build(star_sky: SpaceflightSky) -> void:
	sky = star_sky
	mesh_instance = MeshInstance3D.new()
	array_mesh = ArrayMesh.new()
	mesh_instance.mesh = array_mesh

	var material := ShaderMaterial.new()
	material.shader = load("res://shaders/star_field.gdshader")
	if sky != null and sky.is_ready():
		# A tabela de cor sai de `core/render/blackbody.hpp` como uma imagem
		# RGBAF. Vizinho mais próximo criaria bandas visíveis no lugar do locus
		# de Planck; linear é o que `PlanckTable::sample_rgb` reproduz em C++, de
		# modo que os dois concordam.
		planck_texture = ImageTexture.create_from_image(sky.get_planck_table_image())
		material.set_shader_parameter("planck_table", planck_texture)
		material.set_shader_parameter("table_reference_temperature",
			sky.get_planck_table_reference_temperature())
		material.set_shader_parameter("half_saturation", sky.get_half_saturation())
	mesh_instance.material_override = material

	# O céu é reconstruído a cada quadro e o Godot não sabe a extensão de uma
	# malha vazia; sem isto o campo inteiro é descartado pelo frustum assim que a
	# câmera vira.
	mesh_instance.custom_aabb = AABB(Vector3.ONE * -SKY_RADIUS * 1.1,
		Vector3.ONE * SKY_RADIUS * 2.2)
	add_child(mesh_instance)


func update(beta: Vector3, exposure: float) -> void:
	if sky == null or not sky.is_ready():
		return

	# A única física que passa por este script: um vetor, transportado.
	sky.update_sky_effects(beta, SKY_RADIUS, effect_aberration, effect_doppler, effect_beaming)

	var surface := sky.get_surface_arrays()
	if surface.is_empty():
		return

	array_mesh.clear_surfaces()
	# A flag de formato é o que faz CUSTOM0 serem quatro FLOATS. Sem ela o Godot
	# empacota em quatro bytes, uma temperatura de 25944 K quantiza para 1.0,
	# todas as estrelas saem da mesma cor -- e nada, em lugar nenhum, reporta erro.
	array_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_POINTS, surface["arrays"],
		[], {}, surface["format"])

	if mesh_instance.material_override is ShaderMaterial:
		(mesh_instance.material_override as ShaderMaterial).set_shader_parameter(
			"half_saturation", exposure)
