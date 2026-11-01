extends Control
## histsim viewer: a flat map or globe with switchable map modes, a legend,
## playback controls, world charts, a cell inspector with an age pyramid, and
## a god-mode panel for meddling with the world.
##
## Hovering shows a cell in the inspector; clicking keeps it there (so the
## panel can be scrolled) until it is clicked again or Esc is pressed. The flat
## map zooms with the wheel around the cursor and pans by dragging.
##
## All simulation and map rendering happens in C++ (the HistSimWorld class from
## the histsim GDExtension). This script only lays out the UI and forwards input.

const MAP_WIDTH := 2048
const MAP_HEIGHT := 1024
const SPEEDS := [1, 10, 100, 1000]  # simulated years per real second
const LEVELS := [4, 5, 6, 7]
const LEVEL_NAMES := ["Coarse (2.6k cells)", "Normal (10k cells)", "Fine (41k cells)", "Detailed (164k cells)"]
const STEP_BUDGET_MSEC := 12.0  # max time per frame spent simulating
const RENDER_INTERVAL := 1.0 / 15.0  # max map redraws per second while playing
const CHART_INTERVAL := 0.25  # chart refreshes per second while playing
const DYNAMIC_LEGENDS := ["language", "language_family", "culture", "polity"]
const SIDE_PANEL_WIDTH := 360
const CELL_KEY_WIDTH := 118  # fits the longest inspector key
const CHART_POINTS := 400
const QUICKSAVE_PATH := "user://quicksave.hsim"
const MAX_ZOOM := 16.0
const ZOOM_STEP := 1.25
const DRAG_THRESHOLD := 5.0  # pixels a press may move and still count as a click

var world: Object  # HistSimWorld; untyped so a missing extension gives a clear message
var mode_id := "terrain"
var playing := false
var speed_index := 1
var years_due := 0.0
var map_dirty := true
var render_cooldown := 0.0
var hover_cell := -1
var selected_cell := -1  # the cell kept in the inspector by a click (-1 none)
var map_zoom := 1.0
var map_offset := Vector2.ZERO  # top-left of the map image within the map view
var map_press := Vector2.ZERO
var map_pressed := false
var map_dragging := false
var map_texture: ImageTexture
var rate_years := 0  # years simulated since the last speed sample
var rate_time := 0.0
var chart_cooldown := 0.0
var charts_dirty := true
var showing_globe := false
var highlight_cell := -1  # cell the god-mode preview outline is centred on

# UI nodes
var map_view: Control  # clips the zoomed map
var map_rect: TextureRect
var year_label: Label
var population_label: Label
var play_button: Button
var speed_option: OptionButton
var actual_speed_label: Label
var seed_spin: SpinBox
var level_option: OptionButton
var coastline_check: CheckBox
var river_check: CheckBox
var border_check: CheckBox
var route_check: CheckBox
var migration_check: CheckBox
var trait_option: OptionButton
var species_label: Label
var mode_buttons := {}
var legend_title: Label
var legend_bar: TextureRect
var legend_ticks: HBoxContainer
var legend_swatches: VBoxContainer
var cell_grid: GridContainer
var cell_hint: Label
var status_label: Label
var globe: GlobeView
var map_button: Button
var globe_button: Button
var population_chart: LineChart
var climate_chart: LineChart
var pyramid: PyramidChart
var god_panel: GodPanel
var god_button: Button


func _ready() -> void:
	_build_ui()
	if not ClassDB.class_exists("HistSimWorld"):
		status_label.text = "The simulation library (histsim_godot) did not load. Check that it sits next to the executable."
		push_error("HistSimWorld class not found: GDExtension failed to load")
		return
	world = ClassDB.instantiate("HistSimWorld")
	world.set_map_size(MAP_WIDTH, MAP_HEIGHT)
	_new_world()


# ---------------------------------------------------------------- simulation

func _new_world() -> void:
	var level: int = LEVELS[level_option.selected]
	if not world.create_world(int(seed_spin.value), level, 0.7):
		status_label.text = "Could not create world: " + world.get_last_error()
		return
	world.set_map_size(MAP_WIDTH, MAP_HEIGHT)
	_after_world_change()
	status_label.text = "New world: seed %d, %s cells, forager capacity %s people." % [
		world.get_seed(), _commas(world.get_cell_count()), _commas(int(world.get_forager_capacity()))]


