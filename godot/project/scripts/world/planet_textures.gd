class_name PlanetTextures
extends RefCounted
## Texturas de planeta PROCEDURAIS, como substituto (regras 40-A, 46, 81).
##
## Não são a Terra. São ruído 3D amostrado na direção da esfera -- sem costura e
## sem beliscar os polos, que é a razão de ser 3D e não uma imagem
## equirretangular ruidosa -- fatiado em oceano, terra, deserto e gelo por
## latitude e altura. O resultado lê como um planeta a 400 km de distância e não
## sobrevive a um zoom.
##
## O definitivo vem de fora: os prompts estão em `docs/assets/planets/` e o
## manifesto diz onde cada arquivo tem de cair. Quando o arquivo existir,
## `CelestialView` o carrega e este gerador deixa de ser chamado -- sem mudar
## nenhuma outra linha, que é o ponto da regra 81.
##
## Elas são desenhadas UMA vez e guardadas em `user://`, porque meio milhão de
## pixels em GDScript custa cerca de um segundo e a regra 66 dá "alguns
## segundos" para o jogo inteiro aparecer.

const WIDTH := 512
const HEIGHT := 256
const CACHE_VERSION := 1     ## suba isto para invalidar o cache ao mudar o gerador


static func earth_albedo() -> Texture2D:
	return _cached("earth_albedo", func() -> Image: return _draw_earth_albedo())


static func earth_clouds() -> Texture2D:
	return _cached("earth_clouds", func() -> Image: return _draw_earth_clouds())


static func earth_night() -> Texture2D:
	return _cached("earth_night", func() -> Image: return _draw_earth_night())


static func moon_albedo() -> Texture2D:
	return _cached("moon_albedo", func() -> Image: return _draw_moon_albedo())


static func mars_albedo() -> Texture2D:
	return _cached("mars_albedo", func() -> Image: return _draw_mars_albedo())


# --- cache -------------------------------------------------------------------

static func _cached(name: String, generator: Callable) -> Texture2D:
	var path := "user://placeholder_%s_v%d.png" % [name, CACHE_VERSION]
	if FileAccess.file_exists(path):
		var loaded := Image.load_from_file(path)
		if loaded != null and loaded.get_width() == WIDTH:
			loaded.generate_mipmaps()
			return ImageTexture.create_from_image(loaded)
	var image: Image = generator.call()
	image.save_png(path)
	image.generate_mipmaps()
	return ImageTexture.create_from_image(image)


# --- geradores ---------------------------------------------------------------

static func _noise(seed_value: int, frequency: float, octaves: int) -> FastNoiseLite:
	var noise := FastNoiseLite.new()
	noise.seed = seed_value
	noise.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	noise.frequency = frequency
	noise.fractal_type = FastNoiseLite.FRACTAL_FBM
	noise.fractal_octaves = octaves
	return noise


static func _direction(x: int, y: int) -> Vector3:
	## Pixel equirretangular -> direção unitária. A convenção de UV de
	## `SphereMesh` é u a dar a volta e v a descer do polo norte, e é ela que tem
	## de ser invertida aqui: uma amostragem que não bate com a malha põe a Europa
	## no Pacífico e nada acusa.
	var u := (float(x) + 0.5) / float(WIDTH)
	var v := (float(y) + 0.5) / float(HEIGHT)
	var longitude := u * TAU
	var latitude := (0.5 - v) * PI
	var cos_lat := cos(latitude)
	return Vector3(cos_lat * cos(longitude), sin(latitude), cos_lat * sin(longitude))


