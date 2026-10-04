/**************************************************************************/
/*  gdscript_bytecode_instructions.cpp                                    */
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

#include "gdscript_bytecode_instructions.h"

#include "gdscript_function.h"

namespace {

using Function = GDScriptFunction;

// p_remaining is checked before every operand read. Widths include the opcode
// and LOAD_INSTRUCTION_ARGS' count word (the disassembler advances it separately).
int instruction_width(const int *p_code, int p_remaining, int p_instruction_args_size) {
	const int opcode = p_code[0];
	int width = 0;
	if ((opcode >= Function::OPCODE_ITERATE_BEGIN && opcode < Function::OPCODE_ITERATE_BEGIN_RANGE) ||
			(opcode >= Function::OPCODE_ITERATE && opcode < Function::OPCODE_ITERATE_RANGE)) {
		width = 5;
	} else if (opcode >= Function::OPCODE_TYPE_ADJUST_BOOL && opcode <= Function::OPCODE_TYPE_ADJUST_PACKED_VECTOR4_ARRAY) {
		width = 2;
	} else {
		switch (opcode) {
			case Function::OPCODE_JUMP_TO_DEF_ARGUMENT:
			case Function::OPCODE_BREAKPOINT:
			case Function::OPCODE_END:
				width = 1;
				break;
			case Function::OPCODE_ASSIGN_NULL:
			case Function::OPCODE_ASSIGN_TRUE:
			case Function::OPCODE_ASSIGN_FALSE:
			case Function::OPCODE_AWAIT:
			case Function::OPCODE_AWAIT_RESUME:
			case Function::OPCODE_JUMP:
			case Function::OPCODE_RETURN:
			case Function::OPCODE_LINE:
				width = 2;
				break;
			case Function::OPCODE_SET_MEMBER:
			case Function::OPCODE_GET_MEMBER:
			case Function::OPCODE_ASSIGN:
			case Function::OPCODE_JUMP_IF:
			case Function::OPCODE_JUMP_IF_NOT:
			case Function::OPCODE_JUMP_IF_SHARED:
			case Function::OPCODE_RETURN_TYPED_BUILTIN:
			case Function::OPCODE_RETURN_TYPED_NATIVE:
			case Function::OPCODE_RETURN_TYPED_SCRIPT:
			case Function::OPCODE_STORE_GLOBAL:
			case Function::OPCODE_STORE_NAMED_GLOBAL:
			case Function::OPCODE_ASSERT:
				width = 3;
				break;
			case Function::OPCODE_TYPE_TEST_BUILTIN:
			case Function::OPCODE_TYPE_TEST_NATIVE:
			case Function::OPCODE_TYPE_TEST_SCRIPT:
			case Function::OPCODE_SET_KEYED:
			case Function::OPCODE_GET_KEYED:
			case Function::OPCODE_SET_NAMED:
			case Function::OPCODE_SET_NAMED_VALIDATED:
			case Function::OPCODE_GET_NAMED:
			case Function::OPCODE_GET_NAMED_VALIDATED:
			case Function::OPCODE_SET_STATIC_VARIABLE:
			case Function::OPCODE_GET_STATIC_VARIABLE:
			case Function::OPCODE_ASSIGN_TYPED_BUILTIN:
			case Function::OPCODE_ASSIGN_TYPED_NATIVE:
			case Function::OPCODE_ASSIGN_TYPED_SCRIPT:
			case Function::OPCODE_CAST_TO_BUILTIN:
			case Function::OPCODE_CAST_TO_NATIVE:
			case Function::OPCODE_CAST_TO_SCRIPT:
				width = 4;
				break;
			case Function::OPCODE_OPERATOR_VALIDATED:
			case Function::OPCODE_SET_KEYED_VALIDATED:
			case Function::OPCODE_SET_INDEXED_VALIDATED:
			case Function::OPCODE_GET_KEYED_VALIDATED:
			case Function::OPCODE_GET_INDEXED_VALIDATED:
			case Function::OPCODE_RETURN_TYPED_ARRAY:
				width = 5;
				break;
			case Function::OPCODE_OPERATOR:
			case Function::OPCODE_TYPE_TEST_ARRAY:
			case Function::OPCODE_ASSIGN_TYPED_ARRAY:
			case Function::OPCODE_ITERATE_RANGE:
				width = 6;
				break;
			case Function::OPCODE_ITERATE_BEGIN_RANGE:
				width = 7;
				break;
			case Function::OPCODE_RETURN_TYPED_DICTIONARY:
				width = 8;
				break;
			case Function::OPCODE_TYPE_TEST_DICTIONARY:
			case Function::OPCODE_ASSIGN_TYPED_DICTIONARY:
				width = 9;
				break;
		}
	}
	if (width) {
		return width <= p_remaining ? width : 0;
	}

	int tail_words = 2;
	int extra_addresses = 1;
	int argument_multiplier = 1;
	int argument_offset = 0;
	switch (opcode) {
		case Function::OPCODE_CONSTRUCT:
		case Function::OPCODE_CONSTRUCT_VALIDATED:
		case Function::OPCODE_CALL_UTILITY:
		case Function::OPCODE_CALL_UTILITY_VALIDATED:
		case Function::OPCODE_CALL_GDSCRIPT_UTILITY:
		case Function::OPCODE_CALL_SELF_BASE:
		case Function::OPCODE_CALL_NATIVE_STATIC_VALIDATED_RETURN:
		case Function::OPCODE_CALL_NATIVE_STATIC_VALIDATED_NO_RETURN:
		case Function::OPCODE_CREATE_LAMBDA:
		case Function::OPCODE_CREATE_SELF_LAMBDA:
			break;
		case Function::OPCODE_CALL:
		case Function::OPCODE_CALL_RETURN:
		case Function::OPCODE_CALL_ASYNC:
		case Function::OPCODE_CALL_METHOD_BIND:
		case Function::OPCODE_CALL_METHOD_BIND_RET:
		case Function::OPCODE_CALL_METHOD_BIND_VALIDATED_RETURN:
		case Function::OPCODE_CALL_METHOD_BIND_VALIDATED_NO_RETURN:
		case Function::OPCODE_CALL_BUILTIN_TYPE_VALIDATED:
			extra_addresses = 2;
			break;
		case Function::OPCODE_CALL_BUILTIN_STATIC:
			tail_words = 3;
			argument_offset = 2;
			break;
		case Function::OPCODE_CALL_NATIVE_STATIC:
			argument_offset = 1;
			break;
		case Function::OPCODE_CONSTRUCT_ARRAY:
			tail_words = 1;
			break;
		case Function::OPCODE_CONSTRUCT_DICTIONARY:
			tail_words = 1;
			argument_multiplier = 2;
			break;
		case Function::OPCODE_CONSTRUCT_TYPED_ARRAY:
			tail_words = 3;
			extra_addresses = 2;
			break;
		case Function::OPCODE_CONSTRUCT_TYPED_DICTIONARY:
			tail_words = 5;
			extra_addresses = 3;
			argument_multiplier = 2;
			break;
		default:
			return 0;
	}
	if (p_remaining < 2 + tail_words) {
		return 0;
	}
	const int addresses = p_code[1];
	if (addresses < extra_addresses || addresses > p_instruction_args_size || addresses > p_remaining - 2 - tail_words) {
		return 0;
	}
	const int arguments = p_code[2 + addresses + argument_offset];
	if (arguments < 0 || int64_t(arguments) * argument_multiplier + extra_addresses != addresses) {
		return 0;
	}
	return 2 + addresses + tail_words;
}

} // namespace

