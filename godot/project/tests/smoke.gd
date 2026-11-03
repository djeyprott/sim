extends SceneTree
## Headless smoke test for the viewer.
##
##   godot --headless --path godot/project --script res://tests/smoke.gd
##
## Checks that the extension loads, exercises its API, then runs the main
## scene for a few dozen frames with playback on. Prints "SMOKE OK" and exits
## 0 on success; prints "SMOKE FAIL: ..." and exits 1 otherwise.

var main: Control
var frames := 0
var failures: Array[String] = []
var god_killed := 0
var quicksave_year := -1
var globe_ok := false
var map_rect_short := Rect2()


func _initialize() -> void:
	if not ClassDB.class_exists("HistSimWorld"):
		_finish(["extension did not load (HistSimWorld missing)"])
		return
	_check_api()
	main = load("res://main.tscn").instantiate()
	if not main.has_method("_set_playing"):
		_finish(["main.gd failed to load (script error)"])
		main = null
		return
	root.add_child(main)
	# The headless window is tiny: give the viewer a real size so layout and
	# input can be checked.
	main.set_anchors_preset(Control.PRESET_TOP_LEFT)
	main.size = Vector2(1600, 900)


func _process(_delta: float) -> bool:
	if main == null:
		return true
	frames += 1
	if frames == 3:
		main._set_playing(true)
		main.speed_index = 3  # 1,000 years per second
	if frames == 8:
		main._set_mode("trait:kinship")
		if main.mode_id != "trait:kinship":
			failures.append("a culture trait mode could not be chosen")
		if main.current_group != "Culture" or not main.trait_row.visible:
			failures.append("choosing a trait mode does not show the Culture group")
		main._set_mode("realms")
		if main.current_group != "Power" or not main.mode_buttons["realms"].visible or main.mode_buttons["terrain"].visible:
			failures.append("choosing a mode does not show only its group's buttons")
	if frames == 10:
		main._set_mode("population")
	if frames == 20:
		main._set_view(true)  # globe
	if frames == 30:
		main._show_cell(main.world.cell_at(1024, 400))
	if frames == 31:
		globe_ok = main.showing_globe and main.globe.visible
	if frames == 32:
		# Paused, so the background run cannot add a year between the
		# checks below.
		main._set_playing(false)
		main._set_view(false)
		main._set_god_mode(true)
		main.god_panel.set_tool("plague")
		var cell := _populated_cell()
		var before: int = main.world.get_population()
		main._apply_tool(cell)
		god_killed = before - main.world.get_population()
		main._on_global_action("eruption", 6.0)
		main._quicksave()
		quicksave_year = main.world.get_year()
		main.world.step_years(3)
		main._quickload()
	# Layout: the map must not move or resize when the inspector grows (a
	# cell with many rows and a scroll bar, then one with few).
	if frames == 40:
		main._set_playing(false)
		main.god_panel.set_tool("inspect")
		main._on_cell_hovered(_ocean_cell())
	if frames == 42:
		map_rect_short = main.map_rect.get_global_rect()
		main._on_cell_hovered(_populated_cell())
	if frames == 44:
		var long_rect: Rect2 = main.map_rect.get_global_rect()
		if long_rect != map_rect_short:
			failures.append("map moved when the inspector grew: %s -> %s" % [map_rect_short, long_rect])
		main._on_cell_hovered(_ocean_cell())
	if frames == 46:
		var back: Rect2 = main.map_rect.get_global_rect()
		if back != map_rect_short:
			failures.append("map moved when the inspector shrank: %s -> %s" % [map_rect_short, back])
		if main.cell_hint.size.x > main.SIDE_PANEL_WIDTH:
			failures.append("the inspector hint widens the side panel (%d px)" % main.cell_hint.size.x)
		_check_selection_and_zoom()
		main._set_view(true)
	if frames == 49:  # once the globe has been laid out
		_check_globe_drag()
		main._set_view(false)
	if frames == 51:
		_check_scene()
		_finish(failures)
		return true
	return false