static func _draw_earth_albedo() -> Image:
	var image := Image.create(WIDTH, HEIGHT, false, Image.FORMAT_RGB8)
	var continents := _noise(1337, 1.15, 5)
	var detail := _noise(4242, 3.6, 4)

	var deep := Color(0.024, 0.055, 0.125)
	var shallow := Color(0.055, 0.145, 0.255)
	var beach := Color(0.52, 0.47, 0.33)
	var grass := Color(0.145, 0.30, 0.145)
	var forest := Color(0.075, 0.195, 0.105)
	var desert := Color(0.60, 0.50, 0.31)
	var ice := Color(0.90, 0.92, 0.95)

	for y in range(HEIGHT):
		var latitude := (0.5 - (float(y) + 0.5) / float(HEIGHT)) * PI
		var abs_lat := absf(latitude) / (PI * 0.5)
		for x in range(WIDTH):
			var direction := _direction(x, y)
			var height := continents.get_noise_3dv(direction * 2.0)
			height += 0.35 * detail.get_noise_3dv(direction * 2.0)
			# O degrau terra/mar em 0.02 dá cerca de 30 % de terra, que é a
			# fração da Terra. Não é uma coincidência escolhida: é o único
			# número neste arquivo que tem um alvo.
			var colour: Color
			if height < 0.0:
				colour = deep.lerp(shallow, clampf((height + 0.35) / 0.35, 0.0, 1.0))
			elif height < 0.035:
				colour = beach
			else:
				var dryness := clampf(0.5 + 0.5 * detail.get_noise_3dv(direction * 5.0), 0.0, 1.0)
				# Desertos nas latitudes de célula de Hadley, ~15 a 35 graus.
				var hadley := 1.0 - absf(abs_lat - 0.28) * 3.4
				colour = forest.lerp(grass, dryness)
				colour = colour.lerp(desert, clampf(hadley, 0.0, 1.0) * dryness)

			# Calotas polares, com a borda quebrada pelo mesmo ruído para não
			# virar uma faixa retangular.
			var ice_edge := 0.80 + 0.10 * detail.get_noise_3dv(direction * 4.0)
			if abs_lat > ice_edge:
				colour = colour.lerp(ice, clampf((abs_lat - ice_edge) / 0.12, 0.0, 1.0))
			image.set_pixel(x, y, colour)
	return image


static func _draw_earth_clouds() -> Image:
	var image := Image.create(WIDTH, HEIGHT, false, Image.FORMAT_RGB8)
	var bands := _noise(909, 1.9, 5)
	var wisps := _noise(5150, 5.5, 3)
	for y in range(HEIGHT):
		var latitude := (0.5 - (float(y) + 0.5) / float(HEIGHT)) * PI
		var abs_lat := absf(latitude) / (PI * 0.5)
		# Zona de convergência no equador, cinturão seco nos subtrópicos, frentes
		# nas latitudes médias. Três faixas, que é o que se vê de órbita.
		var climate := 0.55 * exp(-pow(abs_lat / 0.12, 2.0))
		climate += 0.50 * exp(-pow((abs_lat - 0.62) / 0.22, 2.0))
		climate += 0.20
		climate -= 0.35 * exp(-pow((abs_lat - 0.30) / 0.13, 2.0))
		for x in range(WIDTH):
			var direction := _direction(x, y)
			var value := 0.5 + 0.5 * bands.get_noise_3dv(direction * 2.0)
			value = value * 0.75 + 0.25 * (0.5 + 0.5 * wisps.get_noise_3dv(direction * 2.0))
			var cover := clampf((value + climate - 0.92) * 3.4, 0.0, 1.0)
			image.set_pixel(x, y, Color(cover, cover, cover))
	return image


static func _draw_earth_night() -> Image:
	var image := Image.create(WIDTH, HEIGHT, false, Image.FORMAT_RGB8)
	var continents := _noise(1337, 1.15, 5)
	var detail := _noise(4242, 3.6, 4)
	var settlement := _noise(77, 14.0, 2)
	# Âmbar-sódio e não branco: é a cor que a Terra tem de noite, e a diferença é
	# metade do que faz a imagem ser reconhecível.
	var lamp := Color(1.0, 0.72, 0.36)
	for y in range(HEIGHT):
		var latitude := (0.5 - (float(y) + 0.5) / float(HEIGHT)) * PI
		var abs_lat := absf(latitude) / (PI * 0.5)
		for x in range(WIDTH):
			var direction := _direction(x, y)
			var height := continents.get_noise_3dv(direction * 2.0) \
				+ 0.35 * detail.get_noise_3dv(direction * 2.0)
			if height < 0.035 or abs_lat > 0.78:
				image.set_pixel(x, y, Color.BLACK)
				continue
			# Luzes onde há gente: aglomeradas, não uniformes, e mais fortes
			# perto da costa -- que é onde elas estão.
			var density := 0.5 + 0.5 * settlement.get_noise_3dv(direction * 2.0)
			var coastal := clampf(1.0 - absf(height - 0.10) * 6.0, 0.0, 1.0)
			var brightness := clampf((density - 0.62) * 5.0, 0.0, 1.0) * (0.35 + 0.65 * coastal)
			image.set_pixel(x, y, lamp * brightness)
	return image


