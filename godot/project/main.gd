extends Node3D
## Milestone 2: draw what the core computed.
##
## This script builds the entire scene at runtime so that the repository carries
## no hand-edited .tscn beyond a one-node root.  It owns cameras, meshes and
## labels -- and nothing else.  Every number on screen came out of a
## SimulationSnapshot; none of it was computed here.
##
## Milestone 5 added the relativistic optics.  Note what this script does NOT do:
## there is no gamma, no sqrt(1 - b*b), no pow(D, 4) anywhere below.  It carries
## a velocity from the simulation to the sky and arrays from the sky to a mesh,
## and every number it passes was computed in core/ (ADR-0002,
## docs/architecture/relativistic-shaders.md section 6).
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

## A cruise burn to relativistic speed lasts eight YEARS -- 2.5e8 s. The ladder
## goes up to 1e8 so that a human can watch it happen; the propagator does not
## care, because the warp only decides how much coordinate time is asked for per
## frame and never the integration step (rule 21).
const WARP_LEVELS := [1.0, 10.0, 100.0, 1000.0, 10000.0, 100000.0, 1.0e6, 1.0e7, 1.0e8]

## The star sphere sits inside the camera's far plane (2e5) and beyond the Sun at
## 1.47e5, so the Sun still occludes it.
const SKY_RADIUS := 1.9e5

## BSC5 reaches V = 7.96, two magnitudes past the naked eye. Keeping all of it
## costs nothing and the faint stars are what make the aberration legible: they
## are the ones that sweep.
const MAGNITUDE_LIMIT := 7.96

## Exposure, in the sense of core/render/tone_response.hpp: the flux that reads
## half scale. The default puts a magnitude 2 star at mid scale; E and Q step it
## so that the aft sky can be hunted for after it goes out.
const EXPOSURE_LEVELS := [0.0158, 0.0501, 0.1585, 0.5012, 1.5849]

## Two different camera controls, because they answer two different questions.
##
##   ORBIT   moves the camera AROUND the focus, which keeps the subject centred
##           and shows it -- and the sky behind it -- from any side. This is the
##           one you want almost always, and it is what WASD and the mouse do.
##   LOOK    turns the camera in place, leaving the subject behind. Held under
##           Shift, because a view that wanders off the thing you are flying is
##           the exception, not the default.
##
## WASD and not IJKL because `L` is already the light-time toggle, and `L` for
## light is worth more than `L` for a camera axis.
const ORBIT_SPEED_KEY := 1.4         ## rad/s while a key is held
const ORBIT_SPEED_MOUSE := 0.006     ## rad per pixel of mouse travel
const LOOK_SPEED_KEY := 1.4
const LOOK_SPEED_MOUSE := 0.0035

## Elevation and pitch stop just short of vertical. These are INPUT limits on a
## camera angle, not physical bounds -- the distinction rule 13 is about. Past 90
## degrees the up vector flips and the controls invert under the player's hands,
## which is a usability failure and not a statement about the world.
const LOOK_PITCH_LIMIT := 1.5533     ## 89 degrees

## Zoom, as a multiplier on the focus's natural distance, so the same range works
## for a 20 m ship and for Jupiter.
const ZOOM_STEP := 1.15
const ZOOM_MIN := 0.15
const ZOOM_MAX := 40.0

## Where the camera SITS before the free-look offsets are added, as an azimuth
## and elevation on a sphere around the focus. Azimuth 0 puts the camera on the
## frame's forward axis (so it looks BACK along it) and 180 puts it behind (so it
## looks FORWARD along it).
## Each mode carries its own FRAME, and that is the whole point of there being
## modes at all:
##
##   ship       the hull's own axes. Azimuth 180 is directly behind the tail and 0
##              is nose-on, so orbiting goes round the ship the way a hand turns
##              an object. This is what you want to look AT the ship.
##   prograde   the velocity axis, outward radial as up. Azimuth 180 puts the
##              camera behind, looking into the forward cone.
##   retrograde the same frame, azimuth 0: ahead of the ship, looking down the sky
##              that went dark.
##
## The first version used the velocity frame for all three, and it could not get
## behind the ship at all: with the hull 70 degrees off prograde, sweeping azimuth
## traced a circle that reached 138 degrees from the nose and stopped -- a rear
## quarter, never the tail. Measured, not guessed.
const LOOK_MODES := ["ship", "prograde", "retrograde"]
const LOOK_MODE_AZIMUTH := [PI, PI, 0.0]
const LOOK_MODE_ELEVATION := [0.3491, 0.0, 0.0]   ## 20 deg above the hull, 0 for the locks

