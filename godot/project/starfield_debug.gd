extends Node3D
## The starfield validation harness (Milestone 6.1, Part B).
##
## Milestone 6 could not judge aberration, Doppler or beaming because the star
## field was invisible, and "invisible" is the one symptom every possible cause
## shares: a wrong projection, an empty buffer, a culled mesh and a black colour
## all produce the same screenshot. So this harness does not look at the sky. It
## walks the pipeline of docs/validation/starfield-debug.md section 2 one stage at
## a time, and every stage answers a question that has a NUMBER for an answer:
##
##   axes        do six stars on the six axes land where the camera says they are
##   scale       does the answer change with the radius the sky is drawn at
##   clip        is every star in front of the near plane and inside the far one
##   ladder      do 12, 100 and 8786 stars all reach the framebuffer
##   magnitude   is the rendered brightness monotone in V
##   effects     baseline, +aberration, +Doppler, +beaming, all
##   aberration  GPU angle vs CPU angle at beta = 0.1, 0.5, 0.9, 0.99
##   doppler     GPU chromaticity vs the shifted Planck colour
##   beaming     GPU intensity ratio vs the band-limited D^4
##   snapshots   reference images at 0c, 0.5c, 0.9c, 0.99c
##
## The oracle is never a human impression and never a stored screenshot: it is
## core/, reached through SpaceflightSky.get_apparent_direction(),
## get_expected_response() and get_expected_colour(). The snapshots exist to catch
## a REGRESSION, and they are compared with a robust metric, not pixel for pixel
## (section 26 of the milestone prompt).
##
## Run it with scripts/starfield_validation.sh. Exit status 0 means every stage
## passed; anything else means read the log.

const OUT_DIR := "user://starfield"

## The sky radius the production scene uses. A star field is DIRECTIONAL -- the
## radius should be arithmetically irrelevant -- and the `scale` stage is what
## turns that sentence into a measurement.
const PRODUCTION_SKY_RADIUS := 1.9e5
const PRODUCTION_CAMERA_FAR := 2.0e5
## Kept in step with main.gd's CAMERA_NEAR. Stage 3b is what decides its value.
const PRODUCTION_CAMERA_NEAR := 0.05
## What the scene shipped with through Milestone 6, kept here as the control.
const MILESTONE_6_CAMERA_NEAR := 0.01

## Where a star has to land to count as landing there. One pixel is the
## rasteriser's own quantum and the centroid of a 3-pixel sprite cannot do better
## than about a third of one, so 1.5 px is tight without being a coin toss.
const POSITION_TOLERANCE_PX := 1.5

## Aberration is checked in ANGLE, because that is what the physics predicts. The
## budget is the angle one pixel subtends at the centre of the frame, doubled:
## the measurement cannot be sharper than the raster, and demanding that it be
## would only be measuring the blob detector.
const ANGLE_TOLERANCE_SCALE := 2.0

## Chromaticity: the GPU samples the Planck table as an 8-bit-per-channel render
## target after a tone curve, so agreement is asked for in the ratio of the
## channels, to 4%, and not in the absolute value.
const CHROMATICITY_TOLERANCE := 0.04

## Intensity: same reasoning, but the quantity spans four decades, so the test is
## on the RATIO between two stars and is allowed 12% -- an 8-bit channel at a
## value of 30 already carries 3% of quantisation on its own.
const INTENSITY_RATIO_TOLERANCE := 0.12

## The faintest byte a synthetic star is looked for at. Two, not one: a value of
## one is a single quantisation step and dither, rounding or a fractional alpha
## can all produce it, so the blob detector would start finding the noise floor.
const FAINT_THRESHOLD_BYTE := 2

## What the framebuffer can hold. A star whose LINEAR response would encode below
## one byte is not dim in the image, it is absent from it, and no measurement can
## recover it. This is the sensor's floor, not a tolerance: a red-shifted star
## that goes out at beta = 0.5 is the physics working, and the harness has to be
## able to say that instead of reporting a missing star.
const DETECTOR_FLOOR := 0.0004

## How closely a measured pixel has to reproduce what core/ says the detector
## response is. 3% covers the quantisation of an 8-bit channel at the dim end of
## the ladder -- one step at a byte value of 30 is already 3% -- and nothing else:
## the first run that got the sRGB decode right landed inside 0.5% at every rung.
const PHOTOMETRY_TOLERANCE := 0.03

var sky: SpaceflightSky
var camera: Camera3D
var star_mesh: MeshInstance3D
var star_array_mesh: ArrayMesh
var material: ShaderMaterial
var planck_texture: ImageTexture

var failures: Array[String] = []
var lines: Array[String] = []
var stage_queue: Array = []
var settle_frames := 0
var pending: Callable
var sky_radius := 1000.0


func _ready() -> void:
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(OUT_DIR))

	# The project stretches its canvas, so Camera3D.unproject_position() answers in
	# the 1440 x 900 design space while the framebuffer comes back at whatever the
	# window is. One rescale between the two is one more place for a half pixel to
	# hide, and this harness measures in pixels. Turn the stretch off: the viewport
	# is then the window, and predicted and measured coordinates are the same
	# space.
	get_window().content_scale_mode = Window.CONTENT_SCALE_MODE_DISABLED

	var env := WorldEnvironment.new()
	var e := Environment.new()
	# A black background, and nothing else: no ambient, no glow, no tonemap
	# curve. Every one of those would put something between the shader's output
	# and the pixel this harness measures.
	e.background_mode = Environment.BG_COLOR
	e.background_color = Color(0.0, 0.0, 0.0)
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.0, 0.0, 0.0)
	e.ambient_light_energy = 0.0
	e.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	env.environment = e
	add_child(env)

	camera = Camera3D.new()
	camera.near = 0.01
	camera.far = PRODUCTION_CAMERA_FAR
	camera.current = true
	camera.position = Vector3.ZERO
	add_child(camera)

	sky = SpaceflightSky.new()
	add_child(sky)
	sky.set_magnitude_limit(7.96)

	star_mesh = MeshInstance3D.new()
	star_array_mesh = ArrayMesh.new()
	star_mesh.mesh = star_array_mesh
	material = ShaderMaterial.new()
	material.shader = load("res://shaders/star_field.gdshader")
	star_mesh.material_override = material
	add_child(star_mesh)

	stage_queue = [
		_stage_axes, _stage_scale, _stage_clip, _stage_depth, _stage_ladder,
		_stage_magnitude, _stage_effects, _stage_aberration, _stage_doppler,
		_stage_beaming, _stage_snapshots,
	]
	_log("starfield validation harness")
	_log("Godot %s, %s" % [Engine.get_version_info()["string"],
		RenderingServer.get_video_adapter_name()])
	_log("viewport %s" % [get_viewport().get_visible_rect().size])
	_log("")


func _process(_delta: float) -> void:
	# Rendering is a frame behind everything this script does, so every stage
	# arranges the scene, yields for two frames, and only then reads the
	# framebuffer. Reading it in the same frame reads the PREVIOUS stage.
	if pending.is_valid():
		settle_frames -= 1
		if settle_frames > 0:
			return
		var f := pending
		pending = Callable()
		f.call()
		return
	if stage_queue.is_empty():
		_finish()
		return
	var stage: Callable = stage_queue.pop_front()
	stage.call()


func _after_render(what: Callable) -> void:
	pending = what
	settle_frames = 3


# ---------------------------------------------------------------------------
# framebuffer measurement
# ---------------------------------------------------------------------------

func _capture(name: String) -> Image:
	var img := get_viewport().get_texture().get_image()
	img.save_png("%s/%s.png" % [OUT_DIR, name])
	return img