static func _draw_moon_albedo() -> Image:
	var image := Image.create(WIDTH, HEIGHT, false, Image.FORMAT_RGB8)
	var craters := FastNoiseLite.new()
	craters.seed = 2024
	craters.noise_type = FastNoiseLite.TYPE_CELLULAR
	craters.cellular_return_type = FastNoiseLite.RETURN_DISTANCE2_SUB
	craters.frequency = 3.0
	var maria := _noise(31415, 0.9, 3)
	var dust := _noise(2718, 9.0, 3)

	var highland := Color(0.62, 0.61, 0.59)
	var mare := Color(0.27, 0.27, 0.28)
	for y in range(HEIGHT):
		for x in range(WIDTH):
			var direction := _direction(x, y)
			# Os mares são grandes e escuros e só existem de um lado; o ruído
			# de baixa frequência dá exatamente essa distribuição desigual.
			var basalt := clampf((maria.get_noise_3dv(direction * 2.0) - 0.08) * 4.0, 0.0, 1.0)
			var colour := highland.lerp(mare, basalt)
			# Crateras: o ruído celular dá bordas claras e fundos escuros, que é
			# a leitura de uma cratera a esta resolução.
			var rim := craters.get_noise_3dv(direction * 2.0)
			colour = colour.lerp(Color(0.80, 0.79, 0.77), clampf(rim * 1.4, 0.0, 1.0) * 0.5)
			colour = colour.darkened(clampf(-rim * 0.9, 0.0, 1.0) * 0.35)
			colour = colour.lerp(colour.darkened(0.12),
				0.5 + 0.5 * dust.get_noise_3dv(direction * 2.0))
			image.set_pixel(x, y, colour)
	return image


static func _draw_mars_albedo() -> Image:
	## Marte, como substituto (regras 22, 68, 90).
	##
	## O que isto NÃO é: um mapa de Marte. Não há Valles Marineris, não há Olympus
	## Mons e as manchas escuras não são Syrtis Major -- são ruído. O que ele
	## acerta são as três coisas que fazem um planeta ser reconhecido a distância:
	## a cor (óxido de ferro, laranja-acastanhado, não vermelho de desenho), o
	## CONTRASTE entre planícies claras e regiões de albedo escuro, e as calotas
	## polares, que são a assinatura de Marte num disco pequeno.
	##
	## A definitiva vem de fora (`docs/assets/planets/mars-albedo-codex-prompt.md`)
	## e cai em `assets/textures/mars/mars_albedo.png`. Quando existir, o
	## `CelestialView` a carrega e este gerador deixa de ser chamado.
	var image := Image.create(WIDTH, HEIGHT, false, Image.FORMAT_RGB8)
	var regions := _noise(4995, 1.05, 4)
	var dust := _noise(1877, 3.2, 4)
	var grit := _noise(6060, 11.0, 2)

	# Três tons medidos a olho contra imagens do Viking/MOC, não escolhidos por
	# gosto: a poeira clara, a rocha basáltica escura das regiões de albedo, e o
	# gelo das calotas, que é CO2 e não é branco puro.
	var bright_dust := Color(0.72, 0.46, 0.29)
	var ochre := Color(0.58, 0.34, 0.20)
	var dark_terrain := Color(0.33, 0.21, 0.145)
	var polar_ice := Color(0.92, 0.90, 0.88)

	for y in range(HEIGHT):
		var latitude := (0.5 - (float(y) + 0.5) / float(HEIGHT)) * PI
		var abs_lat := absf(latitude) / (PI * 0.5)
		for x in range(WIDTH):
			var direction := _direction(x, y)
			var region := regions.get_noise_3dv(direction * 2.0)
			# As regiões escuras de Marte cobrem cerca de um quarto do disco e
			# concentram-se no hemisfério sul. O degrau e o viés de latitude
			# produzem essa fração e essa assimetria.
			var darkness := clampf((region + 0.10 - 0.22 * latitude / (PI * 0.5)) * 2.2, 0.0, 1.0)
			var colour := bright_dust.lerp(ochre, clampf(0.5 + 0.5 * region, 0.0, 1.0))
			colour = colour.lerp(dark_terrain, darkness)
			# Poeira: as tempestades deixam o planeta manchado em escalas de
			# centenas de quilômetros, e é isso que quebra a leitura de "esfera
			# pintada".
			colour = colour.lerp(bright_dust,
				clampf(dust.get_noise_3dv(direction * 2.0) * 0.55, 0.0, 1.0))
			colour = colour.lerp(colour.darkened(0.10),
				0.5 + 0.5 * grit.get_noise_3dv(direction * 2.0))

			# As calotas. A do sul é menor e a borda é irregular, pela mesma razão
			# que a da Terra: uma faixa retangular lê-se como erro de textura.
			var edge := (0.86 if latitude > 0.0 else 0.90) \
				+ 0.05 * dust.get_noise_3dv(direction * 4.0)
			if abs_lat > edge:
				colour = colour.lerp(polar_ice, clampf((abs_lat - edge) / 0.09, 0.0, 1.0))
			image.set_pixel(x, y, colour)
	return image