var simulation: SpaceflightSimulation
var camera: Camera3D
var readout: Label
var hud_margin: MarginContainer
var body_meshes: Array[MeshInstance3D] = []
var ship_mesh: MeshInstance3D
var sky: SpaceflightSky
var star_mesh: MeshInstance3D
var star_array_mesh: ArrayMesh
var planck_texture: ImageTexture
var body_materials: Array[ShaderMaterial] = []
var warp_index := 0
var exaggeration_index := 0
var exposure_index := 2
var show_apparent := true
var look_yaw := 0.0
var look_pitch := 0.0
## Initialised to the "ship" preset, not to numbers that merely look like it: a
## HUD that opens reading `free` because the defaults drifted from the preset is
## the same class of lie as the label that kept saying `prograde`.
var orbit_azimuth: float = LOOK_MODE_AZIMUTH[0]
var orbit_elevation: float = LOOK_MODE_ELEVATION[0]
var orbit_zoom := 1.0
var look_mode := 0
var _mouse_drag := false
## Headless printing is counted in FRAMES, not wall seconds. `--quit-after N` is
## a frame count, so gating the print on elapsed real time made the verification
## depend on how fast the machine happened to be: at 143 fps the old 1-second
## threshold needed 143 frames, and `run_godot_headless.sh 200` printed once --
## or, on a faster machine, not at all.
const HEADLESS_PRINT_EVERY_FRAMES := 150
var _headless_frames := 0
var _headless_slew_commanded := false
var _headless_burn_commanded := false
var _headless_cruise_commanded := false
var throttle := 0.0
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

	# The catalogue lives next to the kernels, outside the Godot project, for the
	# same reason: it is fetched data (catalogs/MANIFEST.md).
	sky = SpaceflightSky.new()
	add_child(sky)
	sky.set_magnitude_limit(MAGNITUDE_LIMIT)
	sky.set_half_saturation(EXPOSURE_LEVELS[exposure_index])
	var catalogue := ProjectSettings.globalize_path("res://").path_join("../../catalogs/bsc5.dat")
	if not sky.load_catalogue(catalogue.simplify_path()):
		# Not fatal: the sky goes dark and says why. The dynamics does not care.
		push_warning("No star catalogue -- run scripts/fetch_star_catalog.sh. %s"
			% sky.get_last_error())

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

		# The relativistic shader, not a StandardMaterial3D: this is where the
		# per-vertex light time lives, because it is per-vertex by definition.
		var material := ShaderMaterial.new()
		material.shader = load("res://shaders/relativistic_body.gdshader")
		material.set_shader_parameter("reflectance", _colour_for(simulation.get_body_name(i)))
		# The Sun emits its own black body; everything else reflects the Sun's.
		material.set_shader_parameter("is_self_luminous",
			simulation.get_body_name(i) == "Sun")
		if planck_texture != null:
			material.set_shader_parameter("planck_table", planck_texture)
			material.set_shader_parameter("table_reference_temperature",
				sky.get_planck_table_reference_temperature())
		material.set_shader_parameter("light_speed_scene", simulation.get_light_speed_scene())
		mesh_instance.material_override = material

		add_child(mesh_instance)
		body_meshes.append(mesh_instance)
		body_materials.append(material)

	ship_mesh = MeshInstance3D.new()
	# Elongated along +x, the body's nose axis, so that the attitude is legible at
	# a glance: a cube would rotate invisibly.
	var ship_shape := BoxMesh.new()
	ship_shape.size = Vector3(SHIP_SIZE * 2.5, SHIP_SIZE, SHIP_SIZE)
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
	##
	## With stretch mode `canvas_items` the engine already scales the UI by the
	## window/base ratio, so this mostly matters when the aspect ratio changes --
	## `expand` grows the canvas rather than scaling it, and then the font would
	## otherwise stay put while the frame grew.
	var height := get_viewport().get_visible_rect().size.y
	if height <= 0.0:
		height = 720.0

	# height/30 was legible but the panel then covered most of the frame. /38 keeps
	# it roughly twice the original 13 px while leaving the view to the view.
	var font_size := int(clampf(roundf(height / 38.0), 15.0, 28.0))
	readout.add_theme_font_size_override("font_size", font_size)

	var margin := int(maxf(roundf(height * 0.018), 8.0))
	for side in ["margin_left", "margin_top", "margin_right", "margin_bottom"]:
		hud_margin.add_theme_constant_override(side, margin)


