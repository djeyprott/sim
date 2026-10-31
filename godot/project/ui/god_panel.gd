class_name GodPanel
extends PanelContainer
## God mode: debug controls for meddling with the world.
##
## Global actions (eruption, climate forcing) apply at once. Local tools apply
## where you click on the map or globe; hovering shows the area they will
## touch. Every action is recorded in the run, so saves and replays keep it.

signal global_action(type: String, amount: float)
signal tool_changed
signal quicksave_requested
signal quickload_requested

const INK_MUTED := Color("#898781")
const GOOD := Color("#0ca30c")
const CRITICAL := Color("#e66767")

# Local tools: the intervention each one sends and how its strength maps to it.
# kind: which intervention; strength: label, min, max, step, default, unit;
# uses_radius / uses_years: which rows to show.
const TOOLS := [
	{"id": "inspect", "name": "Inspect only (no action)"},
	{"id": "drought", "name": "Drought", "kind": "climate", "uses_radius": true, "uses_years": true,
		"strength": ["Rainfall cut", 10, 100, 5, 50, "%"]},
	{"id": "wet", "name": "Wet years", "kind": "climate", "uses_radius": true, "uses_years": true,
		"strength": ["Extra rainfall", 10, 200, 10, 60, "%"]},
	{"id": "cold", "name": "Cold snap", "kind": "climate", "uses_radius": true, "uses_years": true,
		"strength": ["Cooling", 1, 15, 0.5, 5, " °C"]},
	{"id": "heat", "name": "Heat wave", "kind": "climate", "uses_radius": true, "uses_years": true,
		"strength": ["Warming", 1, 15, 0.5, 5, " °C"]},
	{"id": "plague", "name": "Plague", "kind": "plague", "uses_radius": true,
		"strength": ["Deaths", 5, 100, 5, 30, "%"]},
	{"id": "wipeout", "name": "Wipe out", "kind": "wipeout", "uses_radius": true},
	{"id": "settlers", "name": "Settlers (new group)", "kind": "settlers",
		"strength": ["People", 100, 20000, 100, 2000, ""]},
	{"id": "deplete", "name": "Deplete game", "kind": "game", "uses_radius": true},
	{"id": "restore", "name": "Restore game", "kind": "game", "uses_radius": true},
]

var _tool_option: OptionButton
var _radius: HSlider
var _strength: HSlider
var _years: HSlider
var _eruption: HSlider
var _forcing: HSlider
var _radius_row: Control
var _strength_row: Control
var _years_row: Control
var _strength_name: Label
var _strength_value: Label
var _forcing_hint: Label
var _status: Label


func _ready() -> void:
	var style := StyleBoxFlat.new()
	style.bg_color = Color("#242422")
	style.border_color = Color("#383835")
	style.set_border_width_all(1)
	style.set_corner_radius_all(6)
	style.set_content_margin_all(12)
	add_theme_stylebox_override("panel", style)
	custom_minimum_size.x = 320

	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 6)
	add_child(box)

	box.add_child(_heading("God mode", 16))
	box.add_child(_muted("Debug tools. Every action is recorded in the run."))

	# ---- Global
	box.add_child(HSeparator.new())
	box.add_child(_heading("Whole planet", 13))
	_eruption = _slider(1, 30, 0.5, 5)
	var eruption_value := _value_label()
	var eruption_row := _row("Eruption", _eruption, eruption_value)
	_eruption.value_changed.connect(func(v: float) -> void: eruption_value.text = "%.1f W/m²" % v)
	_eruption.value_changed.emit(_eruption.value)
	box.add_child(eruption_row)
	var erupt := Button.new()
	erupt.text = "Erupt now"
	erupt.pressed.connect(func() -> void: global_action.emit("eruption", _eruption.value))
	box.add_child(erupt)
	box.add_child(_muted("Pinatubo (1991) peaked near 3 W/m² of cooling."))

	_forcing = _slider(-10, 10, 0.5, 0)
	var forcing_value := _value_label()
	box.add_child(_row("Forcing", _forcing, forcing_value))
	_forcing_hint = _muted("")
	_forcing.value_changed.connect(func(v: float) -> void:
		forcing_value.text = "%+.1f W/m²" % v
		_forcing_hint.text = "About %+.1f °C at equilibrium, reached over decades. Doubled CO₂ is about +3.7 W/m²." % (v / 1.3))
	_forcing.value_changed.emit(_forcing.value)
	var set_forcing := Button.new()
	set_forcing.text = "Set climate forcing"
	set_forcing.pressed.connect(func() -> void: global_action.emit("forcing", _forcing.value))
	box.add_child(set_forcing)
	box.add_child(_forcing_hint)

	var bond := _slider(0.5, 5, 0.5, 1.5)
	var bond_value := _value_label()
	box.add_child(_row("Bond event", bond, bond_value))
	bond.value_changed.connect(func(v: float) -> void: bond_value.text = "%.1f °C" % v)
	bond.value_changed.emit(bond.value)
	var start_bond := Button.new()
	start_bond.text = "Start a Bond event"
	start_bond.pressed.connect(func() -> void: global_action.emit("bond", bond.value))
	box.add_child(start_bond)
	box.add_child(_muted("A 200-year Holocene cold spell: the north cools by up to this much and monsoons weaken (like the 8.2 and 4.2 ka events)."))

	# ---- Local tools
	box.add_child(HSeparator.new())
	box.add_child(_heading("Click the map", 13))
	_tool_option = OptionButton.new()
	for t in TOOLS:
		_tool_option.add_item(t["name"])
	_tool_option.item_selected.connect(func(_i: int) -> void:
		_configure_tool()
		tool_changed.emit())
	box.add_child(_tool_option)

	_radius = _slider(0, 3000, 50, 500)
	var radius_value := _value_label()
	_radius.value_changed.connect(func(v: float) -> void:
		radius_value.text = "%d km" % int(v)
		tool_changed.emit())
	_radius.value_changed.emit(_radius.value)
	_radius_row = _row("Radius", _radius, radius_value)
	box.add_child(_radius_row)

	_strength = _slider(0, 1, 1, 0)
	_strength_value = _value_label()
	_strength_name = Label.new()
	_strength.value_changed.connect(func(_v: float) -> void: _update_strength_label())
	_strength_row = _row_with_label(_strength_name, _strength, _strength_value)
	box.add_child(_strength_row)

	_years = _slider(1, 100, 1, 10)
	var years_value := _value_label()
	_years.value_changed.connect(func(v: float) -> void: years_value.text = "%d yr" % int(v))
	_years.value_changed.emit(_years.value)
	_years_row = _row("Duration", _years, years_value)
	box.add_child(_years_row)

	_status = Label.new()
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_status.add_theme_font_size_override("font_size", 12)
	box.add_child(_status)

	# ---- Saves
	box.add_child(HSeparator.new())
	var saves := HBoxContainer.new()
	var save := Button.new()
	save.text = "Quicksave (F5)"
	save.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	save.pressed.connect(func() -> void: quicksave_requested.emit())
	saves.add_child(save)
	var load := Button.new()
	load.text = "Quickload (F9)"
	load.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	load.pressed.connect(func() -> void: quickload_requested.emit())
	saves.add_child(load)
	box.add_child(saves)
	box.add_child(_muted("F2 hides this panel · Esc returns to inspect"))

	_configure_tool()


