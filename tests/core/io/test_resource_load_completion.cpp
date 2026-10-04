/**************************************************************************/
/*  test_resource_load_completion.cpp                                     */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_resource_load_completion)

#include "core/io/resource_loader.h"
#include "core/os/os.h"
#include "core/os/semaphore.h"
#include "core/os/thread.h"
#include "tests/test_utils.h"

namespace TestResourceLoadCompletion {

#ifdef THREADS_ENABLED
class CompletionLoader : public ResourceFormatLoader {
public:
	String producer_path = TestUtils::get_temp_path("producer.load_completion");
	String dependent_path = TestUtils::get_temp_path("dependent.load_completion");
	String alias_path = TestUtils::get_temp_path("completion_alias.tres");
	Semaphore published;
	Semaphore release_decoder;
	Semaphore dependency_read;
	SafeFlag dependency_loaded;

	void get_recognized_extensions(List<String> *p_extensions) const override {
		p_extensions->push_back("load_completion");
	}
	bool handles_type(const String &p_type) const override { return p_type.is_empty() || p_type == "Resource"; }
	String get_resource_type(const String &p_path) const override { return p_path.get_extension() == "load_completion" ? "Resource" : ""; }

	Ref<Resource> load(const String &p_path, const String &p_original_path, Error *r_error, bool p_use_sub_threads, float *r_progress, CacheMode p_cache_mode) override {
		Ref<Resource> resource;
		resource.instantiate();
		if (p_path == producer_path) {
			resource->set_path(alias_path);
			published.post();
			release_decoder.wait();
			resource->set_meta("ready", true);
		} else {
			// A dependent decoder can use a shell to resolve a cycle. Its external
			// callers must still wait for the producer, even after this task ends.
			Ref<Resource> dependency = ResourceLoader::load(alias_path);
			dependency_loaded.set_to(dependency.is_valid());
			resource->set_meta("dependency", dependency);
			dependency_read.post();
		}
		if (r_error) {
			*r_error = OK;
		}
		return resource;
	}
};

class CompletionFixture {
public:
	Ref<CompletionLoader> loader;
	Vector<String> requests;
	Thread reader;
	Ref<Resource> reader_result;

	CompletionFixture() {
		loader.instantiate();
		ResourceLoader::add_resource_format_loader(loader, true);
	}

	bool request(const String &p_path) {
		if (ResourceLoader::load_threaded_request(p_path, "", true) != OK) {
			return false;
		}
		requests.push_back(p_path);
		return true;
	}

	static void read_alias(void *p_data) {
		CompletionFixture *fixture = static_cast<CompletionFixture *>(p_data);
		fixture->reader_result = ResourceLoader::load(fixture->loader->alias_path);
	}

	~CompletionFixture() {
		// Always unblock workers, including on an assertion failure.
		loader->release_decoder.post();
		if (reader.is_started()) {
			reader.wait_to_finish();
		}
		for (const String &path : requests) {
			ResourceLoader::load_threaded_get(path);
		}
		ResourceLoader::remove_resource_format_loader(loader);
	}
};

static bool await_signal(Semaphore &p_signal) {
	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + 10000;
	while (!p_signal.try_wait()) {
		if (OS::get_singleton()->get_ticks_msec() >= deadline) {
			return false;
		}
		OS::get_singleton()->delay_usec(1000);
	}
	return true;
}

TEST_CASE("[ResourceLoader] Cached shells wait for their decoder and dependent group") {
	CompletionFixture fixture;
	REQUIRE(fixture.request(fixture.loader->producer_path));
	REQUIRE(await_signal(fixture.loader->published));
	REQUIRE(fixture.request(fixture.loader->alias_path));
	CHECK(ResourceLoader::load_threaded_get_status(fixture.loader->alias_path) == ResourceLoader::THREAD_LOAD_IN_PROGRESS);
	REQUIRE(fixture.reader.start(CompletionFixture::read_alias, &fixture) != Thread::UNASSIGNED_ID);

	SUBCASE("Completed dependencies retain the producer's completion group") {
		REQUIRE(fixture.request(fixture.loader->dependent_path));
		REQUIRE(await_signal(fixture.loader->dependency_read));
		CHECK(fixture.loader->dependency_loaded.is_set());
		CHECK(ResourceLoader::load_threaded_get_status(fixture.loader->dependent_path) == ResourceLoader::THREAD_LOAD_IN_PROGRESS);
	}

	fixture.loader->release_decoder.post();
	fixture.reader.wait_to_finish();
	REQUIRE(fixture.reader_result.is_valid());
	CHECK(fixture.reader_result->get_meta("ready", false) == Variant(true));
	Ref<Resource> alias = ResourceLoader::load_threaded_get(fixture.loader->alias_path);
	Ref<Resource> producer = ResourceLoader::load_threaded_get(fixture.loader->producer_path);
	CHECK(alias == producer);
	CHECK(alias == fixture.reader_result);
	fixture.requests.erase(fixture.loader->alias_path);
	fixture.requests.erase(fixture.loader->producer_path);
}
#endif // THREADS_ENABLED

TEST_CASE("[ResourceLoader] Cache publication preserves the first resource") {
	const String path = TestUtils::get_temp_path("first_resource.tres");
	Ref<Resource> first;
	Ref<Resource> second;
	first.instantiate();
	second.instantiate();
	ResourceLoader::_cache_resource_if_missing(first, path);
	ResourceLoader::_cache_resource_if_missing(second, path);
	CHECK(ResourceCache::get_ref(path) == first);
	CHECK(second->get_path().is_empty());
	Ref<ResourceLoadCompletion> completion;
	CHECK(ResourceCache::get_ref(path, &completion) == first);
	CHECK(completion.is_null());
}

} // namespace TestResourceLoadCompletion