# Clicking keeps a cell in the inspector while the mouse moves on; clicking
# it again lets go. The flat map zooms about the cursor and resets.
func _check_selection_and_zoom() -> void:
	main.god_panel.set_tool("inspect")
	var kept := _populated_cell()
	var other := _ocean_cell()
	main._on_cell_clicked(kept)
	main._on_cell_hovered(other)
	# A kept cell brings out its group in grouping modes only.
	if not main.world.set_focus("language", kept) or main.world.set_focus("terrain", kept):
		failures.append("set_focus does not pick out a language's cells (and only in grouping modes)")
	if typeof(main.world.get_polity_labels()) != TYPE_ARRAY:
		failures.append("get_polity_labels did not return an array")
	# The timeline lists what happened and takes you there.
	main._set_timeline(true)
	if main.timeline_panel.list.item_count == 0:
		failures.append("the timeline is empty")
	main._go_to_cell(kept)
	if main.selected_cell != kept:
		failures.append("choosing a timeline entry does not keep its cell")
	main._set_timeline(false)
	main._reset_map_view()
	if main.selected_cell != kept or main._inspected_cell() != kept:
		failures.append("a clicked cell is not kept in the inspector")
	if main.inspector_values.get("Cell", "") != main._commas(kept):
		failures.append("the inspector does not show the kept cell")
	main._on_cell_clicked(kept)
	if main.selected_cell != -1 or main._inspected_cell() != other:
		failures.append("clicking the kept cell again does not let it go")
	var at: Vector2 = main.map_view.size * 0.5 + Vector2(37, 21)
	var before: int = main._cell_under(at)
	main._zoom_map(2.0, at)
	if absf(main.map_zoom - 2.0) > 1e-6 or main._cell_under(at) != before:
		failures.append("zooming the map moves the cell under the cursor (%d -> %d)" % [before, main._cell_under(at)])
	main._zoom_map(100.0, at)
	if main.map_zoom > main.MAX_ZOOM:
		failures.append("map zoom is not capped")
	var r: Rect2 = Rect2(main.map_rect.position, main.map_rect.size)
	if r.position.y > 0.0 or r.end.y < main.map_view.size.y:
		failures.append("the zoomed map does not cover the view north to south: %s" % r)
	# East-west it wraps: every point across the view is on the map, and
	# panning a whole map width comes back to the same cells.
	for i in 9:
		var p: Vector2 = Vector2(main.map_view.size.x * i / 8.0, main.map_view.size.y * 0.5)
		if main._cell_under(p) < 0:
			failures.append("no map at %s: the flat map does not wrap" % p)
			break
	var probe: Vector2 = main.map_view.size * 0.5
	var before_pan: int = main._cell_under(probe)
	main.map_offset.x += main.map_rect.size.x
	main._layout_map()
	if main._cell_under(probe) != before_pan:
		failures.append("panning a whole map width does not come back to the same cell")
	main._reset_map_view()
	if main.map_zoom != 1.0:
		failures.append("Home does not reset the map zoom")


# Dragging the globe keeps the grabbed point under the cursor.
func _check_globe_drag() -> void:
	var g: GlobeView = main.globe
	var start: Vector2 = g.size * 0.5 + Vector2(-20, 10)
	var grabbed: Vector2i = g.pixel_at(start)
	var press := InputEventMouseButton.new()
	press.button_index = MOUSE_BUTTON_LEFT
	press.pressed = true
	press.position = start
	g._gui_input(press)
	var to := start
	for step in 6:  # several small moves, as a real drag sends
		var motion := InputEventMouseMotion.new()
		motion.relative = Vector2(14, -9)
		to += motion.relative
		motion.position = to
		g._gui_input(motion)
	var release := InputEventMouseButton.new()
	release.button_index = MOUSE_BUTTON_LEFT
	release.pressed = false
	release.position = to
	g._gui_input(release)
	var now: Vector2i = g.pixel_at(to)
	if grabbed.x < 0 or absi(now.x - grabbed.x) > 3 or absi(now.y - grabbed.y) > 3:
		failures.append("globe drag lost the grabbed point: %s -> %s" % [grabbed, now])