# Reset view state after the world is replaced (new world or quickload).
func _after_world_change() -> void:
	years_due = 0.0
	hover_cell = -1
	selected_cell = -1
	highlight_cell = -1
	world.set_highlight(-1, 0.0)
	god_panel.show_forcing(world.get_imposed_forcing())
	god_panel.set_diseases(world.get_disease_names())
	_clear_cell_info()
	_update_legend()
	charts_dirty = true
	map_dirty = true
	render_cooldown = 0.0
	_update_summary()


func _process(delta: float) -> void:
	if world == null:
		return
	if playing:
		var speed: int = SPEEDS[speed_index]
		# Cap the backlog so a slow machine falls behind gracefully instead of stalling.
		years_due = minf(years_due + delta * speed, speed * 0.5 + 1.0)
		var wanted := int(years_due)
		if wanted > 0:
			var done: int = world.step_for_budget(wanted, STEP_BUDGET_MSEC)
			years_due -= done
			rate_years += done
			if done > 0:
				map_dirty = true
				charts_dirty = true
				_update_summary()
		# Show the real speed when the machine cannot keep up with the setting.
		rate_time += delta
		if rate_time >= 0.5:
			var actual := rate_years / rate_time
			actual_speed_label.text = "running at %s years/s" % _commas(int(actual)) if actual < speed * 0.9 else ""
			rate_years = 0
			rate_time = 0.0
	render_cooldown -= delta
	if map_dirty and render_cooldown <= 0.0:
		_render_map()
	chart_cooldown -= delta
	if charts_dirty and chart_cooldown <= 0.0:
		_update_charts()


func _render_map() -> void:
	var image: Image = world.render_map(mode_id, coastline_check.button_pressed, river_check.button_pressed,
		border_check.button_pressed, route_check.button_pressed, migration_check.button_pressed)
	if image == null:
		status_label.text = "Map render failed: " + world.get_last_error()
		return
	if map_texture == null or map_texture.get_size() != Vector2(image.get_size()):
		map_texture = ImageTexture.create_from_image(image)
		map_rect.texture = map_texture
		globe.set_texture(map_texture)
		_layout_map()
	else:
		map_texture.update(image)
	map_dirty = false
	render_cooldown = RENDER_INTERVAL
	if mode_id in DYNAMIC_LEGENDS:
		_update_legend()  # lists the largest groups, which change as the run goes on
	if _inspected_cell() >= 0:
		_refresh_inspector()


func _update_summary() -> void:
	year_label.text = "Year %s" % _commas(world.get_year())
	var capacity: float = world.get_capacity()
	var share: float = 100.0 * world.get_population() / capacity if capacity > 0.0 else 0.0
	var peoples: Dictionary = world.get_people_counts()
	population_label.text = "Population %s (%.0f%% of capacity)  ·  %s cells inhabited  ·  %s languages in %s families  ·  climate %+.2f °C" % [
		_commas(world.get_population()), share, _commas(world.get_inhabited_cells()),
		_commas(peoples["languages"]), _commas(peoples["families"]), world.get_global_anomaly()]


func _update_charts() -> void:
	var h: Dictionary = world.get_history(CHART_POINTS)
	population_chart.set_data(h["year"], [
		{"name": "Population", "values": _to_floats(h["population"]), "color": Color("#3987e5")},
		{"name": "Farmers", "values": h["farmers"], "color": Color("#7fa33a")},
		{"name": "Capacity", "values": h["capacity"], "color": Color("#898781"), "width": 1.0},
	])
	var marks: PackedInt64Array = world.get_event_years("intervention")
	marks.append_array(world.get_event_years("domestication"))
	population_chart.markers = marks
	climate_chart.markers = world.get_event_years("eruption")
	climate_chart.spans = world.get_bond_spans()
	climate_chart.set_data(h["year"], [
		{"name": "Global", "values": h["anomaly"], "color": Color("#d95926")},
	])
	charts_dirty = false
	chart_cooldown = CHART_INTERVAL


func _to_floats(values: PackedInt64Array) -> PackedFloat64Array:
	var out := PackedFloat64Array()
	out.resize(values.size())
	for i in values.size():
		out[i] = float(values[i])
	return out


func _set_view(globe_view: bool) -> void:
	showing_globe = globe_view
	globe.visible = globe_view
	map_view.visible = not globe_view
	if not globe_view:
		_layout_map()
	map_button.button_pressed = not globe_view
	globe_button.button_pressed = globe_view
	hover_cell = -1
	_refresh_inspector()
	_update_preview(-1)


func _set_playing(value: bool) -> void:
	playing = value
	play_button.text = "Pause" if playing else "Play"
	years_due = 0.0
	rate_years = 0
	rate_time = 0.0
	actual_speed_label.text = ""


