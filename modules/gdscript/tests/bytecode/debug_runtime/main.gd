extends Node

var member_value: int = 7

func _ready() -> void:
	var local_value: int = 42
	breakpoint
	local_value += 1
	print("COMPILED_DEBUG_RESULT ", JSON.stringify({"compiled": get_script().get_source_code().is_empty(), "value": local_value}))
	# Let the asynchronous debugger transport flush before closing the process.
	await get_tree().create_timer(0.25).timeout
	get_tree().quit()
