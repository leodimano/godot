/**************************************************************************/
/*  gdscript_compilation_context.cpp                                      */
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

#include "gdscript_compilation_context.h"

#include "gdscript.h"

#include "core/os/os.h"

SafeNumeric<uint64_t> GDScriptCompilationContext::source_pipeline_entries;
SafeNumeric<uint64_t> GDScriptCompilationContext::initialization_sequence;

bool GDScriptCompilationContext::is_compile_only() {
#ifdef TOOLS_ENABLED
	return OS::get_singleton()->get_cmdline_user_args().find("--gdscript-compile-only") != nullptr;
#else
	return false;
#endif
}

bool GDScriptCompilationContext::is_debug_compilation() {
#ifdef DEBUG_ENABLED
	return !is_compile_only() || OS::get_singleton()->get_cmdline_user_args().find("--gdscript-target-release") == nullptr;
#else
	return false;
#endif
}

void GDScriptCompilationContext::record_initialization_order(GDScript *p_script) {
	// finish_compiling() can complete cyclic dependencies first. Preserve its
	// actual completion order, without executing game code in the exporter.
	p_script->compilation_initialization_order = initialization_sequence.increment();
}

void GDScriptCompilationContext::record_source_pipeline_entry() {
	source_pipeline_entries.increment();
}

uint64_t GDScriptCompilationContext::get_source_pipeline_entries() {
	return source_pipeline_entries.get();
}