## The framebuffer as four flat arrays: r, g, b and the max channel, indexed
## y * width + x.
##
## Image.get_pixel() is a bound call per pixel, and a 1280 x 800 frame is a
## million of them -- twice over, once to find the seeds and once inside the
## flood fill. Measured, the first version of this harness spent ten minutes on
## stage 1 and never reached stage 2. get_data() is one copy and then arithmetic.
func _read_frame(img: Image) -> Dictionary:
	var source := img
	if source.get_format() != Image.FORMAT_RGBA8:
		source = Image.new()
		source.copy_from(img)
		source.convert(Image.FORMAT_RGBA8)
	var bytes := source.get_data()
	var n := source.get_width() * source.get_height()
	var r := PackedFloat32Array(); r.resize(n)
	var g := PackedFloat32Array(); g.resize(n)
	var b := PackedFloat32Array(); b.resize(n)
	var v := PackedFloat32Array(); v.resize(n)
	# The framebuffer is sRGB-ENCODED; the shader wrote a linear value. Comparing
	# the stored byte with what core/ computed would be comparing a number with
	# its own transfer function applied -- a measured 0.647 against an expected
	# 0.530 is the same quantity, counted once. Everything this harness compares
	# is linear, and the decode happens here, once, rather than at eight call
	# sites.
	var table := _srgb_decode_table()
	for i in range(n):
		var o := i * 4
		var fr: float = table[bytes[o]]
		var fg: float = table[bytes[o + 1]]
		var fb: float = table[bytes[o + 2]]
		r[i] = fr
		g[i] = fg
		b[i] = fb
		v[i] = maxf(fr, maxf(fg, fb))
	return {"r": r, "g": g, "b": b, "v": v,
		"w": source.get_width(), "h": source.get_height()}


var _decode_table: PackedFloat32Array


## IEC 61966-2-1 sRGB, inverted, tabulated over the 256 values a byte can hold.
## A table because the alternative is a pow() per channel per pixel, and a frame
## has three million of them.
func _srgb_decode_table() -> PackedFloat32Array:
	if _decode_table.size() == 256:
		return _decode_table
	_decode_table.resize(256)
	for i in range(256):
		var c := float(i) / 255.0
		_decode_table[i] = (c / 12.92) if c <= 0.04045 else pow((c + 0.055) / 1.055, 2.4)
	return _decode_table


## Connected lit pixels, grouped into blobs, each with its intensity-weighted
## centroid and its peak channel values.
##
## Weighted rather than the brightest pixel: a point sprite three pixels across
## has no unique brightest pixel, and taking the first one found would quantise
## every measured position onto the pixel grid -- which is exactly the error the
## aberration stage is trying to measure.
func _blobs(img: Image, threshold_byte: int = 8) -> Array:
	# The threshold is given as a BYTE because that is the unit the framebuffer
	# actually has. Expressed as a linear intensity it would read like a physical
	# limit and would silently change meaning the moment the transfer function
	# did.
	var threshold: float = _srgb_decode_table()[maxi(1, threshold_byte)] * 0.999
	var frame := _read_frame(img)
	var w: int = frame["w"]
	var h: int = frame["h"]
	var v: PackedFloat32Array = frame["v"]
	var r: PackedFloat32Array = frame["r"]
	var g: PackedFloat32Array = frame["g"]
	var b: PackedFloat32Array = frame["b"]
	var n := w * h
	var seen := PackedByteArray(); seen.resize(n)
	var out: Array = []
	for seed in range(n):
		if seen[seed] != 0 or v[seed] <= threshold:
			continue
		# Flood fill, iteratively: an 8786-star frame has blobs that would
		# recurse deeper than GDScript's stack likes.
		var stack: PackedInt32Array = PackedInt32Array([seed])
		seen[seed] = 1
		var sum_w := 0.0
		var sum_x := 0.0
		var sum_y := 0.0
		var peak := Color(0, 0, 0)
		var peak_v := 0.0
		var count := 0
		while not stack.is_empty():
			var k: int = stack[stack.size() - 1]
			stack.remove_at(stack.size() - 1)
			var kx := k % w
			var ky := k / w
			var value: float = v[k]
			sum_w += value
			sum_x += float(kx) * value
			sum_y += float(ky) * value
			count += 1
			if value > peak_v:
				peak_v = value
				peak = Color(r[k], g[k], b[k])
			for dy in [-1, 0, 1]:
				var ny: int = ky + dy
				if ny < 0 or ny >= h:
					continue
				for dx in [-1, 0, 1]:
					var nx: int = kx + dx
					if nx < 0 or nx >= w:
						continue
					var nk: int = ny * w + nx
					if seen[nk] != 0 or v[nk] <= threshold:
						continue
					seen[nk] = 1
					stack.append(nk)
		if sum_w > 0.0:
			out.append({
				"centre": Vector2(sum_x / sum_w, sum_y / sum_w),
				"peak": peak,
				"peak_value": peak_v,
				"pixels": count,
				"energy": sum_w,
			})
	return out


## Where core/ says star `index` is, what core/ says its response is, and what
## the framebuffer actually has there.
##
## One function because every photometric stage needs exactly this, and because
## "not rendered" has to be separated into its two very different causes: the
## star is below the sensor's floor (physics, expected, not a failure) or it is
## above it and missing (a defect).
func _measure_star(index: int, img: Image, blobs: Array, radius := 24.0) -> Dictionary:
	var world := sky.get_apparent_direction(index) * float(sky_radius)
	var behind := camera.is_position_behind(world)
	var predicted := _to_image_space(camera.unproject_position(world), img)
	var expected: float = sky.get_expected_response(index)
	var blob = null if behind else _blob_near(blobs, predicted, radius)
	var in_frame := not behind and predicted.x >= 0.0 and predicted.y >= 0.0 \
		and predicted.x < float(img.get_width()) and predicted.y < float(img.get_height())
	return {
		"predicted": predicted,
		"blob": blob,
		"expected": expected,
		"behind": behind,
		"in_frame": in_frame,
		"below_floor": expected < DETECTOR_FLOOR,
		"measured": -1.0 if blob == null else blob["peak_value"],
		"colour": Color(0, 0, 0) if blob == null else blob["peak"],
		"distance": -1.0 if blob == null else (blob["centre"] as Vector2).distance_to(predicted),
	}


## Why a star that should have been measurable was not, in words a log can carry.
func _why_missing(m: Dictionary) -> String:
	if m["behind"]:
		return "behind the camera"
	if not m["in_frame"]:
		return "outside the frame at %s" % [m["predicted"].round()]
	if m["below_floor"]:
		return "below the sensor floor (response %.6f < %.6f)" % [m["expected"], DETECTOR_FLOOR]
	return "NOT FOUND at %s -- above the floor and missing" % [m["predicted"].round()]


## The blob nearest a predicted position, or null if nothing is within `radius`.
func _blob_near(blobs: Array, at: Vector2, radius: float = 24.0):
	var best = null
	var best_d := radius
	for b in blobs:
		var d: float = (b["centre"] as Vector2).distance_to(at)
		if d < best_d:
			best_d = d
			best = b
	return best


## Viewport coordinates to image INDICES.
##
## Two conventions meet here and the half pixel between them is measured, not
## assumed. A projection answers in continuous coordinates, where the boundary
## between the first and second pixel is at 1.0; a blob centroid is a weighted
## mean of integer indices, where pixel 0 has its centre at 0.5. Subtracting the
## half pixel moves the residual on a star that is exactly where it should be
## from 1.05-1.25 px to 0.54-0.71 px, at every point size and in every stage --
## so the convention is real and what is left over is the rasteriser's own
## sub-pixel placement of a point sprite, which no convention will remove.
func _to_image_space(viewport_point: Vector2, img: Image) -> Vector2:
	var vp := get_viewport().get_visible_rect().size
	return Vector2(viewport_point.x * float(img.get_width()) / vp.x,
		viewport_point.y * float(img.get_height()) / vp.y) - Vector2(0.5, 0.5)