func _set_mode(id: String) -> void:
	mode_id = id
	if mode_buttons.has(id):
		mode_buttons[id].button_pressed = true
		if trait_option != null:
			trait_option.select(0)
	elif id.begins_with("trait:"):
		# A trait mode: no grid button is current.
		for button in mode_buttons.values():
			button.button_pressed = false
	_update_legend()
	map_dirty = true
	render_cooldown = 0.0


# ---------------------------------------------------------------- input

func _unhandled_input(event: InputEvent) -> void:
	if world == null or not (event is InputEventKey) or not event.pressed or event.echo:
		return
	var key: int = event.keycode
	if key == KEY_SPACE:
		_set_playing(not playing)
	elif (key >= KEY_1 and key <= KEY_9) or key == KEY_0:
		var modes: Array = world.get_modes()
		var index := 9 if key == KEY_0 else key - KEY_1
		if index < modes.size():
			_set_mode(modes[index]["id"])
	elif key == KEY_PERIOD and not playing:
		world.step_years(1)
		map_dirty = true
		charts_dirty = true
		_update_summary()
	elif key == KEY_G:
		_set_view(not showing_globe)
	elif key == KEY_F2:
		_set_god_mode(not god_panel.visible)
	elif key == KEY_F5:
		_quicksave()
	elif key == KEY_F9:
		_quickload()
	elif key == KEY_ESCAPE and god_panel.visible and god_panel.current_tool() != "inspect":
		god_panel.set_tool("inspect")
	elif key == KEY_ESCAPE and selected_cell >= 0:
		_select(-1)
	elif key == KEY_HOME:
		_reset_map_view()
	else:
		return
	get_viewport().set_input_as_handled()


func _on_map_input(event: InputEvent) -> void:
	if world == null or map_texture == null:
		return
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP and event.pressed:
			_zoom_map(ZOOM_STEP, event.position)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN and event.pressed:
			_zoom_map(1.0 / ZOOM_STEP, event.position)
		elif event.button_index in [MOUSE_BUTTON_LEFT, MOUSE_BUTTON_MIDDLE, MOUSE_BUTTON_RIGHT]:
			if event.pressed:
				map_pressed = true
				map_dragging = false
				map_press = event.position
			else:
				var was_click: bool = map_pressed and not map_dragging and event.button_index == MOUSE_BUTTON_LEFT
				map_pressed = false
				map_dragging = false
				if was_click:
					_on_cell_clicked(_cell_under(event.position))
		return
	if not (event is InputEventMouseMotion):
		return
	if map_pressed:
		if not map_dragging and event.position.distance_to(map_press) >= DRAG_THRESHOLD:
			map_dragging = true
		if map_dragging:
			map_offset += event.relative
			_layout_map()
	_on_cell_hovered(_cell_under(event.position))


func _on_globe_pixel(pixel: Vector2i) -> void:
	if world == null or map_texture == null:
		return
	_on_cell_hovered(world.cell_at(pixel.x, pixel.y) if pixel.x >= 0 else -1)


func _on_globe_click(pixel: Vector2i) -> void:
	if world != null and map_texture != null:
		_on_cell_clicked(world.cell_at(pixel.x, pixel.y))


func _on_cell_hovered(cell: int) -> void:
	if cell == hover_cell:
		return
	hover_cell = cell
	if selected_cell < 0:
		_refresh_inspector()
	_update_preview(cell)


# A click applies the god-mode tool if one is chosen; otherwise it keeps the
# cell in the inspector (or lets it go if it was already kept).
func _on_cell_clicked(cell: int) -> void:
	if cell < 0:
		return
	if _god_tool_active():
		_apply_tool(cell)
	else:
		_select(-1 if cell == selected_cell else cell)


func _select(cell: int) -> void:
	selected_cell = cell
	_refresh_inspector()
	highlight_cell = -2  # force the outline to move
	_update_preview(hover_cell)


# The cell the inspector shows: the kept one, else the one under the mouse.
func _inspected_cell() -> int:
	return selected_cell if selected_cell >= 0 else hover_cell


func _refresh_inspector() -> void:
	var cell := _inspected_cell()
	if cell >= 0:
		_show_cell(cell)
	else:
		_clear_cell_info()
	cell_hint.text = ("Kept by a click: click it again or press Esc to let go." if selected_cell >= 0
		else "Hover over the map to look at a cell; click to keep it here.")
	cell_hint.visible = true


# ---------------------------------------------------------------- flat map view

