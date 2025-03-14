#pragma once

#include <solanaceae/object_store/object_store.hpp>

#include <string>
#include <string_view>
#include <memory>

namespace Backends {

// fwd to hide the threading headers
struct SHA1MappedFilesystem_InfoBuilderState;

struct SHA1MappedFilesystem : public StorageBackendIMeta, public StorageBackendIFile2 {
	ObjectStore2& _os;
	std::unique_ptr<SHA1MappedFilesystem_InfoBuilderState> _ibs;

	SHA1MappedFilesystem(
		ObjectStore2& os
	);
	~SHA1MappedFilesystem(void);

	// pull from info builder queue
	// call from main thread (os thread?) often
	void tick(float current_time);

	ObjectHandle newObject(ByteSpan id, bool throw_construct = true) override;

	// performs async file hashing
	// create message in cb
	void newFromFile(std::string_view file_name, std::string_view file_path, std::function<void(ObjectHandle o)>&& cb/*, bool merge_preexisting = false*/);

	// might return pre-existing?
	ObjectHandle newFromInfoHash(ByteSpan info_hash);

	std::unique_ptr<File2I> file2(Object o, FILE2_FLAGS flags) override;
};

} // Backends