func _build_starfield() -> void:
	## 8786 real stars, drawn as points, coloured and dimmed by a shader that is
	## handed the Doppler factor and never told what beta is.
	##
	## The mesh is rebuilt each frame because the CPU is what aberrates the sky
	## (core/render/relativistic_sky.cpp). The alternative -- pass beta as a
	## uniform and aberrate in the vertex shader -- would save the upload and move
	## the physics into GLSL; the trade and its measured cost are
	## docs/architecture/relativistic-shaders.md section 7.
	star_mesh = MeshInstance3D.new()
	star_array_mesh = ArrayMesh.new()
	star_mesh.mesh = star_array_mesh

	var material := ShaderMaterial.new()
	material.shader = load("res://shaders/star_field.gdshader")
	if sky != null and sky.is_ready():
		# The colour table comes out of core/render/blackbody.hpp as an RGBAF
		# image. Nearest-neighbour would band the Planck locus visibly; linear is
		# what the C++ PlanckTable::sample_rgb reproduces, so the two agree.
		planck_texture = ImageTexture.create_from_image(sky.get_planck_table_image())
		material.set_shader_parameter("planck_table", planck_texture)
		material.set_shader_parameter("table_reference_temperature",
			sky.get_planck_table_reference_temperature())
		material.set_shader_parameter("half_saturation", sky.get_half_saturation())
	star_mesh.material_override = material

	# The sky is rebuilt every frame and Godot cannot know its extent from an
	# empty mesh, so say it: without this the whole field is frustum-culled the
	# moment the camera turns.
	star_mesh.custom_aabb = AABB(Vector3.ONE * -SKY_RADIUS * 1.1, Vector3.ONE * SKY_RADIUS * 2.2)
	add_child(star_mesh)


func _update_starfield() -> void:
	if sky == null or not sky.is_ready():
		return

	# The only physics that passes through this script: a vector, carried.
	sky.update_sky(simulation.get_beta_vector(), SKY_RADIUS)

	var surface := sky.get_surface_arrays()
	if surface.is_empty():
		return

	star_array_mesh.clear_surfaces()
	# The format flag is what makes CUSTOM0 four FLOATS. Without it Godot packs it
	# into four bytes, a temperature of 25944 K quantises to 1.0, every star comes
	# out the same colour -- and nothing anywhere reports an error.
	star_array_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_POINTS, surface["arrays"],
		[], {}, surface["format"])


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

	_apply_manual_rcs()
	_apply_camera_input(delta)

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
		# Where it APPEARS, not where it is: light time against the real ephemeris,
		# then aberration. Both done in core/ (relativistic-rendering.md section 2).
		mesh_instance.position = simulation.get_body_apparent_position(i)
		var radius: float = simulation.get_body_radius(i)
		mesh_instance.visible = radius > 0.0
		if radius > 0.0:
			mesh_instance.scale = Vector3.ONE * radius

		var material := body_materials[i]
		material.set_shader_parameter("doppler", simulation.get_body_doppler(i))
		material.set_shader_parameter("relative_velocity_scene",
			simulation.get_body_relative_velocity_scene(i))
		material.set_shader_parameter("light_speed_scene", simulation.get_light_speed_scene())
		material.set_shader_parameter("apply_light_time", show_apparent)
		material.set_shader_parameter("half_saturation", EXPOSURE_LEVELS[exposure_index])

	_update_starfield()

	ship_mesh.position = simulation.get_spacecraft_position()
	# The hull points where the attitude says it points -- not along the velocity,
	# which is what a simulator without attitude has to pretend.
	ship_mesh.basis = simulation.get_spacecraft_basis()

	_place_camera()
	# After _place_camera, because the shader measures the retarded time from
	# where the observer actually is, and the camera moved this frame.
	for material in body_materials:
		material.set_shader_parameter("observer_position_scene", camera.position)
	if star_mesh != null and star_mesh.material_override is ShaderMaterial:
		(star_mesh.material_override as ShaderMaterial).set_shader_parameter(
			"half_saturation", EXPOSURE_LEVELS[exposure_index])
	_update_readout()


const MANUAL_TORQUE := 400.0   ## N m, about what the modelled RCS can deliver


func _format_duration(seconds: float) -> String:
	## A cruise burn lasts years and an impulse burn lasts minutes; one unit
	## cannot show both. Seconds are useless at 2.5e8 and years are useless at 40.
	if seconds <= 0.0:
		return "--"
	if seconds < 120.0:
		return "%.1f s" % seconds
	if seconds < 7200.0:
		return "%.1f min" % (seconds / 60.0)
	if seconds < 172800.0:
		return "%.2f h" % (seconds / 3600.0)
	if seconds < 3.15576e7:
		return "%.2f d" % (seconds / 86400.0)
	return "%.3f yr" % (seconds / 3.15576e7)


