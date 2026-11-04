class_name TimelinePanel
extends PanelContainer
## The timeline: what happened in the run, in words, newest first. Filter by
## kind, or to the realm of the cell kept in the inspector; click an entry to
## go to where it happened.

signal entry_chosen(cell: int)
signal figure_chosen(figure: int)

const KINDS := [["", "Everything"], ["realms", "Realms"], ["war", "War"], ["peoples", "Peoples"],
	["faith", "Faith"], ["nature", "Nature"], ["discovery", "Discoveries"]]
const MAX_ENTRIES := 300
const INK_MUTED := Color("#898781")

var kind_option: OptionButton
var realm_check: CheckBox
var list: ItemList
var note: Label
var _signature := ""


func _ready() -> void:
	custom_minimum_size = Vector2(460, 320)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 4)
	add_child(box)
	var row := HBoxContainer.new()
	var title := Label.new()
	title.text = "Timeline"
	title.add_theme_font_size_override("font_size", 15)
	row.add_child(title)
	kind_option = OptionButton.new()
	kind_option.focus_mode = Control.FOCUS_NONE
	kind_option.add_theme_font_size_override("font_size", 12)
	for k in KINDS:
		kind_option.add_item(k[1])
	kind_option.item_selected.connect(func(_i: int) -> void: _signature = "")
	row.add_child(kind_option)
	realm_check = CheckBox.new()
	realm_check.text = "Kept cell's realm only"
	realm_check.focus_mode = Control.FOCUS_NONE
	realm_check.add_theme_font_size_override("font_size", 12)
	realm_check.toggled.connect(func(_on: bool) -> void: _signature = "")
	row.add_child(realm_check)
	box.add_child(row)
	list = ItemList.new()
	list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	list.focus_mode = Control.FOCUS_NONE
	list.add_theme_font_size_override("font_size", 12)
	list.item_clicked.connect(func(index: int, _at: Vector2, _button: int) -> void:
		var meta: Array = list.get_item_metadata(index)
		if meta[0] >= 0:
			entry_chosen.emit(meta[0])
		if meta[1] > 0:
			figure_chosen.emit(meta[1]))
	box.add_child(list)
	note = Label.new()
	note.add_theme_font_size_override("font_size", 11)
	note.add_theme_color_override("font_color", INK_MUTED)
	box.add_child(note)


# Refill the list if anything changed: the year, the filter or the realm.
func refresh(world: Object, kept_cell: int) -> void:
	if world == null or list == null:
		return
	var polity := 0
	if realm_check.button_pressed and kept_cell >= 0:
		polity = world.get_cell_polity(kept_cell)
	var kind: String = KINDS[kind_option.selected][0]
	var signature := "%d/%s/%d" % [world.get_year(), kind, polity]
	if signature == _signature:
		return
	_signature = signature
	list.clear()
	if realm_check.button_pressed and polity == 0:
		note.text = "Keep a cell (click it) in a realm to see that realm's history."
		return
	for e in world.get_timeline(kind, polity, -1, MAX_ENTRIES):
		var index := list.add_item("Year %s   %s" % [_commas(e["year"]), e["text"]])
		list.set_item_metadata(index, [e["cell"], e.get("figure", 0)])
		list.set_item_tooltip(index, e["text"])
	note.text = ("The latest %d entries. Click one to go there (and to the person it names)." % MAX_ENTRIES) if list.item_count >= MAX_ENTRIES \
		else ("%d entries. Click one to go there (and to the person it names)." % list.item_count if list.item_count > 0 else "Nothing yet.")


func _commas(n: int) -> String:
	var s := str(absi(n))
	var out := ""
	while s.length() > 3:
		out = "," + s.substr(s.length() - 3) + out
		s = s.substr(0, s.length() - 3)
	return ("-" if n < 0 else "") + s + out