# Size and place the map image in the view: fitted to it at zoom 1, larger
# when zoomed, and never pulled away from the view's edges.
func _layout_map() -> void:
	if map_view == null or map_rect == null:
		return
	var view := map_view.size
	var aspect := float(MAP_WIDTH) / float(MAP_HEIGHT)
	var fit := Vector2(view.x, view.x / aspect)
	if fit.y > view.y:
		fit = Vector2(view.y * aspect, view.y)
	var map_size := fit * map_zoom
	for axis in 2:
		if map_size[axis] <= view[axis]:
			map_offset[axis] = (view[axis] - map_size[axis]) * 0.5
		else:
			map_offset[axis] = clampf(map_offset[axis], view[axis] - map_size[axis], 0.0)
	map_rect.position = map_offset
	map_rect.size = map_size


# Zoom by a factor, keeping the map point under `at` (view coordinates) still.
func _zoom_map(factor: float, at: Vector2) -> void:
	var zoom := clampf(map_zoom * factor, 1.0, MAX_ZOOM)
	if zoom == map_zoom:
		return
	if map_rect.size.x <= 0.0 or map_rect.size.y <= 0.0:
		_layout_map()
	var point := (at - map_offset) / map_rect.size  # 0-1 across the map
	map_zoom = zoom
	_layout_map()
	map_offset = at - point * map_rect.size
	_layout_map()


func _reset_map_view() -> void:
	map_zoom = 1.0
	_layout_map()


# ---------------------------------------------------------------- god mode

func _god_tool_active() -> bool:
	return god_panel.visible and god_panel.current_tool() != "inspect"


func _set_god_mode(on: bool) -> void:
	god_panel.visible = on
	god_button.button_pressed = on
	_update_preview(hover_cell)


# Outline the cells the current tool would touch around the hovered cell, or
# else the cell kept in the inspector.
func _update_preview(cell: int) -> void:
	if world == null:
		return
	var tool := _god_tool_active()
	var target := cell if tool else selected_cell
	if target == highlight_cell and (target < 0 or not tool):
		return
	highlight_cell = target
	world.set_highlight(target, god_panel.preview_radius() if tool else 0.0)
	map_dirty = true
	render_cooldown = 0.0


func _apply_tool(cell: int) -> void:
	if not _god_tool_active():
		return
	var c: Dictionary = god_panel.command()
	_report(world.apply_intervention(c["type"], cell, c["radius"], c["amount"], c["amount2"], c["years"]))


func _on_global_action(type: String, amount: float) -> void:
	var years := 200 if type == "bond" else 0
	_report(world.apply_intervention(type, -1, 0.0, amount, 0.0, years))


func _report(result: Dictionary) -> void:
	god_panel.set_status(result["message"], result["ok"])
	status_label.text = ("Year %s: " % _commas(world.get_year())) + result["message"]
	if result["ok"]:
		map_dirty = true
		charts_dirty = true
		render_cooldown = 0.0
		chart_cooldown = 0.0
		_update_summary()
		_refresh_inspector()


func _quicksave() -> void:
	var path := ProjectSettings.globalize_path(QUICKSAVE_PATH)
	if world.save_snapshot(path):
		god_panel.set_status("Saved year %s." % _commas(world.get_year()), true)
		status_label.text = "Quicksaved year %s to %s" % [_commas(world.get_year()), path]
	else:
		god_panel.set_status("Save failed: " + world.get_last_error(), false)


func _quickload() -> void:
	var path := ProjectSettings.globalize_path(QUICKSAVE_PATH)
	if not FileAccess.file_exists(QUICKSAVE_PATH):
		god_panel.set_status("No quicksave yet: press F5 first.", false)
		return
	if not world.load_snapshot(path):
		god_panel.set_status("Load failed: " + world.get_last_error(), false)
		return
	_set_playing(false)
	seed_spin.set_value_no_signal(world.get_seed())
	level_option.selected = maxi(0, LEVELS.find(world.get_level()))
	_after_world_change()
	_clear_cell_info()
	map_dirty = true
	charts_dirty = true
	render_cooldown = 0.0
	chart_cooldown = 0.0
	_update_summary()
	god_panel.set_status("Loaded year %s." % _commas(world.get_year()), true)
	status_label.text = "Quickloaded year %s." % _commas(world.get_year())


func _on_map_mouse_exited() -> void:
	map_pressed = false
	map_dragging = false
	_on_cell_hovered(-1)