func _from_image_space(image_point: Vector2, img: Image) -> Vector2:
	var vp := get_viewport().get_visible_rect().size
	var centred := image_point + Vector2(0.5, 0.5)
	return Vector2(centred.x * vp.x / float(img.get_width()),
		centred.y * vp.y / float(img.get_height()))


## The angle one pixel of the CAPTURED IMAGE subtends on the optical axis. The
## yardstick every angular tolerance in this file is written in -- and it has to
## be the image's pixel, not the design viewport's, because the image is what
## gets measured.
func _pixel_angle_rad(img: Image) -> float:
	return deg_to_rad(camera.fov) / float(img.get_height())


# ---------------------------------------------------------------------------
# scene arrangement
# ---------------------------------------------------------------------------

func _load_synthetic(directions: PackedVector3Array, temps: PackedFloat64Array,
		mags: PackedFloat64Array) -> void:
	if not sky.load_synthetic(directions, temps, mags):
		_fail("load_synthetic failed: %s" % sky.get_last_error())
		return
	_refresh_material()


func _load_real() -> void:
	var catalogue := ProjectSettings.globalize_path("res://../../catalogs/bsc5.dat")
	if not sky.load_catalogue(catalogue):
		_fail("load_catalogue failed: %s" % sky.get_last_error())
		return
	_refresh_material()


func _refresh_material() -> void:
	planck_texture = ImageTexture.create_from_image(sky.get_planck_table_image())
	material.set_shader_parameter("planck_table", planck_texture)
	material.set_shader_parameter("table_reference_temperature",
		sky.get_planck_table_reference_temperature())
	material.set_shader_parameter("half_saturation", sky.get_half_saturation())


## `debug` bypasses the photometry; `flat` bypasses the sprite SHAPING -- the
## size that tracks brightness and the radial alpha falloff. They are two
## different questions and they get two different switches
## (docs/validation/starfield-debug.md section 3).
func _set_debug_mode(on: bool, point_size: float = 3.0, flat := false) -> void:
	material.set_shader_parameter("starfield_debug", on)
	material.set_shader_parameter("debug_point_size", point_size)
	material.set_shader_parameter("starfield_flat_sprite", flat or on)


func _upload(beta: Vector3, aberration := true, doppler := true, beaming := true) -> void:
	sky.update_sky_effects(beta, sky_radius, aberration, doppler, beaming)
	var surface := sky.get_surface_arrays()
	star_array_mesh.clear_surfaces()
	if surface.is_empty():
		return
	star_array_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_POINTS, surface["arrays"],
		[], {}, surface["format"])
	# Godot cannot derive an extent from a mesh it is handed every frame, and a
	# missing AABB frustum-culls the whole field the moment the camera turns --
	# a failure mode indistinguishable, on screen, from the one this harness was
	# written to find.
	star_mesh.custom_aabb = AABB(Vector3.ONE * -sky_radius * 1.1, Vector3.ONE * sky_radius * 2.2)


## Point the camera so that every star in the current catalogue is inside the
## frame, and widen the field until it is.
##
## Not a convenience. At beta = 0.99 aberration piles the whole sky into a cone
## eight degrees wide; a beaming test spreads its stars over 105 degrees. No
## fixed attitude and fixed field of view holds both, and a star that fell off
## the edge of the frame reports as "not rendered" -- which is the one answer
## this harness must never produce for a reason other than the one it is testing.
func _frame_all_stars(up := Vector3.UP, margin := 1.3, minimum_fov := 10.0,
		maximum_fov := 120.0) -> void:
	var count := sky.get_star_count()
	if count == 0:
		return
	var mean := Vector3.ZERO
	for i in range(count):
		mean += sky.get_apparent_direction(i)
	if mean.length() < 1.0e-6:
		mean = sky.get_apparent_direction(0)
	mean = mean.normalized()
	var widest := 0.0
	for i in range(count):
		widest = maxf(widest, absf(mean.angle_to(sky.get_apparent_direction(i))))
	# Godot's `fov` names the VERTICAL field, which on a landscape viewport is the
	# smaller of the two, so sizing to it frames both axes.
	camera.fov = clampf(rad_to_deg(widest) * 2.0 * margin, minimum_fov, maximum_fov)
	_look_along(mean, up)


func _look_along(direction: Vector3, up: Vector3 = Vector3.UP) -> void:
	var d := direction.normalized()
	var u := up
	if absf(d.dot(u.normalized())) > 0.99:
		u = Vector3.RIGHT if absf(d.dot(Vector3.RIGHT)) < 0.9 else Vector3.FORWARD
	camera.look_at_from_position(Vector3.ZERO, d * sky_radius, u)


# ---------------------------------------------------------------------------
# stage 1: six stars on six axes  (prompt section 19, 20)
# ---------------------------------------------------------------------------

const AXIS_NAMES := ["star_right(+x)", "star_left(-x)", "star_forward(+y)",
	"star_backward(-y)", "star_up(+z)", "star_down(-z)"]
const AXIS_DIRECTIONS := [Vector3(1, 0, 0), Vector3(-1, 0, 0), Vector3(0, 1, 0),
	Vector3(0, -1, 0), Vector3(0, 0, 1), Vector3(0, 0, -1)]


func _axis_catalogue() -> void:
	var dirs := PackedVector3Array()
	var temps := PackedFloat64Array()
	var mags := PackedFloat64Array()
	for d in AXIS_DIRECTIONS:
		dirs.append(d)
		temps.append(5800.0)
		mags.append(0.0)
	_load_synthetic(dirs, temps, mags)


func _stage_axes() -> void:
	_heading("1. six stars on six axes  (prompt 19)")
	sky_radius = 1000.0
	camera.far = 1.0e5
	_axis_catalogue()
	_set_debug_mode(true, 5.0)
	_upload(Vector3.ZERO)
	_axis_step(0)


func _axis_step(i: int) -> void:
	if i >= AXIS_DIRECTIONS.size():
		_set_debug_mode(false)
		_log("")
		return
	_look_along(AXIS_DIRECTIONS[i])
	# Put the stage back on the queue so the loop resumes after the capture.
	stage_queue.push_front(func() -> void: _axis_step(i + 1))
	_after_render(func() -> void: _axis_measure(i))


func _axis_measure(i: int) -> void:
	var img := _capture("01_axis_%d_%s" % [i, AXIS_NAMES[i].split("(")[0]])
	var blobs := _blobs(img)
	# The centre of a w x h image in INDEX coordinates, matching _to_image_space().
	var centre := Vector2(float(img.get_width() - 1), float(img.get_height() - 1)) * 0.5
	if blobs.is_empty():
		_fail("%s: nothing rendered while looking straight at it" % AXIS_NAMES[i])
		_log("  %-20s NOTHING RENDERED" % AXIS_NAMES[i])
		return
	var on_axis = _blob_near(blobs, centre, 40.0)
	if on_axis == null:
		_fail("%s: %d blobs rendered, none at the centre of the frame"
			% [AXIS_NAMES[i], blobs.size()])
		_log("  %-20s %d blobs, none centred" % [AXIS_NAMES[i], blobs.size()])
		return
	var offset: float = (on_axis["centre"] as Vector2).distance_to(centre)
	var ok := offset <= POSITION_TOLERANCE_PX
	if not ok:
		_fail("%s: %.2f px off the optical axis" % [AXIS_NAMES[i], offset])
	_log("  %-20s %d blobs, centred to %.2f px, peak %.3f   %s"
		% [AXIS_NAMES[i], blobs.size(), offset, on_axis["peak_value"], "ok" if ok else "FAIL"])


