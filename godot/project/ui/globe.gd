class_name GlobeView
extends SubViewportContainer
## The map wrapped on a sphere. Drag to turn it, scroll to zoom. Emits
## `pixel_hovered` with the map pixel under the mouse (or (-1, -1)), so the
## viewer can look up the cell exactly as it does on the flat map.

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
var _press_position := Vector2.ZERO


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
	_pivot.rotation = Vector3(_pitch, _yaw, 0.0)
	_camera.position = Vector3(0, 0, _distance)
	_camera.look_at(Vector3.ZERO)


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_LEFT:
			_dragging = event.pressed
			if event.pressed:
				_press_position = event.position
			elif event.position.distance_to(_press_position) < 5.0:
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
			_yaw += event.relative.x * 0.006
			_pitch = clampf(_pitch + event.relative.y * 0.006, -1.4, 1.4)
			_update_camera()
		pixel_hovered.emit(pixel_at(event.position))


# Map pixel under a point in this control, or (-1, -1) off the globe.
# SphereMesh lays its texture out with u along the angle atan2(x, z) and v
# from the top (+y, north) to the bottom, matching the flat map.
func pixel_at(local: Vector2) -> Vector2i:
	var scale_factor := Vector2(_viewport.size) / size
	var p := local * scale_factor
	var origin := _camera.project_ray_origin(p)
	var dir := _camera.project_ray_normal(p)
	# Ray-sphere intersection with the unit sphere at the origin.
	var b := origin.dot(dir)
	var c := origin.dot(origin) - 1.0
	var disc := b * b - c
	if disc < 0.0:
		return Vector2i(-1, -1)
	var t := -b - sqrt(disc)
	var hit := origin + dir * t
	var local_hit := _pivot.global_transform.affine_inverse() * hit
	var u := atan2(local_hit.x, local_hit.z) / TAU
	if u < 0.0:
		u += 1.0
	var v := acos(clampf(local_hit.y, -1.0, 1.0)) / PI
	return Vector2i(clampi(int(u * map_size.x), 0, map_size.x - 1), clampi(int(v * map_size.y), 0, map_size.y - 1))