# Map a point in the map view to a map pixel, accounting for zoom and pan,
# then ask the extension which cell is there.
func _cell_under(local: Vector2) -> int:
	var tex_size := Vector2(MAP_WIDTH, MAP_HEIGHT)
	if map_rect.size.x <= 0.0 or map_rect.size.y <= 0.0:
		return -1
	var pixel := (local - map_offset) / map_rect.size * tex_size
	if pixel.x < 0 or pixel.y < 0 or pixel.x >= tex_size.x or pixel.y >= tex_size.y:
		return -1
	return world.cell_at(int(pixel.x), int(pixel.y))


# ---------------------------------------------------------------- panels

func _update_legend() -> void:
	var legend: Dictionary = world.get_legend(mode_id)
	legend_title.text = legend.get("title", "")
	var ramp: Array = legend.get("ramp", [])
	legend_bar.visible = ramp.size() >= 2
	legend_ticks.visible = ramp.size() >= 2

	if ramp.size() < 2:
		ramp = [{"label": "", "color": Color.BLACK}, {"label": "", "color": Color.BLACK}]
	var gradient := Gradient.new()
	var offsets := PackedFloat32Array()
	var colors := PackedColorArray()
	for i in ramp.size():
		offsets.append(float(i) / maxf(1.0, ramp.size() - 1))
		colors.append(ramp[i]["color"])
	gradient.offsets = offsets
	gradient.colors = colors
	var texture := GradientTexture1D.new()
	texture.gradient = gradient
	legend_bar.texture = texture

	for child in legend_ticks.get_children():
		child.queue_free()
	for i in ramp.size():
		var tick := Label.new()
		tick.text = ramp[i]["label"]
		tick.add_theme_font_size_override("font_size", 11)
		tick.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		if i == 0:
			tick.horizontal_alignment = HORIZONTAL_ALIGNMENT_LEFT
		elif i == ramp.size() - 1:
			tick.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		else:
			tick.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		legend_ticks.add_child(tick)

	for child in legend_swatches.get_children():
		child.queue_free()
	for swatch in legend.get("swatches", []):
		var row := HBoxContainer.new()
		var chip := ColorRect.new()
		chip.color = swatch["color"]
		chip.custom_minimum_size = Vector2(14, 14)
		chip.size_flags_vertical = Control.SIZE_SHRINK_CENTER
		row.add_child(chip)
		var label := Label.new()
		label.text = swatch["label"]
		label.add_theme_font_size_override("font_size", 12)
		label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		row.add_child(label)
		legend_swatches.add_child(row)


func _show_cell(cell: int) -> void:
	_clear_cell_info()
	cell_hint.visible = false
	for pair in world.describe_cell(cell):
		var key := Label.new()
		key.text = pair[0]
		key.add_theme_color_override("font_color", Color(0.76, 0.76, 0.72))
		key.custom_minimum_size.x = CELL_KEY_WIDTH
		key.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
		cell_grid.add_child(key)
		var value := Label.new()
		value.text = pair[1]
		value.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		value.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		cell_grid.add_child(value)
	var native: PackedStringArray = world.get_native_species(cell)
	species_label.visible = native.size() > 0
	species_label.text = "Wild species: " + ", ".join(native)
	var p: Dictionary = world.get_cell_pyramid(cell)
	var people := 0
	for n in p["female"]:
		people += n
	for n in p["male"]:
		people += n
	pyramid.visible = people > 0
	if people > 0:
		pyramid.set_pyramid(p)


func _clear_cell_info() -> void:
	for child in cell_grid.get_children():
		cell_grid.remove_child(child)
		child.queue_free()
	cell_hint.visible = true
	if species_label != null:
		species_label.visible = false
	if pyramid != null:
		pyramid.visible = false


# ---------------------------------------------------------------- layout