## The selected local tool's id ("inspect" when none).
func current_tool() -> String:
	return TOOLS[_tool_option.selected]["id"]


func set_tool(id: String) -> void:
	for i in TOOLS.size():
		if TOOLS[i]["id"] == id:
			_tool_option.selected = i
			_configure_tool()
			tool_changed.emit()
			return


## Radius of the area the current tool will touch (0 for single-cell tools).
func preview_radius() -> float:
	var t: Dictionary = TOOLS[_tool_option.selected]
	return _radius.value if t.get("uses_radius", false) else 0.0


## The intervention to send for a click: {type, radius, amount, amount2, years}.
func command() -> Dictionary:
	var t: Dictionary = TOOLS[_tool_option.selected]
	var s := _strength.value
	var c := {"type": t.get("kind", ""), "radius": preview_radius(), "amount": 0.0, "amount2": 0.0,
		"years": int(_years.value)}
	match t["id"]:
		"drought": c["amount2"] = -s / 100.0
		"wet": c["amount2"] = s / 100.0
		"cold": c["amount"] = -s
		"heat": c["amount"] = s
		"plague": c["amount"] = s / 100.0
		"settlers": c["amount"] = s
		"deplete": c["amount"] = 0.05
		"restore": c["amount"] = 1.0
	return c


func set_status(text: String, ok: bool) -> void:
	_status.text = text
	_status.add_theme_color_override("font_color", GOOD if ok else CRITICAL)


## Keep the forcing slider in step with the world (e.g. after a load).
func show_forcing(value: float) -> void:
	_forcing.set_value_no_signal(value)
	_forcing.value_changed.emit(value)


func _configure_tool() -> void:
	var t: Dictionary = TOOLS[_tool_option.selected]
	_radius_row.visible = t.get("uses_radius", false)
	_years_row.visible = t.get("uses_years", false)
	_strength_row.visible = t.has("strength")
	if t.has("strength"):
		var spec: Array = t["strength"]
		_strength_name.text = spec[0]
		_strength.min_value = spec[1]
		_strength.max_value = spec[2]
		_strength.step = spec[3]
		_strength.set_value_no_signal(spec[4])
		_update_strength_label()
	if t["id"] == "inspect":
		set_status("Pick a tool, then click the map.", true)
	else:
		set_status("Click the map to apply %s." % t["name"].to_lower(), true)


func _update_strength_label() -> void:
	var t: Dictionary = TOOLS[_tool_option.selected]
	if not t.has("strength"):
		return
	var unit: String = t["strength"][5]
	var v := _strength.value
	_strength_value.text = ("%d" % int(v) if is_equal_approx(v, roundf(v)) else "%.1f" % v) + unit


# ---- small builders

func _slider(lo: float, hi: float, step: float, value: float) -> HSlider:
	var s := HSlider.new()
	s.min_value = lo
	s.max_value = hi
	s.step = step
	s.value = value
	s.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	s.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	return s


func _value_label() -> Label:
	var l := Label.new()
	l.custom_minimum_size.x = 78
	l.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	return l


func _row(name: String, slider: HSlider, value: Label) -> Control:
	var l := Label.new()
	l.text = name
	return _row_with_label(l, slider, value)


func _row_with_label(name_label: Label, slider: HSlider, value: Label) -> Control:
	var row := HBoxContainer.new()
	name_label.custom_minimum_size.x = 92
	row.add_child(name_label)
	row.add_child(slider)
	row.add_child(value)
	return row


func _heading(text: String, size: int) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_size_override("font_size", size)
	return l


func _muted(text: String) -> Label:
	var l := Label.new()
	l.text = text
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	l.add_theme_font_size_override("font_size", 11)
	l.add_theme_color_override("font_color", INK_MUTED)
	return l
