class Base:
	func value() -> int:
		return 41

class Derived extends Base:
	signal released

	func value() -> int:
		await released
		return 41

var completed := false

func identity(value: int) -> int:
	return value

func check_value(subject: Base) -> void:
	@warning_ignore("redundant_await")
	var value := await subject.value()
	# The validated addition must not reuse a coroutine's Object-typed slot.
	var next := identity(value) + 1
	var bound := identity.bind(next)
	print(bound.call())
	completed = true

func test():
	@warning_ignore("missing_await")
	check_value(Base.new())
	print("synchronous: ", completed)
	completed = false
	var subject := Derived.new()
	@warning_ignore("missing_await")
	check_value(subject)
	print("suspended: ", not completed)
	subject.released.emit()
	print("resumed: ", completed)
