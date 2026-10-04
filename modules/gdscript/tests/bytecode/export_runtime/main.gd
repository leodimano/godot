extends Control

const Peer = preload("res://peer.gd")
const DATA = preload("res://data.tres")
var failures: Array[String] = []

func check(condition: bool, message: String):
	if not condition:
		failures.append(message)

@rpc("any_peer", "call_local")
func echo(value: int) -> int:
	return value

func _ready():
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
	if "--verify" in OS.get_cmdline_user_args():
		check(compiled, "main script still contains source")
		var peer_script: Script = Peer
		check(peer_script.get_source_code().is_empty(), "peer script still contains source")
		check(DATA.get_script().get_source_code().is_empty(), "resource script still contains source")
		print("COMPILED_EXPORT_RESULT ", JSON.stringify({"failures": failures, "compiled": compiled}))
		get_tree().quit(0 if failures.is_empty() else 1)
	$Title.text = "Precompiled GDScript"
	$Details.text = "Runtime checks: " + ("passed" if failures.is_empty() else str(failures))
	$Details.text += "\n" + ("Running precompiled VM code" if compiled else "Editor source preview")
	$Details.text += "\n\nProject > Export > Script > Precompiled GDScript"
