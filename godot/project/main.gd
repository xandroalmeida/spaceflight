extends Node3D
## Milestone 2: draw what the core computed.
##
## This script builds the entire scene at runtime so that the repository carries
## no hand-edited .tscn beyond a one-node root.  It owns cameras, meshes and
## labels -- and nothing else.  Every number on screen came out of a
## SimulationSnapshot; none of it was computed here.
##
## Controls:  , and .  change time warp     F  cycle focus     R  restart

const ALTITUDE_M := 400_000.0
const INCLINATION_DEG := 51.6
const EPOCH_UTC := "2026-01-01T00:00:00"

## Scene units per metre.  Not a precision knob -- see
## docs/architecture/rendering.md section 3.
## At 1e-6: Earth radius 6.371 units, ship at 6.771, Moon at 358, Sun at 147 000.
const RENDER_SCALE := 1.0e-6

## Bodies drawn larger than life.  1.0 means honest geometry, and that is the
## right default HERE: from a 400 km orbit the Earth already fills the sky, and
## any exaggeration puts the camera INSIDE the planet -- at 8x its drawn radius
## is 50.97 units while the ship sits 6.77 units from the centre. The knob exists
## for a Solar-System-wide view, where true-scale planets are invisible dots, and
## `B` cycles it at runtime.
const BODY_EXAGGERATION := 1.0
const EXAGGERATION_LEVELS := [1.0, 10.0, 100.0, 1000.0]

## Chase camera distance behind the ship, in scene units (0.2 = 200 km).
const CHASE_DISTANCE := 0.2
## The ship drawn at true scale would be 1e-5 units across -- invisible. 0.02 is
## 20 km: a deliberate lie, and the only one in the scene.
const SHIP_SIZE := 0.02

const WARP_LEVELS := [1.0, 10.0, 100.0, 1000.0, 10000.0, 100000.0]

var simulation: SpaceflightSimulation
var camera: Camera3D
var readout: Label
var hud_margin: MarginContainer
var body_meshes: Array[MeshInstance3D] = []
var ship_mesh: MeshInstance3D
var warp_index := 0
var exaggeration_index := 0
var _headless_seconds := 0.0
var focus_index := -1  ## -1 = the spacecraft


func _ready() -> void:
	simulation = SpaceflightSimulation.new()
	add_child(simulation)

	# The kernels live outside the Godot project, next to the core that reads
	# them.  res:// cannot leave the project directory, so globalize and walk up.
	var kernel_dir := ProjectSettings.globalize_path("res://").path_join("../../kernels/spice")
	kernel_dir = kernel_dir.simplify_path()

	if not simulation.configure(kernel_dir, EPOCH_UTC):
		push_error("Could not load SPICE kernels from %s -- run scripts/fetch_kernels.sh. %s"
			% [kernel_dir, simulation.get_last_error()])
		return

	if not simulation.start_circular_orbit(ALTITUDE_M, INCLINATION_DEG):
		push_error(simulation.get_last_error())
		return

	simulation.set_render_scale(RENDER_SCALE)
	simulation.set_body_scale_exaggeration(BODY_EXAGGERATION)
	simulation.set_time_warp(WARP_LEVELS[warp_index])

	_build_scene()


