class_name GlobeView
extends SubViewportContainer
## The map wrapped on a sphere. Drag to turn it, scroll to zoom. Emits
## `pixel_hovered` with the map pixel under the mouse (or (-1, -1)), so the
## viewer can look up the cell exactly as it does on the flat map.
##
## Dragging keeps the point grabbed under the cursor: the globe turns about
## its own axis (yaw) and tilts toward the viewer (pitch), with north always
## up, so the turn is solved exactly for the point rather than guessed from
## how far the mouse moved.

signal pixel_hovered(pixel: Vector2i)
## A click (press and release without dragging) on the globe.
signal pixel_clicked(pixel: Vector2i)

var map_size := Vector2i(2048, 1024)

var _viewport: SubViewport
var _camera: Camera3D
var _pivot: Node3D
var _material: StandardMaterial3D
var _yaw := 0.0
var _pitch := 0.35
var _distance := 3.2
var _dragging := false
var _moved := false  # the press has moved far enough to be a drag, not a click
var _press_position := Vector2.ZERO
var _grab := Vector3.ZERO  # the grabbed point, on the unit sphere, in globe coordinates
var _has_grab := false


func _ready() -> void:
	stretch = true
	mouse_filter = Control.MOUSE_FILTER_STOP
	_viewport = SubViewport.new()
	_viewport.own_world_3d = true
	_viewport.transparent_bg = true
	_viewport.handle_input_locally = false
	add_child(_viewport)

	_pivot = Node3D.new()
	_viewport.add_child(_pivot)
	var sphere := MeshInstance3D.new()
	var mesh := SphereMesh.new()
	mesh.radius = 1.0
	mesh.height = 2.0
	mesh.radial_segments = 160
	mesh.rings = 80
	sphere.mesh = mesh
	_material = StandardMaterial3D.new()
	_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	_material.texture_filter = BaseMaterial3D.TEXTURE_FILTER_NEAREST
	sphere.material_override = _material
	_pivot.add_child(sphere)

	_camera = Camera3D.new()
	_camera.fov = 40.0
	_viewport.add_child(_camera)
	_update_camera()


func set_texture(texture: Texture2D) -> void:
	_material.albedo_texture = texture


func _update_camera() -> void:
	# Turn about the globe's own axis first, then tilt: north stays up.
	_pivot.basis = Basis(Vector3.RIGHT, _pitch) * Basis(Vector3.UP, _yaw)
	_camera.position = Vector3(0, 0, _distance)
	_camera.look_at(Vector3.ZERO)


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_LEFT:
			_dragging = event.pressed
			if event.pressed:
				_press_position = event.position
				_moved = false
				var hit := _sphere_hit(event.position)
				_has_grab = hit.size() > 0
				if _has_grab:
					_grab = _pivot.global_transform.basis.inverse() * hit[0]
			elif not _moved:
				var pixel := pixel_at(event.position)
				if pixel.x >= 0:
					pixel_clicked.emit(pixel)
		elif event.button_index == MOUSE_BUTTON_WHEEL_UP and event.pressed:
			_distance = maxf(1.4, _distance * 0.9)
			_update_camera()
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN and event.pressed:
			_distance = minf(6.0, _distance / 0.9)
			_update_camera()
	elif event is InputEventMouseMotion:
		if _dragging:
			if event.position.distance_to(_press_position) >= 5.0:
				_moved = true
			_drag_to(event.position, event.relative)
		pixel_hovered.emit(pixel_at(event.position))


