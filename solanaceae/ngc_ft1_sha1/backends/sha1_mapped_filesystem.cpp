#include "./sha1_mapped_filesystem.hpp"

#include <solanaceae/object_store/meta_components.hpp>

#include "../file_constructor.hpp"
#include "../ft1_sha1_info.hpp"
#include "../hash_utils.hpp"
#include "../components.hpp"

#include <solanaceae/util/utils.hpp>

#include <atomic>
#include <mutex>
#include <list>
#include <thread>

#include <iostream>

namespace Backends {

struct SHA1MappedFilesystem_InfoBuilderState {
	std::atomic_bool info_builder_dirty {false};
	std::mutex info_builder_queue_mutex;
	using InfoBuilderEntry = std::function<void(void)>;
	std::list<InfoBuilderEntry> info_builder_queue;
};

SHA1MappedFilesystem::SHA1MappedFilesystem(
	ObjectStore2& os
) : StorageBackendI::StorageBackendI(os), _ibs(std::make_unique<SHA1MappedFilesystem_InfoBuilderState>()) {
}

SHA1MappedFilesystem::~SHA1MappedFilesystem(void) {
}

void SHA1MappedFilesystem::tick(void) {
	if (_ibs->info_builder_dirty) {
		std::lock_guard l{_ibs->info_builder_queue_mutex};
		_ibs->info_builder_dirty = false; // set while holding lock

		for (auto& it : _ibs->info_builder_queue) {
			it();
		}
		_ibs->info_builder_queue.clear();
	}
}

ObjectHandle SHA1MappedFilesystem::newObject(ByteSpan id) {
	ObjectHandle o{_os.registry(), _os.registry().create()};

	o.emplace<ObjComp::Ephemeral::Backend>(this);
	o.emplace<ObjComp::ID>(std::vector<uint8_t>{id});
	//o.emplace<ObjComp::Ephemeral::FilePath>(object_file_path.generic_u8string());

	_os.throwEventConstruct(o);

	return o;
}

void SHA1MappedFilesystem::newFromFile(std::string_view file_name, std::string_view file_path, std::function<void(ObjectHandle o)>&& cb) {
	std::thread(std::move([
		this,
		ibs = _ibs.get(),
		cb = std::move(cb),
		file_name_ = std::string(file_name),
		file_path_ = std::string(file_path)
	]() mutable {
		// 0. open and fail
		std::unique_ptr<File2I> file_impl = construct_file2_rw_mapped(file_path_, -1);
		if (!file_impl->isGood()) {
			{
				std::lock_guard l{ibs->info_builder_queue_mutex};
				ibs->info_builder_queue.push_back([file_path_](){
					// back on iterate thread

					std::cerr << "SHA1MF error: failed opening file '" << file_path_ << "'!\n";
				});
				ibs->info_builder_dirty = true; // still in scope, set before mutex unlock
			}
			return;
		}

		// 1. build info by hashing all chunks
		FT1InfoSHA1 sha1_info;
		// build info
		sha1_info.file_name = file_name_;
		sha1_info.file_size = file_impl->_file_size; // TODO: remove the reliance on implementation details

		{ // build chunks
			// HACK: load file fully
			// ... its only a hack if its not memory mapped, but reading in chunk_sized chunks is probably a good idea anyway
			const auto file_data = file_impl->read(file_impl->_file_size, 0);
			size_t i = 0;
			for (; i + sha1_info.chunk_size < file_data.size; i += sha1_info.chunk_size) {
				sha1_info.chunks.push_back(hash_sha1(file_data.ptr+i, sha1_info.chunk_size));
			}

			if (i < file_data.size) {
				sha1_info.chunks.push_back(hash_sha1(file_data.ptr+i, file_data.size-i));
			}
		}

		file_impl.reset();

		std::lock_guard l{ibs->info_builder_queue_mutex};
		ibs->info_builder_queue.push_back(std::move([
			this,
			file_name_,
			file_path_,
			sha1_info = std::move(sha1_info),
			cb = std::move(cb)
		]() mutable { //
			// executed on iterate thread

			// reopen, cant move, since std::function needs to be copy consturctable (meh)
			std::unique_ptr<File2I> file_impl = construct_file2_rw_mapped(file_path_, sha1_info.file_size);
			if (!file_impl->isGood()) {
				std::cerr << "SHA1MF error: failed opening file '" << file_path_ << "'!\n";
				return;
			}

			// 2. hash info
			std::vector<uint8_t> sha1_info_data;
			std::vector<uint8_t> sha1_info_hash;

			std::cout << "SHA1MF info is: \n" << sha1_info;
			sha1_info_data = sha1_info.toBuffer();
			std::cout << "SHA1MF sha1_info size: " << sha1_info_data.size() << "\n";
			sha1_info_hash = hash_sha1(sha1_info_data.data(), sha1_info_data.size());
			std::cout << "SHA1MF sha1_info_hash: " << bin2hex(sha1_info_hash) << "\n";

			ObjectHandle o;
			// check if content exists
			// TODO: store "info_to_content" in reg/backend, for better lookup speed
			// rn ok, bc this is rare
			for (const auto& [it_ov, it_ih] : _os.registry().view<Components::FT1InfoSHA1Hash>().each()) {
				if (it_ih.hash == sha1_info_hash) {
					o = {_os.registry(), it_ov};
				}
			}
			//if (self->info_to_content.count(sha1_info_hash)) {
			if (static_cast<bool>(o)) {
				//ce = self->info_to_content.at(sha1_info_hash);

				// TODO: check if content is incomplete and use file instead
				if (!o.all_of<Components::FT1InfoSHA1>()) {
					o.emplace<Components::FT1InfoSHA1>(sha1_info);
				}
				if (!o.all_of<Components::FT1InfoSHA1Data>()) {
					o.emplace<Components::FT1InfoSHA1Data>(sha1_info_data);
				}

				// hash has to be set already
				// Components::FT1InfoSHA1Hash

				{ // lookup tables and have
					auto& cc = o.get_or_emplace<Components::FT1ChunkSHA1Cache>();
					cc.have_all = true;
					// skip have vec, since all
					//cc.have_chunk
					cc.have_count = sha1_info.chunks.size(); // need?

					//self->_info_to_content[sha1_info_hash] = ce;
					cc.chunk_hash_to_index.clear(); // for cpy pst
					for (size_t i = 0; i < sha1_info.chunks.size(); i++) {
						//self->_chunks[sha1_info.chunks[i]] = ce;
						cc.chunk_hash_to_index[sha1_info.chunks[i]].push_back(i);
					}
				}

				{ // file info
					// TODO: not overwrite fi? since same?
					auto& file_info = o.emplace_or_replace<Message::Components::Transfer::FileInfo>();
					file_info.file_list.emplace_back() = {std::string{file_name_}, file_impl->_file_size};
					file_info.total_size = file_impl->_file_size;

					o.emplace_or_replace<Message::Components::Transfer::FileInfoLocal>(std::vector{std::string{file_path_}});
				}

				// hmmm
				o.remove<Message::Components::Transfer::TagPaused>();

				// we dont want the info anymore
				o.remove<Components::ReRequestInfoTimer>();
			} else {
				o = newObject(ByteSpan{sha1_info_hash});

				o.emplace<Components::FT1InfoSHA1>(sha1_info);
				o.emplace<Components::FT1InfoSHA1Data>(sha1_info_data); // keep around? or file?
				o.emplace<Components::FT1InfoSHA1Hash>(sha1_info_hash);
				{ // lookup tables and have
					auto& cc = o.emplace<Components::FT1ChunkSHA1Cache>();
					cc.have_all = true;
					// skip have vec, since all
					cc.have_count = sha1_info.chunks.size(); // need?

					cc.chunk_hash_to_index.clear(); // for cpy pst
					for (size_t i = 0; i < sha1_info.chunks.size(); i++) {
						cc.chunk_hash_to_index[sha1_info.chunks[i]].push_back(i);
					}
				}

				{ // file info
					auto& file_info = o.emplace<Message::Components::Transfer::FileInfo>();
					file_info.file_list.emplace_back() = {std::string{file_name_}, file_impl->_file_size};
					file_info.total_size = file_impl->_file_size;

					o.emplace<Message::Components::Transfer::FileInfoLocal>(std::vector{std::string{file_path_}});
				}
			}

			o.emplace_or_replace<Message::Components::Transfer::File>(std::move(file_impl));

			// TODO: replace with transfers stats
			if (!o.all_of<Message::Components::Transfer::BytesSent>()) {
				o.emplace<Message::Components::Transfer::BytesSent>(0u);
			}

			cb(o);

			// TODO: earlier?
			_os.throwEventUpdate(o);
		}));
		ibs->info_builder_dirty = true; // still in scope, set before mutex unlock
	})).detach();
}

std::unique_ptr<File2I> SHA1MappedFilesystem::file2(Object ov, FILE2_FLAGS flags) {
	ObjectHandle o{_os.registry(), ov};

	if (!static_cast<bool>(o)) {
		return nullptr;
	}

	if (!o.all_of<Message::Components::Transfer::FileInfoLocal>()) {
		return nullptr;
	}

	const auto& file_list = o.get<Message::Components::Transfer::FileInfoLocal>().file_list;
	if (file_list.empty()) {
		return nullptr;
	}

	auto res = construct_file2_rw_mapped(file_list.front(), -1);
	if (!res || !res->isGood()) {
		return nullptr;
	}

	return res;
}

} // Backends