func _set_throttle(value: float) -> void:
	## The throttle is changed BETWEEN frames, never inside a propagation step:
	## opening it mid-step would be a discontinuity in the derivative, which is
	## the same reason planned burns are split at their ignition epochs
	## (docs/architecture/navigation.md section 4).
	throttle = clampf(value, 0.0, 1.0)
	simulation.set_throttle(throttle)


func _apply_manual_rcs() -> void:
	## Held keys become a torque REQUEST in the body frame. The request goes to
	## the RCS, which fires thrusters, which burn propellant -- there is no path
	## from a key to the orientation (rule 28).
	var torque := Vector3.ZERO
	if Input.is_key_pressed(KEY_UP):
		torque.y += MANUAL_TORQUE
	if Input.is_key_pressed(KEY_DOWN):
		torque.y -= MANUAL_TORQUE
	if Input.is_key_pressed(KEY_LEFT):
		torque.z += MANUAL_TORQUE
	if Input.is_key_pressed(KEY_RIGHT):
		torque.z -= MANUAL_TORQUE
	if Input.is_key_pressed(KEY_PAGEUP):
		torque.x += MANUAL_TORQUE
	if Input.is_key_pressed(KEY_PAGEDOWN):
		torque.x -= MANUAL_TORQUE
	simulation.set_manual_torque(torque)


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
	## Orbit camera, in scene units. The projection origin is the focus itself, so
	## the camera sits a fraction of a unit from the origin and the float precision
	## stays microscopic (docs/architecture/rendering.md).
	##
	## The camera is placed on a sphere around the focus, at (azimuth, elevation)
	## in a frame built from the VELOCITY -- not from the world axes. That is what
	## makes the orbit mean something here: azimuth 180 is behind the ship, looking
	## into the forward cone, and azimuth 0 is ahead of it, looking down the sky
	## that went dark. Turning the camera with A and D sweeps between the two, and
	## the HUD reads out the Doppler factor the whole way.
	var target := ship_mesh.position
	var natural := CHASE_DISTANCE
	if focus_index >= 0:
		target = body_meshes[focus_index].position
		natural = maxf(simulation.get_body_radius(focus_index) * 3.0, 1.0)

	var frame := _orbit_frame(target)
	var forward: Vector3 = frame[0]
	var up: Vector3 = frame[1]
	var right := forward.cross(up)
	if right.length() < 1.0e-6:
		# Looking along the radial: any perpendicular will do, and the choice only
		# rotates the azimuth's zero, which nothing downstream depends on.
		right = forward.cross(Vector3.UP)
		if right.length() < 1.0e-6:
			right = forward.cross(Vector3.RIGHT)
	right = right.normalized()
	up = right.cross(forward).normalized()

	var offset := (forward * (cos(orbit_elevation) * cos(orbit_azimuth))
		+ right * (cos(orbit_elevation) * sin(orbit_azimuth))
		+ up * sin(orbit_elevation))

	camera.position = target + offset * (natural * orbit_zoom)
	_aim_camera(target - camera.position, up)


func _orbit_frame(target: Vector3) -> Array:
	## Returns [forward, up]; the caller builds `right` from them, so the handedness
	## lives in one place.
	if LOOK_MODES[look_mode] == "ship" and focus_index < 0:
		# The hull's own axes. get_spacecraft_basis() is orthonormal (it comes from
		# the attitude quaternion, ADR-0008) and x is the nose, which is why the
		# ship mesh is elongated along x in the first place.
		var hull := simulation.get_spacecraft_basis()
		return [hull.x.normalized(), hull.y.normalized()]

	# The velocity frame: `forward` is the axis the sky is aberrated about and `up`
	# is outward from the reference body, so elevation is "climb away from the
	# planet" and the horizon stays where the eye expects it.
	var forward := Vector3(simulation.get_beta_vector()).normalized()
	if forward.length() < 0.5:
		forward = Vector3.FORWARD
	var nadir := _reference_body_position() - target
	var up := (-nadir).normalized() if nadir.length() > 0.0 else Vector3.UP
	return [forward, up]