func _build_scene() -> void:
	var sun_light := DirectionalLight3D.new()
	sun_light.light_energy = 1.2
	sun_light.rotation_degrees = Vector3(-35.0, 40.0, 0.0)
	add_child(sun_light)

	var ambient := WorldEnvironment.new()
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.01, 0.012, 0.02)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.06, 0.07, 0.09)
	ambient.environment = environment
	add_child(ambient)

	_build_starfield()

	for i in range(simulation.get_body_count()):
		var mesh_instance := MeshInstance3D.new()
		var sphere := SphereMesh.new()
		sphere.radial_segments = 48
		sphere.rings = 24
		# SphereMesh defaults to radius 0.5 / height 1.0, so scaling the node by
		# `radius` would draw a body of HALF the right size. From a 400 km orbit
		# that is the difference between a planet filling the screen (70 deg of
		# angular radius) and one entirely outside the frame (28 deg, starting
		# 62 deg off-axis, against a 37.5 deg half-FOV) -- which is exactly the
		# black screen this scene showed the first time it was run.
		sphere.radius = 1.0
		sphere.height = 2.0
		mesh_instance.mesh = sphere

		var material := StandardMaterial3D.new()
		material.albedo_color = _colour_for(simulation.get_body_name(i))
		mesh_instance.material_override = material

		add_child(mesh_instance)
		body_meshes.append(mesh_instance)

	ship_mesh = MeshInstance3D.new()
	var ship_shape := BoxMesh.new()
	ship_shape.size = Vector3.ONE * SHIP_SIZE
	ship_mesh.mesh = ship_shape
	var ship_material := StandardMaterial3D.new()
	ship_material.albedo_color = Color(1.0, 0.85, 0.3)
	ship_material.emission_enabled = true
	ship_material.emission = Color(1.0, 0.85, 0.3)
	ship_material.emission_energy_multiplier = 0.6
	ship_mesh.material_override = ship_material
	add_child(ship_mesh)

	camera = Camera3D.new()
	# near 0.01 = 10 km; far 2e5 = 2e11 m = 1.3 au, which reaches past the Sun at
	# 147 000 units. Godot 4 uses a reverse-Z depth buffer, which survives this
	# ratio far better than the classic one would.
	camera.near = 0.01
	camera.far = 2.0e5
	camera.current = true
	add_child(camera)

	_build_hud()


func _build_hud() -> void:
	var layer := CanvasLayer.new()
	add_child(layer)

	hud_margin = MarginContainer.new()
	hud_margin.set_anchors_preset(Control.PRESET_TOP_LEFT)
	layer.add_child(hud_margin)

	var panel := PanelContainer.new()
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.0, 0.0, 0.0, 0.55)
	style.corner_radius_top_left = 6
	style.corner_radius_top_right = 6
	style.corner_radius_bottom_left = 6
	style.corner_radius_bottom_right = 6
	style.content_margin_left = 12
	style.content_margin_right = 12
	style.content_margin_top = 8
	style.content_margin_bottom = 8
	panel.add_theme_stylebox_override("panel", style)
	hud_margin.add_child(panel)

	readout = Label.new()
	# Monospaced on purpose: the readout is a table of aligned columns, and a
	# proportional font turns it into ragged prose. SystemFont picks the first
	# name the platform actually has.
	var font := SystemFont.new()
	font.font_names = PackedStringArray([
		"Menlo", "SF Mono", "Monaco", "Consolas", "DejaVu Sans Mono", "monospace"])
	readout.add_theme_font_override("font", font)
	readout.add_theme_color_override("font_color", Color(0.88, 0.92, 1.0))
	panel.add_child(readout)

	_scale_hud()
	get_viewport().size_changed.connect(_scale_hud)


func _scale_hud() -> void:
	## The HUD is sized from the viewport, not fixed in pixels: the same scene has
	## to be readable in a small embedded game window and on a 4K display.
	var height := get_viewport().get_visible_rect().size.y
	if height <= 0.0:
		height = 720.0

	var font_size := int(clampf(roundf(height / 30.0), 16.0, 34.0))
	readout.add_theme_font_size_override("font_size", font_size)

	var margin := int(maxf(roundf(height * 0.018), 8.0))
	for side in ["margin_left", "margin_top", "margin_right", "margin_bottom"]:
		hud_margin.add_theme_constant_override(side, margin)


