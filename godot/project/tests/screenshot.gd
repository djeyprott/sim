extends SceneTree
## Renders the viewer and saves a screenshot, for checking the UI without a
## person at the screen. Needs a display (use xvfb-run on a headless machine):
##
##   xvfb-run -s "-screen 0 1600x900x24" godot --path godot/project \
##       --rendering-driver opengl3 --script res://tests/screenshot.gd -- out.png population 400
##
## Arguments after "--": output PNG path, map mode id, years to simulate.

var main: Control
var frames := 0
var out_path := "screenshot.png"
var mode := "terrain"
var years := 300


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() > 0:
		out_path = args[0]
	if args.size() > 1:
		mode = args[1]
	if args.size() > 2:
		years = int(args[2])
	root.size = Vector2i(1600, 900)
	main = load("res://main.tscn").instantiate()
	root.add_child(main)


func _process(_delta: float) -> bool:
	frames += 1
	if frames == 2:
		main.world.step_years(years)
		main._update_summary()
		main._set_mode(mode)
	if frames == 5:
		# Hover a point over land near the map's center-left, to show the inspector.
		var motion := InputEventMouseMotion.new()
		motion.position = main.map_rect.size * Vector2(0.30, 0.45)
		main._on_map_input(motion)
	if frames == 12:
		var image := root.get_texture().get_image()
		var err := image.save_png(out_path)
		print("saved %s (%s)" % [out_path, error_string(err)])
		return true
	return false