func _build_ui() -> void:
	var background := ColorRect.new()
	background.color = Color("#1a1a19")
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(background)

	var outer := MarginContainer.new()
	outer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for side in ["left", "right", "top", "bottom"]:
		outer.add_theme_constant_override("margin_" + side, 10)
	add_child(outer)

	var root := VBoxContainer.new()
	root.add_theme_constant_override("separation", 8)
	outer.add_child(root)

	root.add_child(_build_top_bar())

	var body := HBoxContainer.new()
	body.size_flags_vertical = Control.SIZE_EXPAND_FILL
	body.add_theme_constant_override("separation", 12)
	root.add_child(body)
	body.add_child(_build_side_panel())

	var column := VBoxContainer.new()
	column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	column.add_theme_constant_override("separation", 6)
	body.add_child(column)

	var view_bar := HBoxContainer.new()
	var view_group := ButtonGroup.new()
	map_button = Button.new()
	map_button.text = "Map"
	map_button.tooltip_text = "Scroll to zoom, drag to pan, Home to see it all. Click a cell to keep it in the inspector."
	map_button.toggle_mode = true
	map_button.button_group = view_group
	map_button.button_pressed = true
	map_button.pressed.connect(_set_view.bind(false))
	view_bar.add_child(map_button)
	globe_button = Button.new()
	globe_button.text = "Globe"
	globe_button.tooltip_text = "G. Drag to turn, scroll to zoom. Click a cell to keep it in the inspector."
	globe_button.toggle_mode = true
	globe_button.button_group = view_group
	globe_button.pressed.connect(_set_view.bind(true))
	view_bar.add_child(globe_button)
	god_button = Button.new()
	god_button.text = "God mode"
	god_button.tooltip_text = "F2. Eruptions, climate, plague, settlers and more."
	god_button.toggle_mode = true
	god_button.toggled.connect(_set_god_mode)
	view_bar.add_child(god_button)
	view_bar.add_child(VSeparator.new())
	population_label = Label.new()
	population_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	population_label.clip_text = true
	population_label.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	view_bar.add_child(population_label)
	column.add_child(view_bar)

	var stack := Control.new()
	stack.size_flags_vertical = Control.SIZE_EXPAND_FILL
	stack.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	column.add_child(stack)

	map_view = Control.new()
	map_view.clip_contents = true
	map_view.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	map_view.mouse_filter = Control.MOUSE_FILTER_STOP
	map_view.gui_input.connect(_on_map_input)
	map_view.mouse_exited.connect(_on_map_mouse_exited)
	map_view.resized.connect(_layout_map)
	stack.add_child(map_view)
	map_rect = TextureRect.new()
	map_rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	map_rect.stretch_mode = TextureRect.STRETCH_SCALE
	map_rect.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	map_rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
	map_view.add_child(map_rect)

	globe = GlobeView.new()
	globe.map_size = Vector2i(MAP_WIDTH, MAP_HEIGHT)
	globe.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	globe.visible = false
	globe.pixel_hovered.connect(_on_globe_pixel)
	globe.pixel_clicked.connect(_on_globe_click)
	globe.mouse_exited.connect(_on_map_mouse_exited)
	stack.add_child(globe)

	god_panel = GodPanel.new()
	god_panel.visible = false
	god_panel.set_anchors_and_offsets_preset(Control.PRESET_TOP_RIGHT)
	god_panel.grow_horizontal = Control.GROW_DIRECTION_BEGIN
	god_panel.position = Vector2(-8, 8)
	god_panel.global_action.connect(_on_global_action)
	god_panel.tool_changed.connect(func() -> void:
		highlight_cell = -2  # force a refresh
		_update_preview(hover_cell))
	god_panel.quicksave_requested.connect(_quicksave)
	god_panel.quickload_requested.connect(_quickload)
	stack.add_child(god_panel)

	var charts := HBoxContainer.new()
	charts.custom_minimum_size = Vector2(0, 150)
	charts.add_theme_constant_override("separation", 24)
	column.add_child(charts)
	population_chart = LineChart.new()
	population_chart.title = "World population, farmers and capacity (marks: domestications)"
	population_chart.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	population_chart.size_flags_stretch_ratio = 1.6
	charts.add_child(population_chart)
	climate_chart = LineChart.new()
	climate_chart.title = "Global temperature (°C; blue: Bond events)"
	climate_chart.value_format = "signed1"
	climate_chart.zero_line = true
	climate_chart.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	charts.add_child(climate_chart)
	var pyramid_box := VBoxContainer.new()
	pyramid_box.custom_minimum_size = Vector2(300, 0)
	var pyramid_title := Label.new()
	pyramid_title.text = "Ages in the inspected cell"
	pyramid_title.add_theme_font_size_override("font_size", 13)
	pyramid_box.add_child(pyramid_title)
	pyramid = PyramidChart.new()
	pyramid.custom_minimum_size = Vector2(300, 120)
	pyramid.size_flags_vertical = Control.SIZE_EXPAND_FILL
	pyramid.visible = false
	pyramid_box.add_child(pyramid)
	charts.add_child(pyramid_box)

	status_label = Label.new()
	status_label.add_theme_font_size_override("font_size", 12)
	status_label.add_theme_color_override("font_color", Color(0.76, 0.76, 0.72))
	root.add_child(status_label)


