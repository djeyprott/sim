class_name PyramidChart
extends Control
## Age-sex pyramid: men to the left, women to the right, 5-year age groups
## from 0-4 at the bottom. Ages 80 and over are pooled (forager populations
## have very few). Bars show each group's share of the whole population.

const INK := Color("#f0efec")
const INK_MUTED := Color("#898781")
const GRID := Color("#2c2c2a")
const MALE := Color("#3987e5")    # categorical slot 1 (dark mode)
const FEMALE := Color("#d95926")  # categorical slot 2 (dark mode)
const SHOWN_BINS := 17  # 0-4 ... 75-79, then 80+

var _female := PackedInt64Array()
var _male := PackedInt64Array()
var _font: Font


func _ready() -> void:
	_font = get_theme_default_font()


func set_pyramid(data: Dictionary) -> void:
	_female = data.get("female", PackedInt64Array())
	_male = data.get("male", PackedInt64Array())
	queue_redraw()


func _bins(values: PackedInt64Array) -> Array[float]:
	var out: Array[float] = []
	for b in SHOWN_BINS:
		out.append(float(values[b]) if b < values.size() else 0.0)
	for b in range(SHOWN_BINS, values.size()):
		out[SHOWN_BINS - 1] += float(values[b])
	return out


func _draw() -> void:
	if _female.is_empty():
		return
	var f := _bins(_female)
	var m := _bins(_male)
	var total := 0.0
	var biggest := 0.0
	for b in SHOWN_BINS:
		total += f[b] + m[b]
		biggest = maxf(biggest, maxf(f[b], m[b]))
	if total <= 0.0:
		return

	var label_w := 40.0
	var top := 18.0
	var bottom := 16.0
	var mid := label_w + (size.x - label_w) / 2.0
	var half := (size.x - label_w) / 2.0 - 4.0
	var row_h := (size.y - top - bottom) / SHOWN_BINS
	var scale := half / biggest

	draw_string(_font, Vector2(label_w, 12), "Men", HORIZONTAL_ALIGNMENT_LEFT, -1, 11, MALE)
	draw_string(_font, Vector2(size.x - 50, 12), "Women", HORIZONTAL_ALIGNMENT_RIGHT, 50, 11, FEMALE)
	draw_line(Vector2(mid, top), Vector2(mid, size.y - bottom), GRID, 1.0)

	for b in SHOWN_BINS:
		var y := size.y - bottom - (b + 1) * row_h
		var h := maxf(1.0, row_h - 2.0)  # 2 px gap between rows
		draw_rect(Rect2(mid - m[b] * scale, y + 1, m[b] * scale, h), MALE)
		draw_rect(Rect2(mid, y + 1, f[b] * scale, h), FEMALE)
		if b % 4 == 0 or b == SHOWN_BINS - 1:
			var label := "80+" if b == SHOWN_BINS - 1 else str(b * 5)
			draw_string(_font, Vector2(0, y + row_h * 0.5 + 4), label, HORIZONTAL_ALIGNMENT_RIGHT, label_w - 6, 10, INK_MUTED)

	# Scale: the widest bar's share of the population.
	var share := 100.0 * biggest / total
	draw_string(_font, Vector2(label_w, size.y - 2), "%.1f%%" % share, HORIZONTAL_ALIGNMENT_LEFT, -1, 10, INK_MUTED)
	draw_string(_font, Vector2(size.x - 60, size.y - 2), "%.1f%%" % share, HORIZONTAL_ALIGNMENT_RIGHT, 60, 10, INK_MUTED)