# Turn the globe so the grabbed point sits under `local`. Off the globe (or
# when the press missed it) fall back to turning by the distance moved, at
# the rate the globe's surface moves on screen at this zoom.
func _drag_to(local: Vector2, relative: Vector2) -> void:
	var hit := _sphere_hit(local) if _has_grab else []
	if hit.size() > 0 and _solve_grab(hit[0]):
		_update_camera()
		return
	var rate := (_distance - 1.0) * tan(deg_to_rad(_camera.fov) * 0.5) * 2.0 / maxf(1.0, size.y)
	_yaw += relative.x * rate
	_pitch = clampf(_pitch + relative.y * rate, -1.4, 1.4)
	_update_camera()
	if _has_grab:  # grab again where the cursor is, if it is on the globe
		var again := _sphere_hit(local)
		if again.size() > 0:
			_grab = _pivot.global_transform.basis.inverse() * again[0]


# Find pitch and yaw that carry the grabbed point to world point `w`:
# Rx(pitch) * Ry(yaw) * grab = w. Rx keeps the height after Ry, so pitch
# follows from matching heights, then yaw from the angle around the axis.
func _solve_grab(w: Vector3) -> bool:
	var r := sqrt(w.y * w.y + w.z * w.z)
	if r < 1e-6:
		return false
	var c := clampf(_grab.y / r, -1.0, 1.0)
	var phi := atan2(w.z, w.y)
	var best := INF
	var pitch := _pitch
	for candidate in [phi + acos(c), phi - acos(c)]:
		var p: float = wrapf(candidate, -PI, PI)
		if absf(p) > 1.4:
			continue
		if absf(p - _pitch) < best:
			best = absf(p - _pitch)
			pitch = p
	if best == INF:
		return false
	# Undo the tilt, then read the turn about the axis.
	var untilted := Basis(Vector3.RIGHT, -pitch) * w
	_pitch = pitch
	_yaw = atan2(untilted.x, untilted.z) - atan2(_grab.x, _grab.z)
	return true


# The world point on the globe under a point in this control: [] if none.
func _sphere_hit(local: Vector2) -> Array:
	if size.x <= 0.0 or size.y <= 0.0:
		return []
	var p := local * (Vector2(_viewport.size) / size)
	var origin := _camera.project_ray_origin(p)
	var dir := _camera.project_ray_normal(p)
	var b := origin.dot(dir)
	var c := origin.dot(origin) - 1.0
	var disc := b * b - c
	if disc < 0.0:
		return []
	return [origin + dir * (-b - sqrt(disc))]


# Where a point of the map (u, v: 0-1 across and down the flat map) shows
# in this control: (x, y, facing), facing > 0 on the near side (1 face on).
func screen_of(uv: Vector2) -> Vector3:
	if _viewport == null or size.x <= 0.0 or size.y <= 0.0:
		return Vector3(0, 0, -1)
	var theta := uv.x * TAU
	var phi := uv.y * PI
	var local := Vector3(sin(phi) * sin(theta), cos(phi), sin(phi) * cos(theta))
	var world := _pivot.global_transform * local
	var facing := world.normalized().dot(_camera.global_position.normalized())
	var p := _camera.unproject_position(world) * (size / Vector2(_viewport.size))
	return Vector3(p.x, p.y, facing)


# The globe's radius on screen, in pixels.
func screen_radius() -> float:
	var d := maxf(1.01, _distance)
	return size.y * 0.5 / tan(deg_to_rad(_camera.fov) * 0.5) / sqrt(d * d - 1.0)


# Map pixel under a point in this control, or (-1, -1) off the globe.
# SphereMesh lays its texture out with u along the angle atan2(x, z) and v
# from the top (+y, north) to the bottom, matching the flat map.
func pixel_at(local: Vector2) -> Vector2i:
	var hits := _sphere_hit(local)
	if hits.is_empty():
		return Vector2i(-1, -1)
	var local_hit: Vector3 = _pivot.global_transform.affine_inverse() * hits[0]
	var u := atan2(local_hit.x, local_hit.z) / TAU
	if u < 0.0:
		u += 1.0
	var v := acos(clampf(local_hit.y, -1.0, 1.0)) / PI
	return Vector2i(clampi(int(u * map_size.x), 0, map_size.x - 1), clampi(int(v * map_size.y), 0, map_size.y - 1))
