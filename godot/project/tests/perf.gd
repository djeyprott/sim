extends SceneTree
## Frame rate while the simulation plays, headless and capped at 60 frames
## per second as a display would be:
##
##   godot --headless --path godot/project --script res://tests/perf.gd
##
## Builds a level-5 world, runs it 3,000 years, then plays at 1,000 years per
## second for 12 seconds and prints the frames per second, the slowest frame
## and the years simulated per second.

var main: Control
var frames := 0
var start_usec := 0
var last_usec := 0
var slowest_usec := 0
var start_year := 0
const SECONDS := 12.0


func _initialize() -> void:
	Engine.max_fps = 60  # as with a display's vsync
	main = load("res://main.tscn").instantiate()
	root.add_child(main)
	main.set_anchors_preset(Control.PRESET_TOP_LEFT)
	main.size = Vector2(1600, 900)


func _process(_delta: float) -> bool:
	frames += 1
	if frames == 2:
		main.world.step_years(3000)
		main.map_dirty = true
		main._set_mode("realms")
		main.border_check.button_pressed = true
	if frames == 5:
		main.speed_index = 3  # 1,000 years per second
		main._set_playing(true)
		start_usec = Time.get_ticks_usec()
		last_usec = start_usec
		start_year = main.world.get_year()
		main.world.take_paused_msec()
		frames = 5
	if frames > 5:
		var now := Time.get_ticks_usec()
		slowest_usec = maxi(slowest_usec, now - last_usec)
		last_usec = now
		var elapsed := (now - start_usec) / 1e6
		if elapsed >= SECONDS:
			main._set_playing(false)
			var years: int = main.world.get_year() - start_year
			var paused: float = main.world.take_paused_msec() / 1000.0
			print("PERF fps %.0f  slowest frame %.1f ms  years/s %.0f  run paused for the UI %.0f%%" % [
				(frames - 5) / elapsed, slowest_usec / 1000.0, years / elapsed, 100.0 * paused / elapsed])
			return true
	return false