func _build_starfield() -> void:
	## A crude fixed starfield: enough to tell that the camera is turning. Real
	## star positions are a Milestone 5 concern, together with everything else
	## that makes the sky physically honest.
	var stars := MeshInstance3D.new()
	var immediate := ImmediateMesh.new()
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.vertex_color_use_as_albedo = true
	material.albedo_color = Color.WHITE
	# PRIMITIVE_POINTS renders nothing at all without this.
	material.use_point_size = true
	material.point_size = 2.0
	material.disable_receive_shadows = true

	var rng := RandomNumberGenerator.new()
	rng.seed = 20260101
	immediate.surface_begin(Mesh.PRIMITIVE_POINTS, material)
	for i in range(2000):
		var direction := Vector3(rng.randfn(), rng.randfn(), rng.randfn()).normalized()
		var brightness := rng.randf_range(0.25, 1.0)
		immediate.surface_set_color(Color(brightness, brightness, brightness))
		# Inside the camera's far plane (2e5), and beyond the Sun at 1.47e5, so the
		# Sun still occludes them. The first version put the stars at 5e5 -- past
		# `far` -- and they were simply clipped away.
		immediate.surface_add_vertex(direction * 1.9e5)
	immediate.surface_end()

	stars.mesh = immediate
	stars.extra_cull_margin = 4.0e5
	add_child(stars)


func _colour_for(name: String) -> Color:
	match name:
		"Sun": return Color(1.0, 0.92, 0.6)
		"Earth": return Color(0.25, 0.45, 0.85)
		"Moon": return Color(0.72, 0.72, 0.70)
		"Mars Barycenter": return Color(0.80, 0.40, 0.28)
		"Venus Barycenter": return Color(0.90, 0.80, 0.55)
		"Jupiter Barycenter": return Color(0.85, 0.72, 0.55)
		_: return Color(0.6, 0.6, 0.65)


func _process(delta: float) -> void:
	if simulation == null or not simulation.is_ready():
		return

	# The frame rate decides how much coordinate time to ask for. It never
	# reaches the integrator, which picks its own steps (rule 21).
	simulation.advance(delta)

	# Floating origin: re-centre on whatever we are looking at, every frame.
	if focus_index < 0:
		simulation.focus_on_spacecraft()
	else:
		simulation.focus_on_body(focus_index)

	for i in range(body_meshes.size()):
		var mesh_instance := body_meshes[i]
		mesh_instance.position = simulation.get_body_position(i)
		var radius: float = simulation.get_body_radius(i)
		mesh_instance.visible = radius > 0.0
		if radius > 0.0:
			mesh_instance.scale = Vector3.ONE * radius

	ship_mesh.position = simulation.get_spacecraft_position()

	_place_camera()
	_update_readout()


func _warn_if_camera_is_inside_a_body() -> void:
	## A drawn radius larger than the distance to the body means the camera is
	## inside it, and the view becomes a wall of flat colour with no hint as to
	## why. Cheap to check, and it is exactly the bug that BODY_EXAGGERATION = 8
	## produced in the first version of this scene.
	for i in range(body_meshes.size()):
		var radius: float = simulation.get_body_radius(i)
		if radius <= 0.0:
			continue
		var distance: float = (body_meshes[i].position - camera.position).length()
		if radius > distance:
			push_warning("camera is inside %s: drawn radius %.2f > distance %.2f units (body scale %.0fx)"
				% [simulation.get_body_name(i), radius, distance, EXAGGERATION_LEVELS[exaggeration_index]])


func _place_camera() -> void:
	## Chase camera, in scene units. The projection origin is the focus itself, so
	## the camera sits a fraction of a unit from the origin and the float
	## precision stays microscopic (docs/architecture/rendering.md).
	##
	## Orientation matters more than distance here. From a 400 km orbit the Earth
	## subtends 140 degrees: pointing the camera along the velocity leaves the
	## planet starting 20 degrees off-axis and mostly out of frame. Offsetting the
	## camera OUTWARD (away from the planet) and using that outward direction as
	## "up" puts the planet in the lower part of the screen, where it belongs,
	## with the horizon visible above it.
	if focus_index >= 0:
		var body_target := body_meshes[focus_index].position
		var body_distance := maxf(simulation.get_body_radius(focus_index) * 3.0, 1.0)
		camera.position = body_target + Vector3(0.0, body_distance * 0.35, body_distance)
		camera.look_at(body_target, Vector3.UP)
		return

	var target := ship_mesh.position
	# "Down" is towards the reference body; the ship is the projection origin, so
	# the body's render position already IS the nadir direction.
	var nadir := _reference_body_position() - target
	var outward := (-nadir).normalized() if nadir.length() > 0.0 else Vector3.UP

	var back := -simulation.get_spacecraft_velocity_direction()
	if back.length() < 0.5:
		back = Vector3.BACK

	camera.position = target + outward * (CHASE_DISTANCE * 0.5) + back * CHASE_DISTANCE
	camera.look_at(target, outward)