# ---------------------------------------------------------------------------
# stage 2: the radius must not matter  (prompt section 20)
# ---------------------------------------------------------------------------

func _stage_scale() -> void:
	_heading("2. sky radius is arithmetically irrelevant  (prompt 20)")
	_axis_catalogue()
	_set_debug_mode(true, 5.0)
	_scale_step(0, [])


const SCALE_RADII := [1.0, 1000.0, PRODUCTION_SKY_RADIUS]


func _scale_step(i: int, measured: Array) -> void:
	if i >= SCALE_RADII.size():
		var reference: Array = measured[0]
		for k in range(1, measured.size()):
			var row: Array = measured[k]
			var worst := 0.0
			for j in range(mini(reference.size(), row.size())):
				worst = maxf(worst, (reference[j] as Vector2).distance_to(row[j]))
			var ok := worst <= POSITION_TOLERANCE_PX and row.size() == reference.size()
			if not ok:
				_fail("sky radius %s moved the stars by %.2f px (%d vs %d visible)"
					% [SCALE_RADII[k], worst, row.size(), reference.size()])
			_log("  radius %10s : %d stars, worst move vs radius %s is %.3f px   %s"
				% [SCALE_RADII[k], row.size(), SCALE_RADII[0], worst, "ok" if ok else "FAIL"])
		_set_debug_mode(false)
		_log("")
		return
	sky_radius = SCALE_RADII[i]
	# The far plane has to contain the sphere or the test would be measuring the
	# far plane, and the near plane has to leave the sphere its depth steps or it
	# would be measuring stage 3b. Both scale with the radius, which is the point:
	# what this stage asks is whether the radius matters once the CONFIGURATION
	# around it is held in proportion.
	camera.far = float(sky_radius) * 1.1
	camera.near = float(sky_radius) * (PRODUCTION_CAMERA_NEAR / PRODUCTION_SKY_RADIUS)
	_upload(Vector3.ZERO)
	# A direction that hits no axis star dead on, so all six are off-centre and
	# a projection error shows up as a DIFFERENT offset rather than as nothing.
	_look_along(Vector3(1, 1, 1))
	stage_queue.push_front(func() -> void: _scale_step(i + 1, measured))
	_after_render(func() -> void:
		var img := _capture("02_scale_%d" % i)
		var blobs := _blobs(img)
		var centres: Array = []
		for b in blobs:
			centres.append(b["centre"])
		centres.sort_custom(func(a, b): return a.x * 10000.0 + a.y < b.x * 10000.0 + b.y)
		measured.append(centres)
		_log("  radius %10s : near %s far %s, %d stars rendered"
			% [sky_radius, camera.near, camera.far, blobs.size()]))


# ---------------------------------------------------------------------------
# stage 3: clip space  (prompt section 20)
# ---------------------------------------------------------------------------

func _stage_clip() -> void:
	_heading("3. clip space  (prompt 20)")
	sky_radius = PRODUCTION_SKY_RADIUS
	camera.near = PRODUCTION_CAMERA_NEAR
	camera.far = PRODUCTION_CAMERA_FAR
	_load_real()
	_upload(Vector3.ZERO)
	_look_along(Vector3(0.3, 0.8, 0.5))

	# The projection is Godot's own, read back rather than reconstructed: the
	# question is whether the stars the ENGINE will transform land in front of
	# the near plane, so the engine's matrix is the one to ask.
	var projection := camera.get_camera_projection()
	var view := camera.get_camera_transform().affine_inverse()
	var count := sky.get_star_count()
	var behind := 0
	var beyond_far := 0
	var inside_near := 0
	var min_w := INF
	var max_w := -INF
	var min_ndc_z := INF
	var max_ndc_z := -INF
	for i in range(count):
		var d := sky.get_apparent_direction(i)
		var world := d * float(sky_radius)
		var eye := view * world
		var clip := projection * Vector4(eye.x, eye.y, eye.z, 1.0)
		if clip.w <= 0.0:
			behind += 1
			continue
		min_w = minf(min_w, clip.w)
		max_w = maxf(max_w, clip.w)
		var ndc_z := clip.z / clip.w
		min_ndc_z = minf(min_ndc_z, ndc_z)
		max_ndc_z = maxf(max_ndc_z, ndc_z)
		var distance := -eye.z
		if distance > camera.far:
			beyond_far += 1
		if distance < camera.near:
			inside_near += 1

	_log("  stars                 : %d" % count)
	_log("  w <= 0 (behind camera): %d   -- expected, half the sky is behind" % behind)
	_log("  closer than near=%s : %d" % [camera.near, inside_near])
	_log("  farther than far=%s : %d" % [camera.far, beyond_far])
	_log("  w in front of camera  : [%s, %s]" % [min_w, max_w])
	_log("  ndc z                 : [%.6f, %.6f]" % [min_ndc_z, max_ndc_z])
	var ok := beyond_far == 0 and inside_near == 0
	if not ok:
		_fail("clip space: %d stars past the far plane, %d inside the near plane"
			% [beyond_far, inside_near])
	# Every star in front of the camera must sit strictly inside the depth range,
	# or the depth test alone would remove it -- and that removal would look
	# exactly like the bug this harness exists to rule out.
	_log("  every visible star inside [near, far]: %s" % ("yes" if ok else "NO"))
	_log("")


# ---------------------------------------------------------------------------
# stage 3b: depth headroom  (prompt section 20)
# ---------------------------------------------------------------------------

## How many stars survive the depth buffer, as a function of the near plane.
##
## This is the second defect Milestone 6 could not see past the first one. The
## depth buffer has 24 bits; with reverse-Z a fragment's depth is near/distance;
## a star on the sky sphere therefore lands at near/SKY_RADIUS, and at the near
## plane the scene shipped with that is 5.26e-8 -- less than the 5.96e-8 that one
## quantisation step is worth. Stars whose depth rounds to the far plane's zero
## are dropped by the depth test, silently, and WHICH of them round is decided by
## float arithmetic. A third of the sky went missing that way.
##
## The stage stays in the suite after the fix, because the failure is invisible:
## it does not warn, it does not error, and what it leaves on screen is a
## perfectly plausible star field with two thirds of the stars in it.
const DEPTH_STAR_COUNT := 400
const DEPTH_NEAR_LADDER := [MILESTONE_6_CAMERA_NEAR, 0.012, 0.02, PRODUCTION_CAMERA_NEAR, 0.5]


func _stage_depth() -> void:
	_heading("3b. depth headroom against the near plane  (prompt 20)")
	sky_radius = PRODUCTION_SKY_RADIUS
	camera.far = PRODUCTION_CAMERA_FAR
	camera.fov = 90.0

	var dirs := PackedVector3Array()
	var temps := PackedFloat64Array()
	var mags := PackedFloat64Array()
	for k in range(DEPTH_STAR_COUNT):
		var z := 1.0 - 2.0 * (float(k) + 0.5) / float(DEPTH_STAR_COUNT)
		var r := sqrt(maxf(0.0, 1.0 - z * z))
		var phi := float(k) * PI * (3.0 - sqrt(5.0))
		dirs.append(Vector3(r * cos(phi), r * sin(phi), z))
		temps.append(5800.0)
		mags.append(0.0)
	_load_synthetic(dirs, temps, mags)
	_set_debug_mode(true, 3.0)
	_upload(Vector3.ZERO)
	_look_along(Vector3(0.3, 0.8, 0.5))
	_depth_step(0)


