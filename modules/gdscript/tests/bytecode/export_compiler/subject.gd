@tool
extends RefCounted

static func _static_init():
	push_error("The export compiler must not run project static initializers.")

static func answer() -> int:
	return 42