func _aim_camera(to_target: Vector3, up: Vector3) -> void:
	## The camera looks at the focus, and the free-look offsets are applied AFTER,
	## in the camera's own axes. Because the base orientation is rebuilt from
	## scratch every frame the offsets never accumulate drift, and "recentre" is
	## just setting two numbers back to zero.
	var forward := to_target
	if forward.length() < 1.0e-9:
		return
	forward = forward.normalized()

	# look_at fails outright when the forward direction and the up hint are
	# parallel -- which happens the moment the camera climbs to the pole of its
	# own orbit. Any perpendicular will do as a replacement.
	var hint := up.normalized() if up.length() > 0.0 else Vector3.UP
	if absf(forward.dot(hint)) > 0.999:
		hint = forward.cross(Vector3.RIGHT)
		if hint.length() < 1.0e-6:
			hint = forward.cross(Vector3.UP)
		hint = hint.normalized()

	camera.look_at(camera.position + forward, hint)
	camera.rotate_object_local(Vector3.UP, look_yaw)
	camera.rotate_object_local(Vector3.RIGHT, look_pitch)


func _apply_camera_input(delta: float) -> void:
	## Shift turns the same four keys from "move around the subject" into "turn in
	## place and leave it behind".
	var step := (LOOK_SPEED_KEY if _free_look_held() else ORBIT_SPEED_KEY) * delta
	var horizontal := 0.0
	var vertical := 0.0
	if Input.is_key_pressed(KEY_A):
		horizontal += step
	if Input.is_key_pressed(KEY_D):
		horizontal -= step
	if Input.is_key_pressed(KEY_W):
		vertical += step
	if Input.is_key_pressed(KEY_S):
		vertical -= step

	if _free_look_held():
		look_yaw += horizontal
		look_pitch += vertical
	else:
		orbit_azimuth += horizontal
		orbit_elevation += vertical
	_settle_camera()


func _free_look_held() -> bool:
	return Input.is_key_pressed(KEY_SHIFT)


func _settle_camera() -> void:
	orbit_azimuth = wrapf(orbit_azimuth, -PI, PI)
	orbit_elevation = clampf(orbit_elevation, -LOOK_PITCH_LIMIT, LOOK_PITCH_LIMIT)
	look_yaw = wrapf(look_yaw, -PI, PI)
	look_pitch = clampf(look_pitch, -LOOK_PITCH_LIMIT, LOOK_PITCH_LIMIT)


func _apply_look_preset() -> void:
	## Snap to the preset, keeping the zoom: flipping between the forward cone and
	## the aft sky should not undo how close you had got to the ship.
	orbit_azimuth = LOOK_MODE_AZIMUTH[look_mode]
	orbit_elevation = LOOK_MODE_ELEVATION[look_mode]
	look_yaw = 0.0
	look_pitch = 0.0


func _recentre_camera() -> void:
	## Everything back, zoom included. One key out of any orientation, because
	## getting lost pointing at empty sky is the failure mode of every camera like
	## this.
	_apply_look_preset()
	orbit_zoom = 1.0


func _at_look_preset() -> bool:
	## Whether the camera is actually where the preset puts it. The HUD needs this
	## because a label that keeps saying "prograde" after you have orbited away
	## from prograde is a readout that lies -- the same failure as measuring the
	## angle from one axis and the Doppler factor from another, one screen earlier.
	return (absf(wrapf(orbit_azimuth - LOOK_MODE_AZIMUTH[look_mode], -PI, PI)) < 0.02
		and absf(orbit_elevation - LOOK_MODE_ELEVATION[look_mode]) < 0.02
		and absf(look_yaw) < 0.02 and absf(look_pitch) < 0.02)


func _zoom_camera(steps: float) -> void:
	orbit_zoom = clampf(orbit_zoom * pow(ZOOM_STEP, steps), ZOOM_MIN, ZOOM_MAX)


func _unhandled_input(event: InputEvent) -> void:
	## Hold the right mouse button to move the camera around the subject; add
	## Shift to turn it in place instead. Captured rather than confined so the
	## pointer cannot run off the window mid-turn, and released the moment the
	## button is -- a simulator that steals the cursor is a simulator you cannot
	## quit.
	if event is InputEventMouseButton:
		var button := event as InputEventMouseButton
		match button.button_index:
			MOUSE_BUTTON_RIGHT:
				_mouse_drag = button.pressed
				if DisplayServer.get_name() != "headless":
					Input.mouse_mode = (Input.MOUSE_MODE_CAPTURED if _mouse_drag
						else Input.MOUSE_MODE_VISIBLE)
			MOUSE_BUTTON_WHEEL_UP:
				if button.pressed:
					_zoom_camera(-1.0)
			MOUSE_BUTTON_WHEEL_DOWN:
				if button.pressed:
					_zoom_camera(1.0)
	elif event is InputEventMouseMotion and _mouse_drag:
		var motion := event as InputEventMouseMotion
		if _free_look_held():
			look_yaw -= motion.relative.x * LOOK_SPEED_MOUSE
			look_pitch -= motion.relative.y * LOOK_SPEED_MOUSE
		else:
			orbit_azimuth -= motion.relative.x * ORBIT_SPEED_MOUSE
			orbit_elevation += motion.relative.y * ORBIT_SPEED_MOUSE
		_settle_camera()