func _depth_step(i: int) -> void:
	if i >= DEPTH_NEAR_LADDER.size():
		camera.near = PRODUCTION_CAMERA_NEAR
		_set_debug_mode(false)
		_log("  one 24-bit depth step is %s; the sky sphere sits at near/%s"
			% [String.num_scientific(pow(2.0, -24.0)), PRODUCTION_SKY_RADIUS])
		_log("")
		return
	camera.near = DEPTH_NEAR_LADDER[i]
	stage_queue.push_front(func() -> void: _depth_step(i + 1))
	_after_render(func() -> void:
		var img := _capture("03b_depth_near_%d" % i)
		var blobs := _blobs(img)
		var in_frame := 0
		var found := 0
		for k in range(sky.get_star_count()):
			var m := _measure_star(k, img, blobs, 5.0)
			if not m["in_frame"]:
				continue
			in_frame += 1
			if m["blob"] != null:
				found += 1
		var headroom: float = float(camera.near) / float(sky_radius) / pow(2.0, -24.0)
		var complete := in_frame > 0 and found == in_frame
		# Only the production near plane has to pass. The rest of the ladder is
		# the evidence for why it is where it is, and the 0.01 row is expected to
		# fail -- if it ever stops failing, the measurement has stopped measuring.
		if is_equal_approx(camera.near, PRODUCTION_CAMERA_NEAR) and not complete:
			_fail("depth: at the production near plane %s, only %d of %d in-frame stars rendered"
				% [camera.near, found, in_frame])
		if is_equal_approx(camera.near, MILESTONE_6_CAMERA_NEAR) and complete:
			_fail("depth: near %s no longer loses stars -- the control case has stopped controlling"
				% camera.near)
		_log("  near %-7s : depth %s = %5.2f steps,  %3d in frame, %3d rendered   %s"
			% [camera.near, String.num_scientific(float(camera.near) / float(sky_radius)),
			headroom, in_frame, found,
			"complete" if complete else "LOSES %d" % (in_frame - found)]))


# ---------------------------------------------------------------------------
# stage 4: 6 -> 12 -> 100 -> 8786  (prompt section 19)
# ---------------------------------------------------------------------------

func _stage_ladder() -> void:
	_heading("4. star count ladder  (prompt 19)")
	sky_radius = PRODUCTION_SKY_RADIUS
	camera.near = PRODUCTION_CAMERA_NEAR
	camera.far = PRODUCTION_CAMERA_FAR
	_ladder_step(0, [])


const LADDER := [6, 12, 100, -1]   ## -1 = the whole BSC5 catalogue


func _ladder_step(i: int, results: Array) -> void:
	if i >= LADDER.size():
		_set_debug_mode(false)
		for r in results:
			if r["rendered"] <= 0:
				_fail("%s: nothing reached the framebuffer" % r["label"])
		_log("")
		return

	var n: int = LADDER[i]
	if n < 0:
		_load_real()
	else:
		var dirs := PackedVector3Array()
		var temps := PackedFloat64Array()
		var mags := PackedFloat64Array()
		# A deterministic spiral on the sphere, so the same n always gives the
		# same n directions and a regression is a regression and not a reseed.
		for k in range(n):
			var z := 1.0 - 2.0 * (float(k) + 0.5) / float(n)
			var r := sqrt(maxf(0.0, 1.0 - z * z))
			var phi := float(k) * PI * (3.0 - sqrt(5.0))
			dirs.append(Vector3(r * cos(phi), r * sin(phi), z))
			temps.append(5800.0)
			mags.append(1.0)
		_load_synthetic(dirs, temps, mags)

	_set_debug_mode(true, 3.0)
	_upload(Vector3.ZERO)
	# Wide enough to hold a good fraction of the sphere, so the count means
	# something.
	camera.fov = 90.0
	_look_along(Vector3(0.3, 0.8, 0.5))
	var label := "BSC5 (%d stars)" % sky.get_star_count() if n < 0 else "%d synthetic" % n
	stage_queue.push_front(func() -> void: _ladder_step(i + 1, results))
	_after_render(func() -> void:
		var img := _capture("04_ladder_%d" % i)
		var blobs := _blobs(img)
		var lit := 0
		for b in blobs:
			lit += b["pixels"]
		# Blobs alone say nothing: half the sphere is behind the camera and
		# neighbouring stars merge. What means something is how many of the stars
		# the PROJECTION puts inside the frame were found there -- and for the
		# crowded cases, that blobs never exceed that count.
		var expected_in_frame := 0
		for k in range(sky.get_star_count()):
			var m := _measure_star(k, img, [], 0.0)
			if m["in_frame"]:
				expected_in_frame += 1
		var found := 0
		for k in range(sky.get_star_count()):
			var m := _measure_star(k, img, blobs, 6.0)
			if m["in_frame"] and m["blob"] != null:
				found += 1
		results.append({"label": label, "rendered": blobs.size(), "in_frame": expected_in_frame,
			"found": found})
		# A synthetic set is laid out so that no two stars can land on the same
		# pixels, so every one of them has to be found. The real catalogue has
		# genuine close pairs -- 8786 stars produce 1575 blobs here -- and when two
		# merge, the joint centroid can sit further from either star than the
		# search radius. That is the blob detector's limit, not the renderer's, so
		# the real catalogue is asked for 99.5% and the synthetic sets for all of
		# it.
		var required := expected_in_frame if n >= 0 \
			else int(ceil(float(expected_in_frame) * 0.995))
		var ok := expected_in_frame > 0 and found >= required
		if not ok:
			_fail("%s: %d of %d stars inside the frame reached the framebuffer (needed %d)"
				% [label, found, expected_in_frame, required])
		_log("  %-22s loaded %5d, %4d in frame, %4d found, %4d blobs, %6d lit px   %s"
			% [label, sky.get_star_count(), expected_in_frame, found, blobs.size(), lit,
			"ok" if ok else "FAIL"]))


# ---------------------------------------------------------------------------
# stage 5: magnitude  (prompt section 21)
# ---------------------------------------------------------------------------

const MAGNITUDE_LADDER := [-1.0, 0.0, 1.0, 3.0, 5.0, 6.0]


func _stage_magnitude() -> void:
	_heading("5. magnitude is monotone  (prompt 21)")
	camera.fov = 75.0
	sky_radius = 1000.0
	camera.near = 0.01
	camera.far = 1.0e5

	# Six stars in a row across the field, same temperature, differing only in V.
	var dirs := PackedVector3Array()
	var temps := PackedFloat64Array()
	var mags := PackedFloat64Array()
	for i in MAGNITUDE_LADDER.size():
		var angle := deg_to_rad(-25.0 + 10.0 * float(i))
		dirs.append(Vector3(sin(angle), 1.0, 0.0).normalized())
		temps.append(5800.0)
		mags.append(MAGNITUDE_LADDER[i])
	_load_synthetic(dirs, temps, mags)
	# Flat sprites: the quantity under test is the RESPONSE, and a sprite whose
	# size and alpha both track that response would put the answer into the
	# measurement twice.
	_set_debug_mode(false, 7.0, true)
	_upload(Vector3.ZERO)
	_frame_all_stars()

	_after_render(func() -> void:
		var img := _capture("05_magnitude")
		var blobs := _blobs(img, FAINT_THRESHOLD_BYTE)
		var previous := INF
		var previous_m := -INF
		var monotone := true
		var seen := 0
		var worst_relative := 0.0
		for i in MAGNITUDE_LADDER.size():
			var m := _measure_star(i, img, blobs, 20.0)
			if m["blob"] == null:
				_log("  V = %+4.1f  expected %.5f   %s"
					% [MAGNITUDE_LADDER[i], m["expected"], _why_missing(m)])
				if not m["below_floor"]:
					_fail("V = %+.1f has response %.4f and did not render"
						% [MAGNITUDE_LADDER[i], m["expected"]])
					monotone = false
				continue
			seen += 1
			var measured: float = m["measured"]
			var expected: float = m["expected"]
			var relative: float = absf(measured - expected) / maxf(expected, 1.0e-6)
			worst_relative = maxf(worst_relative, relative)
			if measured >= previous and previous < INF:
				monotone = false
				_fail("V = %+.1f is not dimmer than V = %+.1f (%.4f vs %.4f)"
					% [MAGNITUDE_LADDER[i], previous_m, measured, previous])
			_log("  V = %+4.1f  expected %.5f  measured %.5f  relative %.4f  %3d px  %.2f px off"
				% [MAGNITUDE_LADDER[i], expected, measured, relative, m["blob"]["pixels"],
				m["distance"]])
			previous = measured
			previous_m = MAGNITUDE_LADDER[i]
		# Monotone is the question section 21 asks, but the response is also
		# checked against core/: a ladder can be monotone and still wrong.
		if worst_relative > PHOTOMETRY_TOLERANCE:
			_fail("magnitude: worst response error %.4f (budget %.4f)"
				% [worst_relative, PHOTOMETRY_TOLERANCE])
		_log("  %d of %d rendered, strictly decreasing: %s, worst response error %.4f (budget %.3f)"
			% [seen, MAGNITUDE_LADDER.size(), "yes" if monotone else "NO", worst_relative,
			PHOTOMETRY_TOLERANCE])
		_log(""))


