@tool
extends RefCounted

static func _static_init():
	Engine.set_meta("compilation_initialized", true)

static func mark():
	Engine.set_meta("compilation_asserted", true)
	return true

static func check_assertion():
	assert(mark(), "test")
	return 7