func _headless_warp_schedule(snapshot: Dictionary) -> void:
	var elapsed: float = snapshot["elapsed_s"]
	var wanted := warp_index
	if not _headless_burn_commanded:
		# The slew takes a couple of minutes of simulation time, which at warp 1 is
		# more frames than any verification run has. Warping through it changes
		# nothing physical -- the warp decides how much coordinate time a frame
		# asks for, never how the propagator gets there (rule 21).
		#
		# 10x and not 100x: while the RCS is firing the propagator's own error
		# control keeps the steps short, so a 100x frame costs ten times the work
		# and buys nothing. Measured: 1200 frames in 8.7 s at 10x against over two
		# minutes at 100x. The burn itself needs a longer run -- `6000` -- and the
		# default budget is spent on the optics, which is what this milestone added.
		wanted = 1
	else:
		if elapsed > 2.0e6:
			wanted = WARP_LEVELS.size() - 1      # 1e8
		elif elapsed > 1.0e4:
			wanted = 7                           # 1e7
		elif elapsed > 6.0e2:
			wanted = 5                           # 1e5
		else:
			wanted = 2                           # 100
	if wanted != warp_index:
		warp_index = wanted
		simulation.set_time_warp(WARP_LEVELS[warp_index])

	# CRUISE trades thrust for exhaust velocity: 0.0899 c of budget becomes
	# 0.9048 c. Switched once the impulse burn has done its part.
	if not _headless_cruise_commanded and elapsed > 1.0e4:
		_headless_cruise_commanded = true
		simulation.set_engine_mode("CRUISE")
		print("\n[headless] CRUISE -- exhaust 0.5 c, budget 0.9048 c")


func _sky_lines() -> Array:
	## What the optics are doing, in numbers, so that "it looks fast" is never the
	## evidence. Every value came out of core/render/relativistic_sky.cpp.
	if sky == null or not sky.is_ready():
		return ["sky            no catalogue (scripts/fetch_star_catalog.sh)", ""]

	var d := sky.get_diagnostics()
	var moon_light := 0.0
	for i in range(simulation.get_body_count()):
		if simulation.get_body_name(i) == "Moon":
			moon_light = simulation.get_body_light_time(i)

	return [
		"stars          %d  (%s)" % [d["star_count"], "apparent" if show_apparent else "GEOMETRIC"],
		"forward cone   %.3f deg holds %d stars (%.2f %%)"
			% [d["forward_cone_deg"], d["stars_in_forward_cone"],
			   100.0 * float(d["fraction_in_forward_cone"])],
		"doppler        %.6f astern .. %.6f ahead" % [d["min_doppler"], d["max_doppler"]],
		"5800 K star    %s x ahead, %s x astern  (visible band)"
			% [String.num_scientific(d["reference_forward_visible"]),
			   String.num_scientific(d["reference_aft_visible"])],
		"exposure       %.4f half-saturation flux" % sky.get_half_saturation(),
		"light time     Moon %.4f s" % moon_light,
	] + _look_lines(d)


func _look_lines(d: Dictionary) -> Array:
	## Where the camera points, and what it is pointing into. The angle is the
	## camera's own geometry and is measured here; the Doppler factor is NOT --
	## it comes from core/relativity/optics.hpp through the sky, because it is
	## physics and this file does not do physics.
	##
	## This is what makes the free look an instrument: turn towards the velocity
	## and D climbs to gamma(1+beta); turn away and it falls to gamma(1-beta),
	## with the star field doing visibly what the number says.
	##
	## The angle is measured from the BARYCENTRIC velocity, not from prograde, and
	## the difference is not pedantry: prograde in this cockpit is relative to the
	## reference body (7.7 km/s around the Earth) while the sky is aberrated by the
	## velocity in the frame the stars are at rest in (30.7 km/s, dominated by the
	## Earth's own orbit). They point 30-odd degrees apart in LEO. Measuring the
	## angle from one and the Doppler factor from the other put two different
	## references on adjacent lines, which is how a readout lies without any number
	## in it being wrong.
	var forward := -camera.global_transform.basis.z
	var axis := Vector3(simulation.get_beta_vector()).normalized()
	var angle := rad_to_deg(forward.angle_to(axis)) if axis.length() > 0.5 else 0.0
	var inside := "  INSIDE the forward cone" if angle <= float(d["forward_cone_deg"]) else ""
	var where: String = LOOK_MODES[look_mode] if _at_look_preset() else "free"
	var zoom := "" if is_equal_approx(orbit_zoom, 1.0) else "   zoom %.2fx" % orbit_zoom
	return [
		"look           %s   %.1f deg off the aberration axis%s%s"
			% [where, angle, inside, zoom],
		"looking into   D = %.6f" % sky.get_doppler_in_direction(forward),
		"",
	]