# ---------------------------------------------------------------------------
# stage 6: one effect at a time  (prompt section 22)
# ---------------------------------------------------------------------------

const EFFECT_CASES := [
	{"name": "baseline", "ab": false, "do": false, "be": false},
	{"name": "aberration", "ab": true, "do": false, "be": false},
	{"name": "doppler", "ab": false, "do": true, "be": false},
	{"name": "beaming", "ab": false, "do": false, "be": true},
	{"name": "all", "ab": true, "do": true, "be": true},
]
const EFFECT_BETA := Vector3(0.0, 0.9, 0.0)


func _stage_effects() -> void:
	_heading("6. one effect at a time at beta = 0.9  (prompt 22)")
	sky_radius = PRODUCTION_SKY_RADIUS
	camera.near = PRODUCTION_CAMERA_NEAR
	camera.far = PRODUCTION_CAMERA_FAR
	camera.fov = 90.0
	_load_real()
	_set_debug_mode(false)
	_effect_step(0, [])


func _effect_step(i: int, rows: Array) -> void:
	if i >= EFFECT_CASES.size():
		# The point of the ladder is that each toggle CHANGES something, and that
		# the baseline is not empty. Both are numbers, not impressions.
		var baseline: Dictionary = rows[0]
		if baseline["blobs"] <= 0:
			_fail("baseline sky at beta = 0.9 with every effect off is empty")
		for k in range(1, rows.size()):
			var row: Dictionary = rows[k]
			if row["blobs"] == baseline["blobs"] and \
					absf(row["energy"] - baseline["energy"]) < 1.0e-6:
				_fail("%s changed nothing against the baseline" % row["name"])
		_log("")
		return
	var c: Dictionary = EFFECT_CASES[i]
	_upload(EFFECT_BETA, c["ab"], c["do"], c["be"])
	_look_along(EFFECT_BETA)   ## into the forward cone
	stage_queue.push_front(func() -> void: _effect_step(i + 1, rows))
	_after_render(func() -> void:
		var img := _capture("06_effect_%d_%s" % [i, c["name"]])
		var blobs := _blobs(img)
		var energy := 0.0
		for b in blobs:
			energy += b["energy"]
		rows.append({"name": c["name"], "blobs": blobs.size(), "energy": energy})
		_log("  %-11s aberration %-5s doppler %-5s beaming %-5s : %4d blobs, energy %10.1f"
			% [c["name"], c["ab"], c["do"], c["be"], blobs.size(), energy]))


# ---------------------------------------------------------------------------
# stage 7: aberration, GPU angle vs CPU angle  (prompt section 23)
# ---------------------------------------------------------------------------

const ABERRATION_BETAS := [0.0, 0.1, 0.5, 0.9, 0.99]

## Stars at known angles from the boost axis, spread over 100 degrees.
##
## Not wider, and the reason is the camera and not the physics: a rectilinear
## projection cannot hold a 120-degree spread without a 120-degree field, and at
## that field the corners are stretched to the point where the measurement is
## about the projection. Not narrower, because the whole point is to catch the
## stars at very different aberration angles on one frame. The stars lie in the
## x-y plane and the frame is landscape, so the spread is laid along the WIDE
## axis -- which is why every stage that uses this passes +z as the up vector.
const ABERRATION_ANGLES_DEG := [30.0, 55.0, 80.0, 105.0, 130.0]


func _stage_aberration() -> void:
	_heading("7. aberration: rendered angle vs core/relativity/optics.hpp  (prompt 23)")
	sky_radius = 1000.0
	camera.near = 0.01
	camera.far = 1.0e5
	camera.fov = 100.0

	# The boost is along +y; the stars are spread in the x-y plane at known polar
	# angles from it, one per test angle.
	var dirs := PackedVector3Array()
	var temps := PackedFloat64Array()
	var mags := PackedFloat64Array()
	for a in ABERRATION_ANGLES_DEG:
		var t := deg_to_rad(a)
		dirs.append(Vector3(sin(t), cos(t), 0.0))
		temps.append(5800.0)
		mags.append(0.0)
	_load_synthetic(dirs, temps, mags)
	# Debug mode ON: this stage is about GEOMETRY, and at beta = 0.99 the aft
	# stars are e^-52 of their rest flux. Letting the photometry take part would
	# turn a geometric test into a test of whether the star is bright enough to
	# find -- which is stage 9's question, not this one.
	_set_debug_mode(true, 5.0)
	_aberration_step(0)


func _aberration_step(i: int) -> void:
	if i >= ABERRATION_BETAS.size():
		_set_debug_mode(false)
		_log("")
		return
	var beta: float = ABERRATION_BETAS[i]
	_upload(Vector3(0.0, beta, 0.0))
	# The field follows the stars. At beta = 0 they span 100 degrees and at
	# beta = 0.99 they span 15, and a fixed frame would hold neither well.
	_frame_all_stars(Vector3(0, 0, 1), 1.15)
	stage_queue.push_front(func() -> void: _aberration_step(i + 1))
	_after_render(func() -> void: _aberration_measure(i, beta))


func _aberration_measure(i: int, beta: float) -> void:
	var img := _capture("07_aberration_beta_%s" % str(beta).replace(".", "p"))
	var blobs := _blobs(img)
	var tolerance := _pixel_angle_rad(img) * ANGLE_TOLERANCE_SCALE
	var worst := 0.0
	var worst_star := -1
	var compared := 0
	var origin := camera.global_position
	for k in ABERRATION_ANGLES_DEG.size():
		var cpu := sky.get_apparent_direction(k)
		var predicted_vp := camera.unproject_position(cpu * float(sky_radius))
		if not camera.is_position_behind(cpu * float(sky_radius)):
			var predicted := _to_image_space(predicted_vp, img)
			var blob = _blob_near(blobs, predicted, 30.0)
			if blob == null:
				continue
			# The measured pixel, turned back into a direction through the SAME
			# projection the engine used. This is the GPU's answer.
			var measured_vp := _from_image_space(blob["centre"], img)
			var gpu := (camera.project_position(measured_vp, float(sky_radius)) - origin).normalized()
			var error: float = absf(cpu.normalized().angle_to(gpu))
			compared += 1
			if error > worst:
				worst = error
				worst_star = k
	var ok := compared == ABERRATION_ANGLES_DEG.size() and worst <= tolerance
	if not ok:
		_fail("aberration at beta = %s: %d of %d stars matched, worst error %.4f deg (budget %.4f)"
			% [beta, compared, ABERRATION_ANGLES_DEG.size(), rad_to_deg(worst),
			rad_to_deg(tolerance)])
	# The physics headline, printed alongside: the rest-frame angle a star has to
	# sit at to be seen at 90 degrees is arccos(beta), and the CPU is asked for it
	# rather than the formula being written here.
	_log("  beta %-5s : %d/%d stars, worst GPU-vs-CPU angle %.5f deg (budget %.5f)  star %d  %s"
		% [beta, compared, ABERRATION_ANGLES_DEG.size(), rad_to_deg(worst),
		rad_to_deg(tolerance), worst_star, "ok" if ok else "FAIL"])
	if beta > 0.0:
		var forward_cone := rad_to_deg(acos(minf(1.0, beta)))
		var rows: Array[String] = []
		for k in ABERRATION_ANGLES_DEG.size():
			var rest: float = ABERRATION_ANGLES_DEG[k]
			var seen := rad_to_deg(sky.get_apparent_direction(k).angle_to(Vector3(0, 1, 0)))
			rows.append("%.0f->%.2f" % [rest, seen])
		_log("           rest angle -> apparent: %s   (90 deg lands at %.3f)"
			% [", ".join(rows), forward_cone])


