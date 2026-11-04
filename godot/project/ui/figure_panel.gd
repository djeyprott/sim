class_name FigurePanel
extends PanelContainer
## Historical figures: a page about one person (life, family, deeds), with
## links to the people and places it names, or a list of the most notable
## figures now. Back returns to the page before.

signal cell_chosen(cell: int)

const INK := Color("#e8e4da")
const INK_MUTED := Color("#a9a69e")
const HEADING := Color(0.93, 0.85, 0.62)

var title_label: Label
var back_button: Button
var text: RichTextLabel
var _world: Object
var _history: Array[int] = []  # figure ids shown before; 0 = the notable list
var _current := -1


func _ready() -> void:
	custom_minimum_size = Vector2(440, 420)
	# Opaque, like the god-mode panel, so the map does not show through.
	var style := StyleBoxFlat.new()
	style.bg_color = Color("#242422")
	style.border_color = Color("#383835")
	style.set_border_width_all(1)
	style.set_corner_radius_all(6)
	style.set_content_margin_all(12)
	add_theme_stylebox_override("panel", style)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 4)
	add_child(box)
	var row := HBoxContainer.new()
	back_button = Button.new()
	back_button.text = "◀"
	back_button.tooltip_text = "Back"
	back_button.focus_mode = Control.FOCUS_NONE
	back_button.pressed.connect(_back)
	row.add_child(back_button)
	title_label = Label.new()
	title_label.add_theme_font_size_override("font_size", 15)
	title_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	title_label.clip_text = true
	row.add_child(title_label)
	var people := Button.new()
	people.text = "Notable"
	people.tooltip_text = "The most notable figures now"
	people.focus_mode = Control.FOCUS_NONE
	people.pressed.connect(func() -> void: show_notable(_world))
	row.add_child(people)
	var close := Button.new()
	close.text = "✕"
	close.focus_mode = Control.FOCUS_NONE
	close.pressed.connect(func() -> void: visible = false)
	row.add_child(close)
	box.add_child(row)
	text = RichTextLabel.new()
	text.bbcode_enabled = true
	text.size_flags_vertical = Control.SIZE_EXPAND_FILL
	text.selection_enabled = true
	text.add_theme_font_size_override("normal_font_size", 13)
	text.add_theme_font_size_override("bold_font_size", 13)
	text.meta_clicked.connect(_on_link)
	box.add_child(text)


func show_figure(world: Object, id: int, remember := true) -> void:
	_world = world
	if world == null:
		return
	var page: Dictionary = world.get_figure(id)
	if page.is_empty():
		return
	if remember and _current >= 0 and _current != id:
		_history.append(_current)
	_current = id
	visible = true
	title_label.text = page["title"]
	var out := ""
	if page["subtitle"] != "":
		out += "[color=#%s]%s[/color]\n" % [INK_MUTED.to_html(false), _escape(page["subtitle"])]
	out += _section("Life", page["life"])
	out += _section("Family", page["family"])
	out += _section("Deeds", page["deeds"])
	text.text = out
	text.scroll_to_line(0)
	back_button.disabled = _history.is_empty()


func show_notable(world: Object, remember := true) -> void:
	_world = world
	if world == null:
		return
	if remember and _current >= 0 and _current != 0:
		_history.append(_current)
	_current = 0
	visible = true
	title_label.text = "Notable figures"
	var out := "[color=#%s]Rulers of the largest realms, teachers of faiths and the latest inventors. Click a name to read about them.[/color]\n\n" % INK_MUTED.to_html(false)
	for f in world.get_notable_figures(16):
		out += "[url=f:%d]%s[/url]\n    [color=#%s]%s[/color]\n" % [f["id"], _escape(f["title"]), INK_MUTED.to_html(false), _escape(f["subtitle"])]
	text.text = out
	text.scroll_to_line(0)
	back_button.disabled = _history.is_empty()


# A new world: forget the pages of the old one.
func clear() -> void:
	_history.clear()
	_current = -1
	visible = false


func _section(name: String, lines: Array) -> String:
	if lines.is_empty():
		return ""
	var out := "\n[b][color=#%s]%s[/color][/b]\n" % [HEADING.to_html(false), name]
	for l in lines:
		var body := _escape(l["text"])
		if int(l["figure"]) > 0:
			body = "[url=f:%d]%s[/url]" % [l["figure"], body] if l["label"] != "" and not _is_deed(l) else body + "  [url=f:%d]»[/url]" % l["figure"]
		if int(l["cell"]) >= 0:
			body += "  [url=c:%d][color=#%s]map[/color][/url]" % [l["cell"], INK_MUTED.to_html(false)]
		var label: String = l["label"]
		out += ("[color=#%s]%s[/color]  %s\n" % [INK_MUTED.to_html(false), _escape(label), body]) if label != "" else body + "\n"
	return out


func _is_deed(l: Dictionary) -> bool:
	return String(l["label"]).begins_with("year ")


func _on_link(meta: Variant) -> void:
	var s := str(meta)
	if s.begins_with("f:"):
		show_figure(_world, int(s.substr(2)))
	elif s.begins_with("c:"):
		cell_chosen.emit(int(s.substr(2)))


func _back() -> void:
	if _history.is_empty():
		return
	var id: int = _history.pop_back()
	if id == 0:
		show_notable(_world, false)
	else:
		show_figure(_world, id, false)


func _escape(s: String) -> String:
	return s.replace("[", "[lb]")
