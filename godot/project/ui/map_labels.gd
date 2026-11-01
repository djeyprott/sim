class_name MapLabels
extends Control
## Names of states, empires and other large realms written over the map: on
## the flat map (as a child of the map image, so it pans and zooms with it)
## or over the globe (`globe` set), where it follows the globe as it turns.
## Each name is sized to its realm's land and dropped if it would not fit or
## would overlap a larger realm's name.

const KM_AROUND := 40075.0  # the equator, the flat map's full width
const PLANET_RADIUS_KM := 6371.0
const MIN_FONT := 10
const MAX_FONT := 30
const INK := Color("#f0efec")
const OUTLINE := Color(0.08, 0.08, 0.07, 0.85)

var globe: GlobeView = null
var _labels: Array = []


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE


func set_labels(labels: Array) -> void:
	_labels = labels
	queue_redraw()


func _process(_delta: float) -> void:
	if globe != null and is_visible_in_tree() and not _labels.is_empty():
		queue_redraw()  # the globe may have turned


func _draw() -> void:
	var font := get_theme_default_font()
	var taken: Array[Rect2] = []
	for label in _labels:
		var at := Vector2.ZERO
		var km_px := 0.0  # pixels per km here
		if globe != null:
			var p: Vector3 = globe.screen_of(Vector2(label["u"], label["v"]))
			if p.z < 0.25:
				continue  # on the far side, or too close to the edge to read
			at = Vector2(p.x, p.y)
			km_px = globe.screen_radius() / PLANET_RADIUS_KM * p.z
		else:
			at = Vector2(float(label["u"]) * size.x, float(label["v"]) * size.y)
			km_px = size.x / KM_AROUND
		var extent := sqrt(float(label["area_km2"])) * km_px
		var text: String = label["name"]
		if label["level"] == "empire":
			text = text.to_upper()
		if extent < 2.2 * MIN_FONT:
			continue  # too small on screen to carry a name
		var font_size := clampi(int(1.6 * extent / maxf(4.0, text.length())), MIN_FONT, MAX_FONT)
		var width := font.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x
		var box := Rect2(at - Vector2(width * 0.5, font_size * 0.6), Vector2(width, font_size * 1.2))
		# Keep the capital's dot in view: a name over it moves just below.
		var seat := _place(Vector2(label.get("capital_u", -1.0), label.get("capital_v", -1.0)))
		if seat.z > 0.0 and box.grow(4.0).has_point(Vector2(seat.x, seat.y)):
			at.y = seat.y + font_size * 0.6 + 6.0
			box.position.y = at.y - font_size * 0.6
		var clash := false
		for other in taken:
			if other.intersects(box):
				clash = true
				break
		if clash:
			continue
		taken.append(box)
		var baseline := Vector2(box.position.x, at.y + font_size * 0.35)
		draw_string_outline(font, baseline, text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, maxi(3, font_size / 4), OUTLINE)
		draw_string(font, baseline, text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, INK)


# Where a map point (u, v) shows in this control: (x, y, facing), facing <= 0
# when it does not show.
func _place(uv: Vector2) -> Vector3:
	if uv.x < 0.0:
		return Vector3(0, 0, -1)
	if globe != null:
		return globe.screen_of(uv)
	return Vector3(uv.x * size.x, uv.y * size.y, 1.0)
