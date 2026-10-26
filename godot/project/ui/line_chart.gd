class_name LineChart
extends Control
## A small line chart for yearly series: recessive grid, 2 px lines, direct
## labels at the line ends, and a crosshair with a tooltip on hover.
##
## set_data(x, series) where x is a PackedInt64Array of years and each series is
## {"name": String, "values": PackedFloat64Array, "color": Color,
##  "width": float (optional, default 2)}.

const INK := Color("#f0efec")
const INK_SECONDARY := Color("#c3c2b7")
const INK_MUTED := Color("#898781")
const GRID := Color("#2c2c2a")
const BASELINE := Color("#383835")
const TOOLTIP_BG := Color("#2c2c2a")

@export var title := ""
@export var value_format := "compact"  # "compact" (12.1M) or "signed1" (+0.3)
@export var unit := ""
@export var zero_line := false  # draw a stronger line at y = 0

var _x := PackedInt64Array()
var _series: Array = []
var _hover_index := -1
var _font: Font
var _plot := Rect2()


func _ready() -> void:
	_font = get_theme_default_font()
	mouse_filter = Control.MOUSE_FILTER_STOP
	mouse_exited.connect(func() -> void:
		_hover_index = -1
		queue_redraw())


func set_data(x: PackedInt64Array, series: Array) -> void:
	_x = x
	_series = series
	queue_redraw()


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and _x.size() > 1 and _plot.size.x > 0:
		var t := clampf((event.position.x - _plot.position.x) / _plot.size.x, 0.0, 1.0)
		var target := lerpf(float(_x[0]), float(_x[_x.size() - 1]), t)
		var best := 0
		for i in _x.size():
			if absf(float(_x[i]) - target) < absf(float(_x[best]) - target):
				best = i
		if best != _hover_index:
			_hover_index = best
			queue_redraw()


func _format(v: float) -> String:
	if value_format == "signed1":
		return ("0" if absf(v) < 0.05 else "%+.1f" % v) + unit
	var a := absf(v)
	if a >= 1e9:
		return "%.1fB%s" % [v / 1e9, unit]
	if a >= 1e6:
		return "%.1fM%s" % [v / 1e6, unit]
	if a >= 1e3:
		return "%.0fk%s" % [v / 1e3, unit]
	return "%.0f%s" % [v, unit]


func _draw() -> void:
	var size_ := size
	draw_string(_font, Vector2(0, 14), title, HORIZONTAL_ALIGNMENT_LEFT, -1, 13, INK)
	if _x.size() < 2 or _series.is_empty():
		draw_string(_font, Vector2(0, 36), "Press Play to build a history.", HORIZONTAL_ALIGNMENT_LEFT, -1, 12, INK_MUTED)
		return

	# Value range across all series, with a little headroom.
	var lo := INF
	var hi := -INF
	for s in _series:
		for v in s["values"]:
			lo = minf(lo, v)
			hi = maxf(hi, v)
	if zero_line:
		# Symmetric around zero, rounded up to a half degree, so 0 sits mid-chart.
		var m := maxf(0.5, ceilf(maxf(absf(lo), absf(hi)) * 2.0) / 2.0)
		lo = -m
		hi = m
	else:
		lo = 0.0 if lo >= 0.0 else lo
		if hi - lo < 1e-9:
			hi = lo + 1.0
		hi += (hi - lo) * 0.08

	var left := 52.0
	var right := 92.0  # room for direct labels
	_plot = Rect2(left, 24.0, maxf(10.0, size_.x - left - right), maxf(10.0, size_.y - 24.0 - 20.0))
	var x0 := float(_x[0])
	var x1 := float(_x[_x.size() - 1])
	var to_screen := func(xv: float, yv: float) -> Vector2:
		return Vector2(_plot.position.x + (xv - x0) / maxf(1.0, x1 - x0) * _plot.size.x,
			_plot.end.y - (yv - lo) / (hi - lo) * _plot.size.y)

	# Grid: three horizontal lines with value labels; x labels at the ends.
	for i in 3:
		var yv := lo + (hi - lo) * i / 2.0
		var p: Vector2 = to_screen.call(x0, yv)
		draw_line(Vector2(_plot.position.x, p.y), Vector2(_plot.end.x, p.y), GRID, 1.0)
		draw_string(_font, Vector2(0, p.y + 4), _format(yv), HORIZONTAL_ALIGNMENT_LEFT, left - 6, 11, INK_MUTED)
	if zero_line:
		var z: Vector2 = to_screen.call(x0, 0.0)
		draw_line(Vector2(_plot.position.x, z.y), Vector2(_plot.end.x, z.y), BASELINE, 1.0)
	draw_string(_font, Vector2(_plot.position.x, size_.y - 4), "year %d" % _x[0], HORIZONTAL_ALIGNMENT_LEFT, -1, 11, INK_MUTED)
	draw_string(_font, Vector2(_plot.end.x - 80, size_.y - 4), "year %d" % _x[_x.size() - 1], HORIZONTAL_ALIGNMENT_RIGHT, 80, 11, INK_MUTED)

	# Series, with a direct label at each line's end.
	var label_ys: Array[float] = []
	for s in _series:
		var values: PackedFloat64Array = s["values"]
		var points := PackedVector2Array()
		for i in values.size():
			points.append(to_screen.call(float(_x[i]), values[i]))
		draw_polyline(points, s["color"], s.get("width", 2.0), true)
		var end_y := points[points.size() - 1].y
		for y in label_ys:  # nudge labels apart
			if absf(end_y - y) < 14.0:
				end_y = y + (14.0 if end_y >= y else -14.0)
		label_ys.append(end_y)
		draw_string(_font, Vector2(_plot.end.x + 6, end_y + 4), s["name"], HORIZONTAL_ALIGNMENT_LEFT, right - 8, 11, INK_SECONDARY)

	# Hover crosshair and tooltip.
	if _hover_index >= 0 and _hover_index < _x.size():
		var hx: float = to_screen.call(float(_x[_hover_index]), lo).x
		draw_line(Vector2(hx, _plot.position.y), Vector2(hx, _plot.end.y), INK_MUTED, 1.0)
		var lines := ["Year %d" % _x[_hover_index]]
		for s in _series:
			var v: float = s["values"][_hover_index]
			var dot: Vector2 = to_screen.call(float(_x[_hover_index]), v)
			draw_circle(dot, 4.0, s["color"])
			lines.append("%s: %s" % [s["name"], _format(v)])
		var box_w := 170.0
		var box_h := 8.0 + 15.0 * lines.size()
		var bx := hx + 10.0 if hx + 10.0 + box_w < size_.x else hx - 10.0 - box_w
		draw_rect(Rect2(bx, _plot.position.y, box_w, box_h), TOOLTIP_BG)
		for i in lines.size():
			draw_string(_font, Vector2(bx + 8, _plot.position.y + 16 + 15 * i), lines[i], HORIZONTAL_ALIGNMENT_LEFT, box_w - 12, 12, INK)
