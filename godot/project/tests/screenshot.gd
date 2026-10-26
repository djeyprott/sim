extends SceneTree
## Renders the viewer and saves a screenshot, for checking the UI without a
## person at the screen. Needs a display (use xvfb-run on a headless machine):
##
##   xvfb-run -s "-screen 0 1600x900x24" godot --path godot/project \
##       --rendering-driver opengl3 --script res://tests/screenshot.gd -- out.png population 400 map
##
## Arguments after "--": output PNG path, map mode id, years to simulate,
## "map" or "globe", and optionally a god-mode tool id: the run then gets an
## eruption and a plague halfway through, and the panel is shown with that
## tool previewing its area.

var main: Control
var frames := 0
var out_path := "screenshot.png"
var mode := "terrain"
var years := 300
var view := "map"
var god_tool := ""


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() > 0:
		out_path = args[0]
	if args.size() > 1:
		mode = args[1]
	if args.size() > 2:
		years = int(args[2])
	if args.size() > 3:
		view = args[3]
	if args.size() > 4:
		god_tool = args[4]
	root.size = Vector2i(1600, 900)
	main = load("res://main.tscn").instantiate()
	root.add_child(main)


func _process(_delta: float) -> bool:
	frames += 1
	if frames == 2:
		# Simulate in yearly steps so the history charts fill in.
		if god_tool != "":
			main.world.step_years(years / 2)
			var target := _populated_cell()
			main.world.apply_intervention("plague", target, 1500.0, 0.6, 0.0, 0)
			main.world.apply_intervention("eruption", -1, 0.0, 12.0, 0.0, 0)
			main.world.step_years(years - years / 2)
		else:
			main.world.step_years(years)
		main.map_dirty = true
		main.charts_dirty = true
		main._update_summary()
		main._set_mode(mode)
		main._set_view(view == "globe")
	if frames == 6:
		_hover_a_populated_cell()
		if god_tool != "":
			main._set_god_mode(true)
			main.god_panel.set_tool(god_tool)
			main._update_preview(main.hover_cell)
	if frames == 14:
		var image := root.get_texture().get_image()
		var err := image.save_png(out_path)
		print("saved %s (%s)" % [out_path, error_string(err)])
		return true
	return false


func _populated_cell() -> int:
	var best := -1
	var best_people := 0
	for y in range(200, 800, 16):
		for x in range(0, 2048, 16):
			var c: int = main.world.cell_at(x, y)
			var p: Dictionary = main.world.get_cell_pyramid(c)
			var people := 0
			for n in p["female"]:
				people += n
			if people > best_people:
				best_people = people
				best = c
	return best


# Find a densely populated cell and show it in the inspector, as hovering would.
func _hover_a_populated_cell() -> void:
	var best := -1
	var best_people := 0
	for y in range(200, 800, 16):
		for x in range(0, 2048, 16):
			var c: int = main.world.cell_at(x, y)
			var p: Dictionary = main.world.get_cell_pyramid(c)
			var people := 0
			for n in p["female"]:
				people += n
			if people > best_people:
				best_people = people
				best = c
	if best >= 0:
		main.hover_cell = best
		main._show_cell(best)