# ---------------------------------------------------------------------------
# stage 8: Doppler chromaticity  (prompt section 24)
# ---------------------------------------------------------------------------

const DOPPLER_TEMPERATURES := [3000.0, 5800.0, 10000.0]
const DOPPLER_BETA := 0.5


func _stage_doppler() -> void:
	_heading("8. Doppler: rendered chromaticity vs the shifted Planck colour  (prompt 24)")
	sky_radius = 1000.0
	camera.fov = 100.0

	# Each temperature at two angles to the boost: one ahead, where the star is
	# blue-shifted, and one astern, where it is red-shifted. The same star at two
	# Doppler factors, on one frame -- which is what makes the comparison a
	# comparison and not two separate readings.
	#
	# Aberration is left ON, because the harness asks the CPU where each star
	# ended up rather than assuming; what is being tested here is the COLOUR.
	var dirs := PackedVector3Array()
	var temps := PackedFloat64Array()
	var mags := PackedFloat64Array()
	var labels: Array[String] = []
	var ahead_deg := 40.0
	var astern_deg := 120.0
	for i in DOPPLER_TEMPERATURES.size():
		for s in [0, 1]:
			var polar: float = deg_to_rad(ahead_deg if s == 0 else astern_deg)
			# Spread the six in azimuth so that no two blobs can merge, WITHOUT
			# changing any star's angle to the boost axis -- which is the only
			# thing the Doppler factor depends on.
			var azimuth := deg_to_rad(-30.0 + 12.0 * float(i * 2 + s))
			dirs.append(Vector3(sin(polar) * cos(azimuth), cos(polar),
				sin(polar) * sin(azimuth)))
			temps.append(DOPPLER_TEMPERATURES[i])
			# The astern stars are red-shifted AND beamed away from the observer,
			# and at magnitude 1.5 the 3000 K one falls off the bottom of an 8-bit
			# channel. Making it bright enough to measure is not making the test
			# easier: the quantity under test is the CHROMATICITY, and a star the
			# framebuffer cannot hold carries no chromaticity at all.
			mags.append(1.5 if s == 0 else -3.5)
			labels.append("%.0fK %s" % [DOPPLER_TEMPERATURES[i],
				"ahead" if s == 0 else "astern"])
	_load_synthetic(dirs, temps, mags)
	_set_debug_mode(false, 7.0, true)
	_upload(Vector3(0.0, DOPPLER_BETA, 0.0))
	_frame_all_stars()

	_after_render(func() -> void:
		var img := _capture("08_doppler")
		var blobs := _blobs(img, FAINT_THRESHOLD_BYTE)
		var worst := 0.0
		var compared := 0
		var skipped := 0
		for k in labels.size():
			var m := _measure_star(k, img, blobs, 20.0)
			var expected: Color = sky.get_expected_colour(k)
			var d: float = sky.get_doppler_of(k)
			var shifted: float = sky.get_star_temperature(k) * d
			if m["blob"] == null:
				_log("  %-14s D %.4f  T\' %7.0f K   %s" % [labels[k], d, shifted, _why_missing(m)])
				if m["below_floor"]:
					skipped += 1
				else:
					_fail("Doppler: %s is above the sensor floor and did not render" % labels[k])
				continue
			var measured: Color = m["colour"]
			# Chromaticity, not absolute value: what the Doppler shift MEANS for
			# colour is the ratio between the channels, and separating that from
			# the brightness is what lets stage 9 test the brightness on its own.
			var es: float = expected.r + expected.g + expected.b
			var ms: float = measured.r + measured.g + measured.b
			if es <= 0.0 or ms <= 0.0:
				skipped += 1
				continue
			var er := expected.r / es
			var eb := expected.b / es
			var mr := measured.r / ms
			var mb := measured.b / ms
			var error: float = maxf(absf(er - mr), absf(eb - mb))
			compared += 1
			worst = maxf(worst, error)
			_log("  %-14s D %.4f  T\' %7.0f K  expected r/b %.3f/%.3f  measured %.3f/%.3f  err %.4f"
				% [labels[k], d, shifted, er, eb, mr, mb, error])
		var ok := compared > 0 and worst <= CHROMATICITY_TOLERANCE
		if not ok:
			_fail("Doppler chromaticity: %d compared, worst %.4f (budget %.4f)"
				% [compared, worst, CHROMATICITY_TOLERANCE])
		_log("  %d compared, %d below the sensor floor, worst chromaticity error %.4f (budget %.4f)   %s"
			% [compared, skipped, worst, CHROMATICITY_TOLERANCE, "ok" if ok else "FAIL"])
		_log(""))


# ---------------------------------------------------------------------------
# stage 9: beaming  (prompt section 25)
# ---------------------------------------------------------------------------

const BEAMING_ANGLES_DEG := [0.0, 45.0, 75.0, 90.0, 105.0]
const BEAMING_BETA := 0.5


