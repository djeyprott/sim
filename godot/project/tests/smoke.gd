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


func _initialize() -> void:
	if not ClassDB.class_exists("HistSimWorld"):
		_finish(["extension did not load (HistSimWorld missing)"])
		return
	_check_api()
	main = load("res://main.tscn").instantiate()
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
	if frames == 40:
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
		_expect(legend.has("title") and legend["ramp"].size() >= 2, "legend for " + mode["id"])

	var cell: int = w.cell_at(128, 64)
	_expect(cell >= 0, "cell_at inside the map")
	_expect(w.cell_at(-5, 0) == -1, "cell_at outside the map")
	_expect(w.describe_cell(cell).size() >= 4, "describe_cell lists fields")

	var path := OS.get_user_data_dir().path_join("smoke.hsim")
	var hash_before: String = w.get_state_hash()
	_expect(w.save_snapshot(path), "save_snapshot")
	var w2: Object = ClassDB.instantiate("HistSimWorld")
	_expect(w2.load_snapshot(path), "load_snapshot")
	_expect(w2.get_state_hash() == hash_before, "snapshot round trip keeps the state hash")
	_expect(not w2.load_snapshot(path + ".missing"), "load_snapshot fails cleanly on a missing file")
	DirAccess.remove_absolute(path)


func _check_scene() -> void:
	_expect(main.world != null, "main scene created a world")
	_expect(main.world.get_year() > 0, "playback advanced the year (now %d)" % main.world.get_year())
	_expect(main.map_texture != null, "main scene rendered the map")
	_expect(main.mode_id == "population", "mode switch")
	_expect(main.legend_ticks.get_child_count() >= 2, "legend ticks shown")


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
