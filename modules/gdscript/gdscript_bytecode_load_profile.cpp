/**************************************************************************/
/*  gdscript_bytecode_load_profile.cpp                                    */
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

#include "gdscript_bytecode_load_profile.h"

#ifdef DEBUG_ENABLED
#include "core/config/project_settings.h"
#include "core/io/json.h"
#include "core/os/os.h"
#endif

GDScriptBytecodeLoadProfile::GDScriptBytecodeLoadProfile(const String &p_path) {
#ifdef DEBUG_ENABLED
	enabled = ProjectSettings::get_singleton()->get_setting("debug/gdscript/compiled_load_profile", false);
	if (enabled) {
		path = p_path;
		started = checkpoint = OS::get_singleton()->get_ticks_usec();
	}
#endif
}

void GDScriptBytecodeLoadProfile::phase(Phase p_phase) {
#ifdef DEBUG_ENABLED
	if (enabled) {
		const uint64_t now = OS::get_singleton()->get_ticks_usec();
		durations[active] += now - checkpoint;
		if (active == LINK) {
			link_durations[link_active] += now - link_checkpoint;
		}
		if (active == CONTAINERS) {
			hydration_durations[hydration_active] += now - hydration_checkpoint;
		}
		checkpoint = now;
		active = p_phase;
		if (active == LINK) {
			link_checkpoint = now;
			link_active = LINK_OTHER;
		}
		if (active == CONTAINERS) {
			hydration_checkpoint = now;
			hydration_active = HYDRATION_OTHER;
		}
	}
#endif
}

GDScriptBytecodeLoadProfile::LinkPhase GDScriptBytecodeLoadProfile::link_phase(LinkPhase p_phase) {
#ifdef DEBUG_ENABLED
	if (enabled && active == LINK) {
		const uint64_t now = OS::get_singleton()->get_ticks_usec();
		link_durations[link_active] += now - link_checkpoint;
		link_checkpoint = now;
		const LinkPhase previous = link_active;
		link_active = p_phase;
		return previous;
	}
#endif
	return LINK_OTHER;
}

GDScriptBytecodeLoadProfile::HydrationPhase GDScriptBytecodeLoadProfile::hydration_phase(HydrationPhase p_phase) {
#ifdef DEBUG_ENABLED
	if (enabled && active == CONTAINERS) {
		const uint64_t now = OS::get_singleton()->get_ticks_usec();
		hydration_durations[hydration_active] += now - hydration_checkpoint;
		hydration_checkpoint = now;
		const HydrationPhase previous = hydration_active;
		hydration_active = p_phase;
		return previous;
	}
#endif
	return HYDRATION_OTHER;
}

void GDScriptBytecodeLoadProfile::count_hydration(HydrationPhase p_phase) {
#ifdef DEBUG_ENABLED
	if (enabled && active == CONTAINERS) {
		hydration_counts[p_phase]++;
	}
#endif
}

void GDScriptBytecodeLoadProfile::count_function() {
#ifdef DEBUG_ENABLED
	if (enabled) {
		functions++;
	}
#endif
}

void GDScriptBytecodeLoadProfile::inventory(int64_t p_bytes, int64_t p_scripts) {
#ifdef DEBUG_ENABLED
	bytes = p_bytes;
	scripts = p_scripts;
#endif
}

void GDScriptBytecodeLoadProfile::complete(bool p_reused) {
#ifdef DEBUG_ENABLED
	succeeded = true;
	reused = p_reused;
#endif
	phase(CLEANUP);
}

GDScriptBytecodeLoadProfile::~GDScriptBytecodeLoadProfile() {
#ifdef DEBUG_ENABLED
	if (enabled) {
		const uint64_t finished = OS::get_singleton()->get_ticks_usec();
		durations[active] += finished - checkpoint;
		if (active == LINK) {
			link_durations[link_active] += finished - link_checkpoint;
		}
		if (active == CONTAINERS) {
			hydration_durations[hydration_active] += finished - hydration_checkpoint;
		}
		const char *names[PHASE_COUNT] = { "read", "decode", "hash", "validate", "allocate", "link", "containers", "initialize", "publish", "cleanup" };
		Dictionary phases;
		for (int index = 0; index < PHASE_COUNT; index++) {
			phases[names[index]] = int64_t(durations[index]);
		}
		const char *link_names[LINK_PHASE_COUNT] = { "class_metadata", "function_metadata", "constants", "debug_metadata", "native_bindings", "function_finalize", "other" };
		Dictionary link_phases;
		for (int index = 0; index < LINK_PHASE_COUNT; index++) {
			link_phases[link_names[index]] = int64_t(link_durations[index]);
		}
		const char *hydration_names[HYDRATION_PHASE_COUNT] = { "container_values", "script_bindings", "resource_load", "static_initializers", "other" };
		Dictionary hydration_phases;
		Dictionary hydration_operations;
		for (int index = 0; index < HYDRATION_PHASE_COUNT; index++) {
			hydration_phases[hydration_names[index]] = int64_t(hydration_durations[index]);
			hydration_operations[hydration_names[index]] = int64_t(hydration_counts[index]);
		}
		Dictionary report;
		report["path"] = path;
		report["succeeded"] = succeeded;
		report["reused"] = reused;
		report["bytes"] = bytes;
		report["scripts"] = scripts;
		report["phases_usec"] = phases;
		report["link_phases_usec"] = link_phases;
		report["hydration_phases_usec"] = hydration_phases;
		report["hydration_operations"] = hydration_operations;
		report["functions"] = functions;
		report["total_usec"] = int64_t(finished - started);
		print_line("GDSCRIPT_BYTECODE_LOAD " + JSON::stringify(report));
	}
#endif
}
