/**************************************************************************/
/*  test_gdscript_compiler_disabled.h                                     */
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

#ifdef GDSCRIPT_NO_COMPILER

#include "../gdscript.h"
#include "../gdscript_compilation_context.h"

#include "tests/test_macros.h"

namespace GDScriptTests {

TEST_CASE("[GDScript][Bytecode] Compiler-free templates reject source compilation") {
	Ref<GDScript> script;
	script.instantiate();
	const uint64_t before = GDScriptCompilationContext::get_source_pipeline_entries();
	CHECK(script->reload() == ERR_UNAVAILABLE);
	CHECK(script->load_source_code("res://source.gd") == ERR_UNAVAILABLE);
	ERR_PRINT_OFF;
	script->set_source_code("extends RefCounted");
	script->set_binary_tokens_source(String("tokens").to_utf8_buffer());
	ERR_PRINT_ON;
	CHECK(script->get_source_code().is_empty());
	CHECK(script->get_binary_tokens_source().is_empty());
	CHECK(script->get_as_binary_tokens().is_empty());
	CHECK_FALSE(script->is_valid());
	CHECK(GDScriptCompilationContext::get_source_pipeline_entries() == before);
}

} // namespace GDScriptTests

#endif // GDSCRIPT_NO_COMPILER