func _sky_projection_lines(beta: float) -> Array:
	## What the SAME code does at a speed the ship is not travelling at. Labelled,
	## every time, because an unlabelled number here would be exactly the kind of
	## quiet lie this project exists to avoid: the state is untouched, and this is
	## a question asked of the optics, not a claim about the flight.
	if sky == null or not sky.is_ready():
		return []
	var heading := simulation.get_spacecraft_velocity_direction()
	var d := sky.get_diagnostics_at(heading * beta)
	return [
		"PROJECTION at beta = %.4f (the ship is NOT at this speed; the state is untouched)"
			% beta,
		"  forward cone %.3f deg holds %d stars (%.2f %%)"
			% [d["forward_cone_deg"], d["stars_in_forward_cone"],
			   100.0 * float(d["fraction_in_forward_cone"])],
		"  doppler      %.4f astern .. %.4f ahead" % [d["min_doppler"], d["max_doppler"]],
		"  5800 K star  %s x ahead, %s x astern  (visible band, not bolometric)"
			% [String.num_scientific(d["reference_forward_visible"]),
			   String.num_scientific(d["reference_aft_visible"])],
		"",
	]


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
		"propellant     %.3f kg" % s["propellant_kg"],
		"flow           %s kg/s   endurance %s" % [String.num_scientific(s["mass_flow_kg_s"]), _format_duration(s["endurance_s"])],
		"engine         %s   w = %.3f c" % [s["engine_mode"], s["exhaust_velocity_c"]],
		"throttle       %.0f %%      thrust %.1f N" % [s["throttle"] * 100.0, s["thrust_n"]],
		"delta-v left   %s m/s" % String.num_scientific(s["delta_v_budget_ms"]),
		"along track    %+.3f   orbit energy %+.1f J/kg/s" % [s["thrust_along_track"], s["specific_energy_rate"]],
		"",
		"pointing       %s   error %.3f deg" % [s["pointing_mode"], s["pointing_error_deg"]],
		"nose->prograde %.3f deg" % s["angle_to_prograde_deg"],
		"nose->nadir    %.3f deg" % s["angle_to_nadir_deg"],
		"spin rate      %.4f deg/s" % s["rotation_rate_deg_s"],
		"target         %s at %.0f km, %.1f m/s" % [s["target"], s["target_distance_m"] / 1000.0, s["target_relative_speed_ms"]],
		"",
		"beta           %s" % String.num_scientific(s["beta"]),
		"gamma - 1      %s" % String.num_scientific(s["lorentz_factor_minus_one"]),
		"render res.    %s m per float ulp at %s" % [String.num_scientific(s["render_resolution_m"]), s["reference"]],
		"",
	]
	lines.append_array(_sky_lines())
	lines.append_array([
		"focus: %s   body scale %.0fx" % [("spacecraft" if focus_index < 0 else simulation.get_body_name(focus_index)), EXAGGERATION_LEVELS[exaggeration_index]],
		"(, . warp   F focus   B body scale   R restart)",
		"(1 prograde  2 retrograde  3 normal  4 anti-normal  5 radial-out  0 hold)",
		"(arrows/PgUp/PgDn RCS   Z full throttle   X cutoff   -/= trim throttle)",
		"(M: engine mode IMPULSE <-> CRUISE)",
		"(E/Q exposure   L: light time + aberration on/off   C: cruise burn)",
		"(WASD / right-drag: orbit   +Shift: look around   [ ] wheel: zoom)",
		"(V: ship/prograde/retrograde frame   H: recentre)",
	])
	readout.text = "\n".join(lines)

	# Headless runs have no window: mirror the readout to stdout once a second so
	# that `--headless --quit-after N` is a real verification and not a silent
	# no-op. This is how Milestone 2 is checked on a machine with no display.
	if DisplayServer.get_name() == "headless":
		# Nobody can press a key without a display, so command a slew on the way
		# past: the printed pointing error then exercises the whole attitude
		# chain -- controller, RCS, torque, Euler's equations -- end to end.
		if not _headless_slew_commanded and s["elapsed_s"] > 2.0:
			_headless_slew_commanded = true
			simulation.set_pointing_mode("prograde")
			print("\n[headless] commanded PROGRADE")
		# Once pointed, burn: the apoapsis should climb while the propellant
		# falls. That exercises attitude, engine and orbit in one go.
		# 3 degrees, not 1: a PD controller settles at the tracking lag
		# 2 zeta n / omega_n = 2.593 deg and never gets closer
		# (docs/physics/attitude.md section 7.1). A threshold below that waits
		# forever -- which is what the first version did.
		#
		# Note the first condition: pointing_error is 0 while the mode is HOLD
		# (no target, no error), so without it the burn fires immediately -- at
		# 90 degrees off prograde, which is a fine demonstration of a cockpit
		# mistake and a poor demonstration of anything else.
		if _headless_slew_commanded and not _headless_burn_commanded \
				and s["pointing_mode"] == "PROGRADE" and s["pointing_error_deg"] < 3.0:
			_headless_burn_commanded = true
			_set_throttle(1.0)
			print("\n[headless] throttle 100%% at %.3f deg of pointing error"
				% s["pointing_error_deg"])
		# Once the impulse burn has been demonstrated, switch to CRUISE and the top
		# of the warp ladder. The optics of section 3 and section 10 are invisible
		# below beta ~ 0.1, and the only honest way to reach them is to actually
		# burn for eight years -- which at warp 1e8 is a few thousand frames.
		# Then fly, because the optics of section 3 and section 10 are invisible
		# below beta ~ 0.1 and the only honest way to reach them is to burn for
		# eight years. The ladder is keyed on SIMULATION time, not wall time, so
		# the same frame budget gets to the same place on any machine.
		#
		# Each rung waits for the one before to have done its job: no warp until
		# the attitude has settled, because a 1e8x step would ask the propagator to
		# cross the whole slew in one go.
		_headless_warp_schedule(s)

		_headless_frames += 1
		if _headless_frames % HEADLESS_PRINT_EVERY_FRAMES == 0:
			print("\n" + readout.text)
		# Once, early: the headline numbers of the milestone, produced by the
		# shipped code rather than quoted from the document. Section 3's cone,
		# section 4's reciprocal Doppler and section 10.2's band-limited beaming,
		# all against the 8786 real stars.
		if _headless_frames == HEADLESS_PRINT_EVERY_FRAMES:
			for beta in [0.0896, 0.9048, 0.99]:
				print("\n".join(_sky_projection_lines(beta)))


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
		KEY_1: simulation.set_pointing_mode("prograde")
		KEY_2: simulation.set_pointing_mode("retrograde")
		KEY_3: simulation.set_pointing_mode("normal")
		KEY_4: simulation.set_pointing_mode("anti_normal")
		KEY_5: simulation.set_pointing_mode("radial_out")
		KEY_6: simulation.set_pointing_mode("radial_in")
		KEY_0: simulation.set_pointing_mode("")
		KEY_E:
			exposure_index = mini(exposure_index + 1, EXPOSURE_LEVELS.size() - 1)
			sky.set_half_saturation(EXPOSURE_LEVELS[exposure_index])
		KEY_Q:
			exposure_index = maxi(exposure_index - 1, 0)
			sky.set_half_saturation(EXPOSURE_LEVELS[exposure_index])
		KEY_L:
			# Turns the OPTICS off, not the physics. The state is bit-for-bit the
			# same either way; what changes is which question the renderer asks.
			show_apparent = not show_apparent
			simulation.set_apparent_positions_enabled(show_apparent)
		KEY_C:
			# Everything needed to actually go fast, in one key: the effects of
			# section 3 and section 10 are invisible below beta ~ 0.1, and the only
			# honest way to see them is to fly there. Eight years of burning at
			# warp 1e8 is a few minutes of watching.
			simulation.set_engine_mode("CRUISE")
			simulation.set_pointing_mode("prograde")
			_set_throttle(1.0)
			warp_index = WARP_LEVELS.size() - 1
			simulation.set_time_warp(WARP_LEVELS[warp_index])
		KEY_V:
			look_mode = (look_mode + 1) % LOOK_MODES.size()
			_apply_look_preset()
		KEY_H: _recentre_camera()
		KEY_BRACKETLEFT: _zoom_camera(1.0)
		KEY_BRACKETRIGHT: _zoom_camera(-1.0)
		KEY_M: simulation.cycle_engine_mode()
		KEY_Z: _set_throttle(1.0)
		KEY_X: _set_throttle(0.0)
		KEY_EQUAL: _set_throttle(throttle + 0.1)
		KEY_MINUS: _set_throttle(throttle - 0.1)
