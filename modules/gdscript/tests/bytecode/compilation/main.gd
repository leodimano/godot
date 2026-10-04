extends SceneTree

func _initialize():
	var arguments := OS.get_cmdline_user_args()
	var compile_only := arguments.has("--gdscript-compile-only")
	var debug_target := not compile_only or not arguments.has("--gdscript-target-release")
	var subject := load("res://subject.gd")
	var initialized := Engine.has_meta("compilation_initialized")
	var result: int = subject.check_assertion()
	var asserted := Engine.has_meta("compilation_asserted")
	var passed := initialized == not compile_only and asserted == debug_target and result == 7
	print("GDSCRIPT_COMPILATION_TEST " + JSON.stringify({
		"compile_only": compile_only,
		"debug_target": debug_target,
		"initialized": initialized,
		"asserted": asserted,
		"passed": passed,
	}))
	quit(0 if passed else 1)