func _reference_body_position() -> Vector3:
	var reference: String = simulation.get_snapshot().get("reference", "")
	for i in range(simulation.get_body_count()):
		if simulation.get_body_name(i) == reference:
			return simulation.get_body_position(i)
	return Vector3.ZERO


func _update_readout() -> void:
	var s := simulation.get_snapshot()
	if s.is_empty():
		return

	# GDScript's % operator supports %s %c %d %o %x %X %f %v %% -- and NOT %e or
	# %g. Scientific notation goes through String.num_scientific instead; the
	# first version of this script used %g and every frame threw a format error.
	var lines := [
		"t (TDB)        %+.3f s since J2000" % s["time_tdb_s"],
		"elapsed        %.6f s   warp %.0fx" % [s["elapsed_s"], s["time_warp"]],
		"proper time    %.6f s" % s["proper_time_s"],
		"clock diff     %s s" % String.num_scientific(s["clock_difference_s"]),
		"",
		"reference      %s" % s["reference"],
		"altitude       %.3f km" % (s["altitude_m"] / 1000.0),
		"speed          %.3f m/s" % s["speed_ms"],
		"acceleration   %.6f m/s^2" % s["acceleration_ms2"],
		"",
		"apoapsis       %.3f km" % (s["apoapsis_m"] / 1000.0),
		"periapsis      %.3f km" % (s["periapsis_m"] / 1000.0),
		"eccentricity   %.8f" % s["eccentricity"],
		"inclination    %.4f deg" % s["inclination_deg"],
		"period         %.2f s" % s["period_s"],
		"",
		"mass           %.1f kg" % s["mass_kg"],
		"target         %s at %.0f km, %.1f m/s" % [s["target"], s["target_distance_m"] / 1000.0, s["target_relative_speed_ms"]],
		"",
		"beta           %s" % String.num_scientific(s["beta"]),
		"gamma - 1      %s" % String.num_scientific(s["lorentz_factor_minus_one"]),
		"render res.    %s m per float ulp at %s" % [String.num_scientific(s["render_resolution_m"]), s["reference"]],
		"",
		"focus: %s   body scale %.0fx" % [("spacecraft" if focus_index < 0 else simulation.get_body_name(focus_index)), EXAGGERATION_LEVELS[exaggeration_index]],
		"(, . warp   F focus   B body scale   R restart)",
	]
	readout.text = "\n".join(lines)

	# Headless runs have no window: mirror the readout to stdout once a second so
	# that `--headless --quit-after N` is a real verification and not a silent
	# no-op. This is how Milestone 2 is checked on a machine with no display.
	if DisplayServer.get_name() == "headless":
		_headless_seconds += get_process_delta_time()
		if _headless_seconds >= 1.0:
			_headless_seconds = 0.0
			print("\n" + readout.text)


func _unhandled_key_input(event: InputEvent) -> void:
	if not (event is InputEventKey and event.pressed and not event.echo):
		return

	match (event as InputEventKey).keycode:
		KEY_PERIOD:
			warp_index = mini(warp_index + 1, WARP_LEVELS.size() - 1)
			simulation.set_time_warp(WARP_LEVELS[warp_index])
		KEY_COMMA:
			warp_index = maxi(warp_index - 1, 0)
			simulation.set_time_warp(WARP_LEVELS[warp_index])
		KEY_F:
			focus_index += 1
			if focus_index >= simulation.get_body_count():
				focus_index = -1
		KEY_B:
			exaggeration_index = (exaggeration_index + 1) % EXAGGERATION_LEVELS.size()
			simulation.set_body_scale_exaggeration(EXAGGERATION_LEVELS[exaggeration_index])
			_warn_if_camera_is_inside_a_body()
		KEY_R:
			simulation.start_circular_orbit(ALTITUDE_M, INCLINATION_DEG)
