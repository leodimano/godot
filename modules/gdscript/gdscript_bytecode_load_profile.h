/**************************************************************************/
/*  gdscript_bytecode_load_profile.h                                      */
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

#include "core/string/ustring.h"

// Opt-in Debug diagnostics. Timings are not persisted in the artifact.
class GDScriptBytecodeLoadProfile {
public:
	enum Phase {
		READ,
		DECODE,
		HASH,
		VALIDATE,
		ALLOCATE,
		LINK,
		CONTAINERS,
		INITIALIZE,
		PUBLISH,
		CLEANUP,
		PHASE_COUNT,
	};
	enum LinkPhase {
		CLASS_METADATA,
		FUNCTION_METADATA,
		FUNCTION_CONSTANTS,
		FUNCTION_DEBUG,
		NATIVE_BINDINGS,
		FUNCTION_FINALIZE,
		LINK_OTHER,
		LINK_PHASE_COUNT,
	};
	enum HydrationPhase {
		CONTAINER_VALUES,
		SCRIPT_BINDINGS,
		RESOURCE_LOAD,
		STATIC_INITIALIZERS,
		HYDRATION_OTHER,
		HYDRATION_PHASE_COUNT,
	};

private:
#ifdef DEBUG_ENABLED
	bool enabled = false;
	bool succeeded = false;
	bool reused = false;
	String path;
	uint64_t started = 0;
	uint64_t checkpoint = 0;
	uint64_t durations[PHASE_COUNT] = {};
	uint64_t link_durations[LINK_PHASE_COUNT] = {};
	uint64_t link_checkpoint = 0;
	LinkPhase link_active = LINK_OTHER;
	uint64_t hydration_durations[HYDRATION_PHASE_COUNT] = {};
	uint64_t hydration_counts[HYDRATION_PHASE_COUNT] = {};
	uint64_t hydration_checkpoint = 0;
	HydrationPhase hydration_active = HYDRATION_OTHER;
	int64_t functions = 0;
	Phase active = READ;
	int64_t bytes = 0;
	int64_t scripts = 0;
#endif

public:
	explicit GDScriptBytecodeLoadProfile(const String &p_path);
	_FORCE_INLINE_ bool is_enabled() const {
#ifdef DEBUG_ENABLED
		return enabled;
#else
		return false;
#endif
	}
	void phase(Phase p_phase);
	LinkPhase link_phase(LinkPhase p_phase);
	HydrationPhase hydration_phase(HydrationPhase p_phase);
	void count_hydration(HydrationPhase p_phase);
	void count_function();
	void inventory(int64_t p_bytes, int64_t p_scripts);
	void complete(bool p_reused = false);
	~GDScriptBytecodeLoadProfile();
};

// Exclusive timing: nested functions restore their parent's active category.
class GDScriptBytecodeLinkScope {
	GDScriptBytecodeLoadProfile *profile = nullptr;
	GDScriptBytecodeLoadProfile::LinkPhase previous = GDScriptBytecodeLoadProfile::LINK_OTHER;

public:
	GDScriptBytecodeLinkScope(GDScriptBytecodeLoadProfile *p_profile, GDScriptBytecodeLoadProfile::LinkPhase p_phase) {
		if (p_profile && p_profile->is_enabled()) {
			profile = p_profile;
			previous = profile->link_phase(p_phase);
			if (p_phase == GDScriptBytecodeLoadProfile::FUNCTION_METADATA) {
				profile->count_function();
			}
		}
	}
	GDScriptBytecodeLinkScope(const GDScriptBytecodeLinkScope &) = delete;
	GDScriptBytecodeLinkScope &operator=(const GDScriptBytecodeLinkScope &) = delete;
	void phase(GDScriptBytecodeLoadProfile::LinkPhase p_phase) {
		if (profile) {
			profile->link_phase(p_phase);
		}
	}
	~GDScriptBytecodeLinkScope() {
		if (profile) {
			profile->link_phase(previous);
		}
	}
};

// Exclusive categories inside CONTAINERS, including reentrant resource scripts.
class GDScriptBytecodeHydrationScope {
	GDScriptBytecodeLoadProfile *profile = nullptr;
	GDScriptBytecodeLoadProfile::HydrationPhase previous = GDScriptBytecodeLoadProfile::HYDRATION_OTHER;

public:
	GDScriptBytecodeHydrationScope(GDScriptBytecodeLoadProfile *p_profile, GDScriptBytecodeLoadProfile::HydrationPhase p_phase) {
		if (p_profile && p_profile->is_enabled()) {
			profile = p_profile;
			previous = profile->hydration_phase(p_phase);
			profile->count_hydration(p_phase);
		}
	}
	GDScriptBytecodeHydrationScope(const GDScriptBytecodeHydrationScope &) = delete;
	GDScriptBytecodeHydrationScope &operator=(const GDScriptBytecodeHydrationScope &) = delete;
	~GDScriptBytecodeHydrationScope() {
		if (profile) {
			profile->hydration_phase(previous);
		}
	}
};