func _build_top_bar() -> Control:
	var bar := HBoxContainer.new()
	bar.add_theme_constant_override("separation", 10)

	var title := Label.new()
	title.text = "histsim"
	title.add_theme_font_size_override("font_size", 20)
	bar.add_child(title)
	bar.add_child(VSeparator.new())

	bar.add_child(_label("Seed"))
	seed_spin = SpinBox.new()
	seed_spin.min_value = 1
	seed_spin.max_value = 999999
	seed_spin.step = 1
	seed_spin.value = 1
	seed_spin.custom_minimum_size.x = 110
	bar.add_child(seed_spin)

	level_option = OptionButton.new()
	for name in LEVEL_NAMES:
		level_option.add_item(name)
	level_option.selected = 1
	level_option.tooltip_text = "Grid detail. Finer grids look better but run slower."
	bar.add_child(level_option)

	var new_button := Button.new()
	new_button.text = "New world"
	new_button.pressed.connect(_new_world)
	bar.add_child(new_button)

	var random_button := Button.new()
	random_button.text = "Random seed"
	random_button.pressed.connect(func() -> void:
		seed_spin.value = randi_range(1, 999999)
		_new_world())
	bar.add_child(random_button)
	bar.add_child(VSeparator.new())

	play_button = Button.new()
	play_button.text = "Play"
	play_button.custom_minimum_size.x = 70
	play_button.tooltip_text = "Space"
	play_button.pressed.connect(func() -> void: _set_playing(not playing))
	bar.add_child(play_button)

	var step_button := Button.new()
	step_button.text = "+1 year"
	step_button.tooltip_text = "Period key, while paused"
	step_button.pressed.connect(func() -> void:
		world.step_years(1)
		map_dirty = true
		charts_dirty = true
		_update_summary())
	bar.add_child(step_button)

	speed_option = OptionButton.new()
	for s in SPEEDS:
		speed_option.add_item("%s years/s" % _commas(s))
	speed_option.selected = speed_index
	speed_option.item_selected.connect(func(i: int) -> void:
		speed_index = i
		years_due = 0.0)
	bar.add_child(speed_option)
	actual_speed_label = Label.new()
	actual_speed_label.add_theme_font_size_override("font_size", 12)
	actual_speed_label.add_theme_color_override("font_color", Color(0.98, 0.70, 0.10))
	actual_speed_label.tooltip_text = "This grid is too detailed to simulate at the chosen speed on this machine."
	bar.add_child(actual_speed_label)
	bar.add_child(VSeparator.new())

	year_label = Label.new()
	year_label.add_theme_font_size_override("font_size", 18)
	year_label.custom_minimum_size.x = 130
	bar.add_child(year_label)
	return bar