bool GDScriptBytecodeInstructions::validate_layout(const Vector<int> &p_code, int p_instruction_args_size, const Vector<int> &p_default_arguments) {
	const int size = p_code.size();
	if (size == 0 || p_instruction_args_size < 0) {
		return false;
	}
	const int *code = p_code.ptr();
	Vector<uint8_t> boundaries;
	if (boundaries.resize(size) != OK) {
		return false;
	}
	boundaries.fill(0);
	uint8_t *starts = boundaries.ptrw();
	int last = 0;
	for (int ip = 0; ip < size;) {
		const int width = instruction_width(code + ip, size - ip, p_instruction_args_size);
		if (width == 0) {
			return false;
		}
		starts[ip] = 1;
		last = ip;
		ip += width;
	}
	// Normal codegen always appends END. Never allow fallthrough past the image.
	if (code[last] != Function::OPCODE_END) {
		return false;
	}
	for (const int target : p_default_arguments) {
		if (target < 0 || target >= size || !starts[target]) {
			return false;
		}
	}
	for (int ip = 0; ip < size; ip++) {
		if (!starts[ip]) {
			continue;
		}
		const int opcode = code[ip];
		int jump_offset = 0;
		if (opcode == Function::OPCODE_JUMP) {
			jump_offset = 1;
		} else if (opcode == Function::OPCODE_JUMP_IF || opcode == Function::OPCODE_JUMP_IF_NOT || opcode == Function::OPCODE_JUMP_IF_SHARED) {
			jump_offset = 2;
		} else if ((opcode >= Function::OPCODE_ITERATE_BEGIN && opcode < Function::OPCODE_ITERATE_BEGIN_RANGE) ||
				(opcode >= Function::OPCODE_ITERATE && opcode < Function::OPCODE_ITERATE_RANGE)) {
			jump_offset = 4;
		} else if (opcode == Function::OPCODE_ITERATE_BEGIN_RANGE) {
			jump_offset = 6;
		} else if (opcode == Function::OPCODE_ITERATE_RANGE) {
			jump_offset = 5;
		}
		if (jump_offset) {
			const int target = code[ip + jump_offset];
			if (target < 0 || target >= size || !starts[target]) {
				return false;
			}
		}
		if (opcode == Function::OPCODE_JUMP_TO_DEF_ARGUMENT && p_default_arguments.is_empty()) {
			return false;
		}
		// Synchronous await reads RESUME's destination and skips both instructions.
		if (opcode == Function::OPCODE_AWAIT && (size - ip < 5 || !starts[ip + 2] || code[ip + 2] != Function::OPCODE_AWAIT_RESUME || !starts[ip + 4])) {
			return false;
		}
		if (opcode == Function::OPCODE_AWAIT_RESUME && (ip < 2 || !starts[ip - 2] || code[ip - 2] != Function::OPCODE_AWAIT)) {
			return false;
		}
	}
	return true;
}
