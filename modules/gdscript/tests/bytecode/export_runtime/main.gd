extends Control

const Peer = preload("res://peer.gd")
const DATA = preload("res://data.tres")
const WIDE_INTEGER: int = 0x123456789abcdef
var failures: Array[String] = []

func check(condition: bool, message: String):
	if not condition:
		failures.append(message)

@rpc("any_peer", "call_local")
func echo(value: int) -> int:
	return value

func check_portable_values():
	var wide: int = WIDE_INTEGER
	check((wide >> 32) == 0x1234567, "64-bit integer on target")
	var packed := PackedInt64Array([wide, -wide])
	check(packed[0] == wide and packed[1] == -wide, "packed 64-bit values")
	var lookup: Dictionary[int, Vector2] = {wide: Vector2(3.0, 4.0)}
	check(lookup[wide].length() == 5.0, "typed dictionary and builtin call")
	position = lookup[wide]
	check(position == Vector2(3.0, 4.0), "native property binding")
	var dynamic: Variant = 0
	for index in 3:
		dynamic += wide
	check(dynamic == wide * 3, "runtime operator cache")
	var transform := Transform2D(0.0, position)
	var point := Vector2(float(packed.size()), 1.0)
	check(transform * point == Vector2(5.0, 5.0), "runtime math constructor and operator")

func _ready():
	check_portable_values()
	check(Engine.get_meta("compiled_autoload_ready", false), "autoload")
	check(Engine.get_meta("compiled_static_ready", false), "static initializer")
	check(Peer.initial_value == 7, "fresh static value")
	check(Peer.answer() == 42, "closure")
	check(Peer.Inner.new().value() == 3, "inner class")
	check(Peer.VALUES.is_typed() and Peer.VALUES.is_read_only(), "typed constant")
	check(DATA.value == 40, "scripted resource")
	check(load("res://peer.gd") == Peer, "logical remap identity")
	check(echo(42) == 42, "RPC function")
	var compiled: bool = get_script().get_source_code().is_empty()
	if "--verify" in OS.get_cmdline_user_args() or OS.has_feature("web"):
		check(compiled, "main script still contains source")
		var peer_script: Script = Peer
		check(peer_script.get_source_code().is_empty(), "peer script still contains source")
		check(DATA.get_script().get_source_code().is_empty(), "resource script still contains source")
		print("COMPILED_EXPORT_RESULT ", JSON.stringify({"failures": failures, "compiled": compiled, "architecture": Engine.get_architecture_name()}))
		if not OS.has_feature("web"):
			get_tree().quit(0 if failures.is_empty() else 1)
	$Title.text = "Precompiled GDScript"
	$Details.text = "Runtime checks: " + ("passed" if failures.is_empty() else str(failures))
	$Details.text += "\n" + ("Running precompiled VM code" if compiled else "Editor source preview")
	$Details.text += "\n\nProject > Export > Script > Precompiled GDScript"