func _build_side_panel() -> Control:
	# A fixed width: the scroll bar is always shown and long text wraps, so
	# hovering a cell with more to say never resizes the panel (and the map).
	var scroll := ScrollContainer.new()
	scroll.custom_minimum_size.x = SIDE_PANEL_WIDTH
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_SHOW_ALWAYS

	var panel := VBoxContainer.new()
	panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	panel.add_theme_constant_override("separation", 6)
	scroll.add_child(panel)

	panel.add_child(_heading("Map mode"))
	var mode_grid := GridContainer.new()
	mode_grid.columns = 2
	mode_grid.add_theme_constant_override("h_separation", 4)
	mode_grid.add_theme_constant_override("v_separation", 2)
	panel.add_child(mode_grid)
	var group := ButtonGroup.new()
	var modes: Array = world.get_modes() if world != null else _fallback_modes()
	var number := 1
	var trait_modes: Array = []
	for mode in modes:
		if String(mode["id"]).begins_with("trait:"):
			trait_modes.append(mode)  # in the picker below the grid
			continue
		var button := Button.new()
		button.text = ("%d  %s" % [number % 10, mode["name"]]) if number <= 10 else ("    " + mode["name"])
		button.tooltip_text = mode["description"]
		button.toggle_mode = true
		button.button_group = group
		button.alignment = HORIZONTAL_ALIGNMENT_LEFT
		button.button_pressed = mode["id"] == mode_id
		button.pressed.connect(_set_mode.bind(mode["id"]))
		button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		button.clip_text = true
		button.add_theme_font_size_override("font_size", 12)
		mode_grid.add_child(button)
		mode_buttons[mode["id"]] = button
		number += 1
	if not trait_modes.is_empty():
		var trait_row := HBoxContainer.new()
		var trait_label := Label.new()
		trait_label.text = "Culture trait"
		trait_label.add_theme_font_size_override("font_size", 12)
		trait_row.add_child(trait_label)
		trait_option = OptionButton.new()
		trait_option.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		trait_option.add_theme_font_size_override("font_size", 12)
		trait_option.add_item("(pick one to map)")
		for mode in trait_modes:
			trait_option.add_item(mode["name"])
			trait_option.set_item_metadata(trait_option.item_count - 1, mode["id"])
			trait_option.set_item_tooltip(trait_option.item_count - 1, mode["description"])
		trait_option.item_selected.connect(func(index: int) -> void:
			if index > 0:
				_set_mode(trait_option.get_item_metadata(index)))
		trait_row.add_child(trait_option)
		panel.add_child(trait_row)

	coastline_check = CheckBox.new()
	coastline_check.text = "Coastlines"
	coastline_check.button_pressed = true
	coastline_check.toggled.connect(func(_on: bool) -> void:
		map_dirty = true
		render_cooldown = 0.0)
	river_check = CheckBox.new()
	river_check.text = "Rivers"
	river_check.button_pressed = true
	river_check.toggled.connect(func(_on: bool) -> void:
		map_dirty = true
		render_cooldown = 0.0)
	border_check = CheckBox.new()
	border_check.text = "Borders"
	border_check.button_pressed = true
	border_check.toggled.connect(func(_on: bool) -> void:
		map_dirty = true
		render_cooldown = 0.0)
	route_check = CheckBox.new()
	route_check.text = "Trade routes"
	route_check.button_pressed = false
	route_check.toggled.connect(func(_on: bool) -> void:
		map_dirty = true
		render_cooldown = 0.0)
	migration_check = CheckBox.new()
	migration_check.text = "Migrations"
	migration_check.tooltip_text = "Folk migrations of the last 50 years, as arrows"
	migration_check.button_pressed = false
	migration_check.toggled.connect(func(_on: bool) -> void:
		map_dirty = true
		render_cooldown = 0.0)
	var overlays := HFlowContainer.new()
	overlays.add_child(coastline_check)
	overlays.add_child(river_check)
	overlays.add_child(border_check)
	overlays.add_child(route_check)
	overlays.add_child(migration_check)
	panel.add_child(overlays)

	panel.add_child(HSeparator.new())
	legend_title = Label.new()
	legend_title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	panel.add_child(legend_title)
	legend_bar = TextureRect.new()
	legend_bar.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	legend_bar.stretch_mode = TextureRect.STRETCH_SCALE
	legend_bar.custom_minimum_size = Vector2(0, 16)
	panel.add_child(legend_bar)
	legend_ticks = HBoxContainer.new()
	panel.add_child(legend_ticks)
	legend_swatches = VBoxContainer.new()
	panel.add_child(legend_swatches)

	panel.add_child(HSeparator.new())
	panel.add_child(_heading("Cell"))
	cell_hint = _label("Hover over the map to look at a cell; click to keep it here.")
	cell_hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART  # never widen the panel
	cell_hint.add_theme_color_override("font_color", Color(0.54, 0.53, 0.51))
	panel.add_child(cell_hint)
	cell_grid = GridContainer.new()
	cell_grid.columns = 2
	cell_grid.add_theme_constant_override("h_separation", 12)
	panel.add_child(cell_grid)
	species_label = Label.new()
	species_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	species_label.add_theme_font_size_override("font_size", 12)
	species_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	species_label.visible = false
	panel.add_child(species_label)

	panel.add_child(HSeparator.new())
	var keys := _label("Space play/pause  ·  1-9, 0 map modes  ·  . step a year  ·  G globe  ·  F2 god mode  ·  F5/F9 quicksave/load")
	keys.add_theme_font_size_override("font_size", 11)
	keys.add_theme_color_override("font_color", Color(0.54, 0.53, 0.51))
	keys.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	panel.add_child(keys)
	return scroll


# Mode buttons are built before the world exists; this mirrors the C++ list
# only for the case where the extension failed to load.
func _fallback_modes() -> Array:
	if ClassDB.class_exists("HistSimWorld"):
		return ClassDB.instantiate("HistSimWorld").get_modes()
	return []


func _heading(text: String) -> Label:
	var label := Label.new()
	label.text = text
	label.add_theme_font_size_override("font_size", 15)
	return label


func _label(text: String) -> Label:
	var label := Label.new()
	label.text = text
	return label


func _commas(n: int) -> String:
	var digits := str(absi(n))
	var out := ""
	var count := 0
	for i in range(digits.length() - 1, -1, -1):
		out = digits[i] + out
		count += 1
		if count % 3 == 0 and i > 0:
			out = "," + out
	return ("-" + out) if n < 0 else out
