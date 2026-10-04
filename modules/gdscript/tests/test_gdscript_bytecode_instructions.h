/**************************************************************************/
/*  test_gdscript_bytecode_instructions.h                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "../gdscript_bytecode_instructions.h"
#include "../gdscript_function.h"

#include "tests/test_macros.h"

namespace TestGDScriptBytecodeInstructions {

using Function = GDScriptFunction;

TEST_CASE("[GDScript][Bytecode] Instruction layout validates fixed and variable widths") {
	CHECK(GDScriptBytecodeInstructions::validate_layout({ Function::OPCODE_END }, 0, {}));
	CHECK(GDScriptBytecodeInstructions::validate_layout({ Function::OPCODE_OPERATOR, 0, 0, 0, Variant::OP_ADD, 0, Function::OPCODE_END }, 0, {}));
	CHECK_FALSE(GDScriptBytecodeInstructions::validate_layout({ Function::OPCODE_OPERATOR, 0, 0, 0, Variant::OP_ADD }, 0, {}));
	CHECK_FALSE(GDScriptBytecodeInstructions::validate_layout({ -1, Function::OPCODE_END }, 0, {}));
	CHECK_FALSE(GDScriptBytecodeInstructions::validate_layout({ INT_MAX, Function::OPCODE_END }, 0, {}));
	CHECK_FALSE(GDScriptBytecodeInstructions::validate_layout({}, 0, {}));
	CHECK_FALSE(GDScriptBytecodeInstructions::validate_layout({ Function::OPCODE_END }, -1, {}));
	CHECK_FALSE(GDScriptBytecodeInstructions::validate_layout({ Function::OPCODE_LINE, 1 }, 0, {}));

	const Vector<int> call = { Function::OPCODE_CALL, 2, 0, 0, 0, 0, Function::OPCODE_END };
	CHECK(GDScriptBytecodeInstructions::validate_layout(call, 2, {}));
	CHECK_FALSE(GDScriptBytecodeInstructions::validate_layout(call, 1, {}));
	for (int length = 0; length < call.size(); length++) {
		Vector<int> truncated = call;
		truncated.resize(length);
		CHECK_FALSE(GDScriptBytecodeInstructions::validate_layout(truncated, 2, {}));
	}
	Vector<int> invalid = call;
	invalid.write[1] = INT_MAX;
	CHECK_FALSE(GDScriptBytecodeInstructions::validate_layout(invalid, INT_MAX, {}));
	invalid = call;
	invalid.write[1] = -1;
	CHECK_FALSE(GDScriptBytecodeInstructions::validate_layout(invalid, 2, {}));
	invalid = call;
	invalid.write[4] = 1; // Argument count disagrees with the address count.
	CHECK_FALSE(GDScriptBytecodeInstructions::validate_layout(invalid, 2, {}));
}

TEST_CASE("[GDScript][Bytecode] Instruction layout validates jump boundaries") {
	const Vector<int> code = { Function::OPCODE_LINE, 1, Function::OPCODE_JUMP, 4, Function::OPCODE_END };
	CHECK(GDScriptBytecodeInstructions::validate_layout(code, 0, { 0, 2, 4 }));
	for (const int target : { -1, 1, 3, 5, INT_MAX }) {
		Vector<int> invalid = code;
		invalid.write[3] = target;
		CHECK_FALSE(GDScriptBytecodeInstructions::validate_layout(invalid, 0, {}));
		CHECK_FALSE(GDScriptBytecodeInstructions::validate_layout(code, 0, { target }));
	}
	CHECK_FALSE(GDScriptBytecodeInstructions::validate_layout({ Function::OPCODE_JUMP_TO_DEF_ARGUMENT, Function::OPCODE_END }, 0, {}));
	CHECK(GDScriptBytecodeInstructions::validate_layout({ Function::OPCODE_JUMP_TO_DEF_ARGUMENT, Function::OPCODE_END }, 0, { 1 }));
	const Vector<int> range = { Function::OPCODE_ITERATE_BEGIN_RANGE, 0, 0, 0, 0, 0, 7, Function::OPCODE_END };
	CHECK(GDScriptBytecodeInstructions::validate_layout(range, 0, {}));
	Vector<int> invalid_range = range;
	invalid_range.write[6] = 6;
	CHECK_FALSE(GDScriptBytecodeInstructions::validate_layout(invalid_range, 0, {}));
}

TEST_CASE("[GDScript][Bytecode] Instruction layout validates await resume pairs") {
	CHECK(GDScriptBytecodeInstructions::validate_layout({ Function::OPCODE_AWAIT, 0, Function::OPCODE_AWAIT_RESUME, 0, Function::OPCODE_END }, 0, {}));
	CHECK_FALSE(GDScriptBytecodeInstructions::validate_layout({ Function::OPCODE_AWAIT_RESUME, 0, Function::OPCODE_END }, 0, {}));
	CHECK_FALSE(GDScriptBytecodeInstructions::validate_layout({ Function::OPCODE_AWAIT, 0, Function::OPCODE_END }, 0, {}));
	CHECK_FALSE(GDScriptBytecodeInstructions::validate_layout({ Function::OPCODE_AWAIT, 0, Function::OPCODE_RETURN, 0, Function::OPCODE_END }, 0, {}));
}

} // namespace TestGDScriptBytecodeInstructions
