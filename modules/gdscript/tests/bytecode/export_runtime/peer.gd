@static_unload
extends RefCounted

const VALUES: Array[int] = [1, 2, 3]
static var initial_value: int = 7

static func _static_init():
	Engine.set_meta("compiled_static_ready", true)

static func answer() -> int:
	var captured: int = 2
	var closure := func(value: int) -> int: return value + captured
	return closure.call(40)

class Inner:
	extends RefCounted
	func value() -> int:
		return 3
