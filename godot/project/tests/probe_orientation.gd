extends SceneTree
## Onde está o ponto subsolar, segundo `get_body_orientation()`.
##
## É o teste externo da orientação: às 00:00 UTC o meio-dia solar está no
## antimeridiano, porque o meio-dia em Greenwich é às 12:00 UTC. Se a matriz
## estiver transposta, a resposta sai perto de 0 em vez de perto de 180.

func _initialize() -> void:
	var sim := SpaceflightSimulation.new()
	root.add_child(sim)
	var k := ProjectSettings.globalize_path("res://").path_join("../../kernels/spice")
	if not sim.configure(k.simplify_path(), "2026-01-01T00:00:00"):
		printerr("SKIP: ", sim.get_last_error())
		quit(77)
		return
	sim.start_circular_orbit(400_000.0, 51.6)
	sim.set_render_scale(1.0e-6)
	sim.advance(0.016)

	var earth := -1
	var sun := -1
	for i in range(sim.get_body_count()):
		if sim.get_body_name(i) == "Earth": earth = i
		if sim.get_body_name(i) == "Sun": sun = i

	var to_sun := (sim.get_body_position(sun) - sim.get_body_position(earth)).normalized()
	var basis := sim.get_body_orientation(earth)
	# As colunas são os eixos do corpo em J2000, então a transposta leva um vetor
	# de J2000 para o referencial do corpo.
	var body := basis.transposed() * to_sun
	var longitude := rad_to_deg(atan2(body.y, body.x))
	var latitude := rad_to_deg(asin(clampf(body.z, -1.0, 1.0)))
	print("sub-solar  longitude %+8.2f deg east   latitude %+7.2f deg" % [longitude, latitude])
	print("expected   ~180 east (00:00 UTC), ~-23 lat (January)")

	# E agora a cadeia inteira, incluindo a convenção de UV da malha: em que
	# ponto da TEXTURA cai o ponto subsolar? Às 00:00 UTC tem de cair no
	# Pacífico -- azul escuro --, e o antissolar na Europa ou em África.
	var mesh := CelestialView._mesh_basis(basis)
	var albedo := Image.load_from_file(
		ProjectSettings.globalize_path("res://assets/textures/earth/earth_albedo.jpg"))
	if albedo == null:
		print("(no albedo texture to sample)")
		quit(0)
		return
	for entry in [["sub-solar ", to_sun], ["anti-solar", -to_sun]]:
		var local: Vector3 = mesh.transposed() * (entry[1] as Vector3)
		var u := fposmod(atan2(local.x, local.z) / TAU, 1.0)
		var v := acos(clampf(local.y, -1.0, 1.0)) / PI
		var px := albedo.get_pixel(
			clampi(int(u * albedo.get_width()), 0, albedo.get_width() - 1),
			clampi(int(v * albedo.get_height()), 0, albedo.get_height() - 1))
		var ocean := px.b > px.r + 0.07 and px.b > px.g + 0.03 and px.b < 0.6
		print("%s uv %.3f, %.3f   rgb %.2f %.2f %.2f   %s"
			% [entry[0], u, v, px.r, px.g, px.b, "ocean" if ocean else "land"])
	quit(0)
