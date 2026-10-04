extends SceneTree

var server := TCPServer.new()
var stream: StreamPeerTCP
var packets := PacketPeerStream.new()
var thread_id: int = 0
var stops: int = 0
var remaining_vars: int = 0
var frame_vars: Dictionary = {}
var stack_checked := false
var deadline: int = 0
var result_path: String
var finished := false

func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 2:
		quit(1)
		return
	result_path = args[1]
	deadline = Time.get_ticks_msec() + 90000
	if server.listen(int(args[0]), "127.0.0.1") != OK:
		finish(false, "Cannot listen on debugger port")
		return
	print("COMPILED_DEBUG_LISTENING")

func finish(passed: bool, reason: String) -> void:
	if finished:
		return
	finished = true
	var file := FileAccess.open(result_path, FileAccess.WRITE)
	if file != null:
		file.store_string(JSON.stringify({"passed": passed, "reason": reason, "stops": stops}))
		file.close()
	print("COMPILED_DEBUG_HOST ", "PASS" if passed else "FAIL: " + reason)
	quit(0 if passed else 1)

func send(command: String, data: Array = []) -> void:
	if packets.put_var([command, thread_id, data]) != OK:
		finish(false, "Cannot send debugger command")

func _process(_delta: float) -> bool:
	if finished:
		return false
	if Time.get_ticks_msec() > deadline:
		finish(false, "Debugger fixture timed out")
		return false
	if stream == null:
		if server.is_connection_available():
			stream = server.take_connection()
			packets.stream_peer = stream
		return false
	stream.poll()
	while packets.get_available_packet_count() > 0 and not finished:
		var message: Variant = packets.get_var()
		if not message is Array or message.size() != 3:
			finish(false, "Malformed debugger message")
			break
		thread_id = message[1]
		var data: Array = message[2]
		match message[0]:
			"debug_enter":
				stops += 1
				stack_checked = false
				frame_vars.clear()
				var expected_reason := "Breakpoint Statement" if stops == 1 else "Breakpoint"
				if stops > 3 or data.size() != 4 or not data[0] or data[1] != expected_reason or not data[2]:
					finish(false, "Unexpected debugger stop: " + str(data))
					break
				send("get_stack_dump")
			"stack_dump":
				if data.size() < 4 or data[1] != "res://main.gd" or data[2] != 6 + stops or data[3] != "_ready":
					finish(false, "Incorrect logical path, line or frame: " + str(data))
					break
				stack_checked = true
				send("get_stack_frame_vars", [0])
			"stack_frame_vars":
				remaining_vars = data[0]
				if remaining_vars <= 0:
					finish(false, "Missing frame variables")
			"stack_frame_var":
				if data[0] in ["local_value", "member_value"]:
					frame_vars[data[0]] = data[3]
				remaining_vars -= 1
				if remaining_vars == 0:
					var expected_local := 43 if stops == 3 else 42
					if not stack_checked or frame_vars.get("local_value") != expected_local or frame_vars.get("member_value") != 7:
						finish(false, "Incorrect frame variables: " + str(frame_vars))
						break
					send("continue" if stops == 3 else "next")
			"debug_exit":
				if stops == 3:
					finish(true, "Breakpoint, two steps, frame inspection and continue")
			"error":
				finish(false, "Runtime debugger error: " + str(data))
	return false
