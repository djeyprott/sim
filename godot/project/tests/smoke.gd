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


func _process(_delta: float) -> bool:
	if main == null:
		return true
	frames += 1
	if frames == 3:
		main._set_playing(true)
		main.speed_index = 3  # 1,000 years per second
	if frames == 10:
		main._set_mode("population")
	if frames == 20:
		main._set_view(true)  # globe
	if frames == 30:
		main._show_cell(main.world.cell_at(1024, 400))
	if frames == 31:
		globe_ok = main.showing_globe and main.globe.visible
	if frames == 32:
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
		main._show_cell(_ocean_cell())
	if frames == 42:
		map_rect_short = main.map_rect.get_global_rect()
		main._show_cell(_populated_cell())
	if frames == 44:
		var long_rect: Rect2 = main.map_rect.get_global_rect()
		if long_rect != map_rect_short:
			failures.append("map moved when the inspector grew: %s -> %s" % [map_rect_short, long_rect])
		main._show_cell(_ocean_cell())
	if frames == 46:
		var back: Rect2 = main.map_rect.get_global_rect()
		if back != map_rect_short:
			failures.append("map moved when the inspector shrank: %s -> %s" % [map_rect_short, back])
	if frames == 48:
		_check_scene()
		_finish(failures)
		return true
	return false


func _check_api() -> void:
	var w: Object = ClassDB.instantiate("HistSimWorld")
	_expect(w.create_world(5, 4, 0.7), "create_world")
	_expect(not w.create_world(5, 99, 0.7), "create_world rejects a bad level")
	_expect(w.create_world(5, 4, 0.7), "create_world again")
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