func _stage_beaming() -> void:
	_heading("9. beaming: rendered intensity ratios vs the band-limited D^4  (prompt 25)")
	sky_radius = 1000.0
	camera.fov = 110.0

	var dirs := PackedVector3Array()
	var temps := PackedFloat64Array()
	var mags := PackedFloat64Array()
	for a in BEAMING_ANGLES_DEG:
		var t := deg_to_rad(a)
		dirs.append(Vector3(sin(t), cos(t), 0.0))
		temps.append(5800.0)
		# Magnitude 4: dim enough at rest that the forward star is not already
		# saturated, so a factor between two stars is still readable.
		mags.append(4.0)
	_load_synthetic(dirs, temps, mags)
	_set_debug_mode(false, 7.0, true)
	# Aberration OFF: this stage is about INTENSITY, and moving the stars would
	# only make them harder to find. The Doppler factor does not depend on where
	# the star appears, only on where it is.
	_upload(Vector3(0.0, BEAMING_BETA, 0.0), false, true, true)
	_frame_all_stars(Vector3(0, 0, 1), 1.15)

	_after_render(func() -> void:
		var img := _capture("09_beaming")
		var blobs := _blobs(img, FAINT_THRESHOLD_BYTE)
		var measured: Array = []
		var expected: Array = []
		for k in BEAMING_ANGLES_DEG.size():
			var m := _measure_star(k, img, blobs, 20.0)
			measured.append(m["measured"])
			expected.append(m["expected"])
			if m["blob"] == null:
				_log("  %5.0f deg  D %.4f  expected response %.5f   %s"
					% [BEAMING_ANGLES_DEG[k], sky.get_beaming_of(k), m["expected"],
					_why_missing(m)])
				if not m["below_floor"]:
					_fail("beaming: the star at %.0f deg is above the sensor floor and did not render"
						% BEAMING_ANGLES_DEG[k])
				continue
			_log("  %5.0f deg  D %.4f  expected response %.5f  measured %.5f  %.2f px off"
				% [BEAMING_ANGLES_DEG[k], sky.get_beaming_of(k), m["expected"], m["measured"],
				m["distance"]])
		# Ratios against the 90-degree star, which the boost leaves closest to its
		# rest brightness. Ratios and not absolutes because what section 10.1
		# predicts is a FACTOR: D^4 corrected to the band. The absolute value is
		# checked too, but as a separate number, so that a wrong exposure and a
		# wrong exponent cannot cancel.
		var reference := BEAMING_ANGLES_DEG.find(90.0)
		var worst := 0.0
		var worst_absolute := 0.0
		var compared := 0
		for k in BEAMING_ANGLES_DEG.size():
			if measured[k] <= 0.0:
				continue
			worst_absolute = maxf(worst_absolute,
				absf(measured[k] - expected[k]) / maxf(expected[k], 1.0e-6))
			if k == reference or measured[reference] <= 0.0:
				continue
			var er: float = expected[k] / expected[reference]
			var mr: float = measured[k] / measured[reference]
			var relative: float = absf(mr - er) / er
			compared += 1
			worst = maxf(worst, relative)
			_log("    %5.0f deg / 90 deg : expected %8.4f  measured %8.4f  relative error %.4f"
				% [BEAMING_ANGLES_DEG[k], er, mr, relative])
		var ok := compared == BEAMING_ANGLES_DEG.size() - 1 \
			and worst <= INTENSITY_RATIO_TOLERANCE and worst_absolute <= PHOTOMETRY_TOLERANCE
		if not ok:
			_fail("beaming: %d of %d ratios, worst ratio error %.4f (budget %.4f), worst absolute %.4f (budget %.4f)"
				% [compared, BEAMING_ANGLES_DEG.size() - 1, worst, INTENSITY_RATIO_TOLERANCE,
				worst_absolute, PHOTOMETRY_TOLERANCE])
		_log("  %d ratios, worst ratio error %.4f (budget %.4f), worst absolute response error %.4f (budget %.4f)   %s"
			% [compared, worst, INTENSITY_RATIO_TOLERANCE, worst_absolute, PHOTOMETRY_TOLERANCE,
			"ok" if ok else "FAIL"])
		_log(""))


# ---------------------------------------------------------------------------
# stage 10: reference snapshots  (prompt section 26)
# ---------------------------------------------------------------------------

const SNAPSHOT_BETAS := [0.0, 0.5, 0.9, 0.99]
## Outside res:// on purpose: the references are evidence that belongs with the
## report, not an asset the game loads. globalize_path() is what makes a path
## above the project root readable at all.
const REFERENCE_DIR := "res://../../docs/validation/starfield-reference"

## How different two renders of the same sky may be and still count as the same
## sky. Not pixel equality: a different GPU, a different driver and a different
## rasterisation rule all move a point sprite by a fraction of a pixel. The
## metric is the mean absolute difference of a 64 x 64 downsample, which is
## insensitive to exactly that and sensitive to a star that moved, vanished or
## changed colour.
const SNAPSHOT_TOLERANCE := 0.02
const SNAPSHOT_GRID := 64


func _stage_snapshots() -> void:
	_heading("10. reference snapshots  (prompt 26)")
	sky_radius = PRODUCTION_SKY_RADIUS
	camera.near = PRODUCTION_CAMERA_NEAR
	camera.far = PRODUCTION_CAMERA_FAR
	camera.fov = 75.0
	_load_real()
	_set_debug_mode(false)
	_snapshot_step(0)


func _snapshot_step(i: int) -> void:
	if i >= SNAPSHOT_BETAS.size():
		_log("")
		return
	var beta: float = SNAPSHOT_BETAS[i]
	_upload(Vector3(0.0, beta, 0.0))
	# A fixed attitude across the whole ladder, 40 degrees off the boost axis, so
	# that what changes between the four images is the PHYSICS and not the camera.
	camera.look_at_from_position(Vector3.ZERO,
		Vector3(sin(deg_to_rad(40.0)), cos(deg_to_rad(40.0)), 0.0) * sky_radius, Vector3(0, 0, 1))
	stage_queue.push_front(func() -> void: _snapshot_step(i + 1))
	_after_render(func() -> void:
		var name := "10_sky_beta_%s" % str(beta).replace(".", "p")
		var img := _capture(name)
		var signature := _downsample(img, SNAPSHOT_GRID)
		var reference_path := ProjectSettings.globalize_path("%s/%s.png" % [REFERENCE_DIR, name])
		var lit := 0
		for b in _blobs(img):
			lit += b["pixels"]
		var note := ""
		if FileAccess.file_exists(reference_path):
			var reference := Image.load_from_file(reference_path)
			var difference := _signature_difference(signature, _downsample(reference, SNAPSHOT_GRID))
			var ok := difference <= SNAPSHOT_TOLERANCE
			if not ok:
				_fail("snapshot beta = %s differs from the reference by %.4f (budget %.4f)"
					% [beta, difference, SNAPSHOT_TOLERANCE])
			note = "vs reference: %.5f (budget %.3f)  %s" % [difference, SNAPSHOT_TOLERANCE,
				"ok" if ok else "FAIL"]
		else:
			note = "no reference stored yet -- this run can seed one"
		_log("  beta %-5s : %6d lit pixels   %s" % [beta, lit, note]))


## A coarse luminance signature. Robust in the sense that matters: it survives a
## sub-pixel difference in where a point sprite lands, and it does not survive a
## star that is not there any more.
func _downsample(img: Image, n: int) -> PackedFloat32Array:
	var frame := _read_frame(img)
	var w: int = frame["w"]
	var h: int = frame["h"]
	var r: PackedFloat32Array = frame["r"]
	var g: PackedFloat32Array = frame["g"]
	var b: PackedFloat32Array = frame["b"]
	var out := PackedFloat32Array()
	out.resize(n * n)
	for gy in range(n):
		var y0 := gy * h / n
		var y1: int = maxi(y0 + 1, (gy + 1) * h / n)
		for gx in range(n):
			var x0 := gx * w / n
			var x1: int = maxi(x0 + 1, (gx + 1) * w / n)
			var total := 0.0
			for y in range(y0, y1):
				var row := y * w
				for x in range(x0, x1):
					var i := row + x
					total += 0.2126 * r[i] + 0.7152 * g[i] + 0.0722 * b[i]
			out[gy * n + gx] = total / float((x1 - x0) * (y1 - y0))
	return out


func _signature_difference(a: PackedFloat32Array, b: PackedFloat32Array) -> float:
	if a.size() != b.size():
		return INF
	var total := 0.0
	for i in a.size():
		total += absf(a[i] - b[i])
	return total / float(a.size())


# ---------------------------------------------------------------------------

func _heading(text: String) -> void:
	_log("---- %s" % text)


func _log(text: String) -> void:
	lines.append(text)
	print("[starfield] %s" % text)


func _fail(text: String) -> void:
	failures.append(text)


func _finish() -> void:
	_log("================================================================")
	if failures.is_empty():
		_log("STARFIELD: PASS  -- every stage measured, nothing outside budget")
	else:
		_log("STARFIELD: FAIL  -- %d finding(s):" % failures.size())
		for f in failures:
			_log("  * %s" % f)
	var log_path := "%s/starfield-validation.log" % OUT_DIR
	var file := FileAccess.open(log_path, FileAccess.WRITE)
	if file != null:
		file.store_string("\n".join(lines) + "\n")
		file.close()
	print("[starfield] artefacts in %s" % ProjectSettings.globalize_path(OUT_DIR))
	get_tree().quit(0 if failures.is_empty() else 1)