func _check_api() -> void:
	var w: Object = ClassDB.instantiate("HistSimWorld")
	_expect(w.create_world(5, 4, 0.7), "create_world")
	_expect(not w.create_world(5, 99, 0.7), "create_world rejects a bad level")
	_expect(w.create_world_of_kind(5, 4, 0.7, "earth"), "create an Earth world")
	_expect(w.get_world_kind() == "earth", "the world is Earth")
	_expect(not w.create_world_of_kind(5, 4, 0.7, "mars"), "create_world_of_kind rejects an unknown kind")
	_expect(w.create_world(5, 4, 0.7), "create_world again")
	_expect(w.get_world_kind() == "generated", "the world is generated again")
	_expect(w.get_cell_count() == 2562, "cell count for level 4")
	var start_population: int = w.get_population()
	_expect(start_population > 0, "initial population")
	w.step_years(50)
	_expect(w.get_year() == 50, "step_years advances the year")

	var modes: Array = w.get_modes()
	_expect(modes.size() >= 5, "at least five map modes")
	w.set_map_size(256, 128)
	for mode in modes:
		var image: Image = w.render_map(mode["id"], true)
		_expect(image != null and image.get_width() == 256 and image.get_height() == 128,
			"render_map(%s) returns a 256x128 image" % mode["id"])
		var legend: Dictionary = w.get_legend(mode["id"])
		_expect(legend.has("title") and (legend["ramp"].size() >= 2 or legend["swatches"].size() >= 3),
			"legend for " + mode["id"])

	var cell: int = w.cell_at(128, 64)
	_expect(cell >= 0, "cell_at inside the map")
	_expect(w.cell_at(-5, 0) == -1, "cell_at outside the map")
	_expect(w.describe_cell(cell).size() >= 4, "describe_cell lists fields")
	_expect(w.render_map("biome", true, false) != null, "render without rivers")
	_expect(w.render_map("trade", true, true, true, true) != null, "render with borders and trade routes")
	_expect(w.render_map("language", true, true, true, true, true) != null, "render with migrations")
	var h: Dictionary = w.get_history(100)
	_expect(h["year"].size() == 50 and h["population"].size() == 50, "history has one row per year")
	_expect(w.get_capacity() > 0.0, "capacity")
	var populated := -1
	for c in w.get_cell_count():
		var p: Dictionary = w.get_cell_pyramid(c)
		var people := 0
		for n in p["female"]:
			people += n
		if people > 0:
			populated = c
			break
	_expect(populated >= 0 and w.get_cell_pyramid(populated)["male"].size() == 21, "cell pyramid has 21 bins")

	# Interventions
	var r: Dictionary = w.apply_intervention("eruption", -1, 0.0, 8.0, 0.0, 0)
	_expect(r["ok"], "eruption intervention")
	r = w.apply_intervention("plague", populated, 800.0, 0.5, 0.0, 0)
	_expect(r["ok"] and r["people"] > 0, "plague intervention kills people")
	r = w.apply_intervention("settlers", populated, 0.0, 1000.0, 0.0, 0)
	_expect(r["ok"] and r["people"] == 1000, "settlers intervention")
	r = w.apply_intervention("climate", populated, 1000.0, -3.0, -0.5, 10)
	_expect(r["ok"] and r["cells"] > 1, "regional climate intervention")
	r = w.apply_intervention("forcing", -1, 0.0, 2.5, 0.0, 0)
	_expect(r["ok"] and is_equal_approx(w.get_imposed_forcing(), 2.5), "forcing intervention")
	r = w.apply_intervention("settlers", -1, 0.0, 10.0, 0.0, 0)
	_expect(not r["ok"], "intervention without a cell is refused")
	_expect(w.get_intervention_count() == 5, "five interventions recorded")
	_expect(w.get_event_years("eruption").size() >= 1, "eruption event years")
	w.set_highlight(populated, 800.0)
	_expect(w.render_map("terrain", true) != null, "render with highlight")
	w.set_highlight(-1, 0.0)

	var path := OS.get_user_data_dir().path_join("smoke.hsim")
	var hash_before: String = w.get_state_hash()
	_expect(w.save_snapshot(path), "save_snapshot")
	var w2: Object = ClassDB.instantiate("HistSimWorld")
	_expect(w2.load_snapshot(path), "load_snapshot")
	_expect(w2.get_state_hash() == hash_before, "snapshot round trip keeps the state hash")
	_expect(w2.get_intervention_count() == 5, "snapshot keeps the interventions")
	_expect(not w2.load_snapshot(path + ".missing"), "load_snapshot fails cleanly on a missing file")
	DirAccess.remove_absolute(path)

	# Years run in the background.
	var year_before: int = w2.get_year()
	w2.queue_years(10)
	var finished := 0
	for i in range(600):
		finished += w2.take_finished_years()
		if finished >= 10:
			break
		OS.delay_msec(10)
	_expect(finished == 10 and w2.get_year() == year_before + 10 and w2.get_queued_years() == 0, "queue_years runs years in the background")
	w2.queue_years(1000)
	w2.clear_queue()
	w2.take_finished_years()
	var stopped_at: int = w2.get_year()
	OS.delay_msec(50)
	_expect(w2.get_year() == stopped_at and w2.get_queued_years() == 0, "clear_queue stops the background run")


func _check_scene() -> void:
	_expect(main.world != null, "main scene created a world")
	_expect(main.world.get_year() > 0, "playback advanced the year (now %d)" % main.world.get_year())
	_expect(main.map_texture != null, "main scene rendered the map")
	_expect(main.mode_id == "population", "mode switch")
	_expect(main.legend_ticks.get_child_count() >= 2, "legend ticks shown")
	_expect(globe_ok, "globe view switch")
	_expect(main.population_chart._x.size() > 1, "population chart has history")
	_expect(god_killed > 0, "god-mode plague click killed people")
	_expect(main.world.get_intervention_count() >= 2, "god-mode actions recorded")
	_expect(main.world.get_year() == quicksave_year, "quickload restored the saved year")
	_expect(main.god_panel.visible, "god panel shown")


# A cell with little to describe (ocean).
func _ocean_cell() -> int:
	for y in range(200, 800, 32):
		for x in range(0, 2048, 32):
			var c: int = main.world.cell_at(x, y)
			if main.world.describe_cell(c).size() < 8:
				return c
	return 0


func _populated_cell() -> int:
	var best := -1
	var best_people := 0
	for y in range(200, 800, 32):
		for x in range(0, 2048, 32):
			var c: int = main.world.cell_at(x, y)
			var p: Dictionary = main.world.get_cell_pyramid(c)
			var people := 0
			for n in p["female"]:
				people += n
			if people > best_people:
				best_people = people
				best = c
	return best


func _expect(condition: bool, what: String) -> void:
	if not condition:
		failures.append(what)


func _finish(problems: Array) -> void:
	if problems.is_empty():
		print("SMOKE OK")
		quit(0)
	else:
		for p in problems:
			print("SMOKE FAIL: ", p)
		quit(1)
