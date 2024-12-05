#pragma once

#include <cstdint>

// uint32_t - same as tox friend ft
// TODO: fill in toxfriend file kinds
enum class NGCFT1_file_kind : uint32_t {
	//INVALID = 0u, // DATA?

	// id:
	// group (implicit)
	// peer pub key + msg_id
	NGC_HS1_MESSAGE_BY_ID = 1u, // history sync PoC 1
	// TODO: oops, 1 should be avatar v1

	// id: TOX_FILE_ID_LENGTH (32) bytes
	// this is basically and id and probably not a hash, like the tox friend api
	// this id can be unique between 2 peers
	ID = 8u, // TODO: this is actually DATA and 0

	// id: hash of the info, like a torrent infohash (using the same hash as the data)
	// TODO: determain internal format
	// draft: (for single file)
	//   - 256 bytes | filename
	//   - 8bytes | file size
	//   - 4bytes | chunk size
	//   - array of chunk hashes (ids) [
	//     - SHA1 bytes (20)
	//   - ]
	HASH_SHA1_INFO,
	// draft: (for single file) v2
	//   - c-string | filename
	//   - 8bytes | file size
	//   - 4bytes | chunk size
	//   - array of chunk hashes (ids) [
	//     - SHA1 bytes (20)
	//   - ]
	HASH_SHA1_INFO2,
	// draft: multiple files
	//   - 4bytes | number of filenames
	//   - array of filenames (variable length c-strings) [
	//     - c-string | filename (including path and '/' as dir seperator)
	//   - ]
	//   - 256 bytes | filename
	//   - 8bytes | file size
	//   - fixed chunk size of 4kb
	//   - array of chunk hashes (ids) [
	//     - SHAX bytes
	//   - ]
	HASH_SHA1_INFO3,
	HASH_SHA2_INFO, // hm?

	// id: hash of the content
	// TODO: fixed chunk size or variable (defined in info)
	// if "variable" sized, it can be aliased with TORRENT_V1_CHUNK in the implementation
	HASH_SHA1_CHUNK,
	HASH_SHA2_CHUNK,

	// TODO: design the same thing again for tox? (msg_pack instead of bencode?)
	// id: infohash
	TORRENT_V1_METAINFO,
	// id: sha1
	TORRENT_V1_PIECE, // alias with SHA1_CHUNK?

	// TODO: fix all the v2 stuff here
	// id: infohash
	// in v2, metainfo contains only the root hashes of the merkletree(s)
	TORRENT_V2_METAINFO,
	// id: root hash
	// contains all the leaf hashes for a file root hash
	TORRENT_V2_FILE_HASHES,
	// id: sha256
	// always of size 16KiB, except if last piece in file
	TORRENT_V2_PIECE,

	// https://gist.github.com/Green-Sky/440cd9817a7114786850eb4c62dc57c3
	// id: ts start, ts end
	HS2_RANGE_TIME = 0x00000f00, // TODO: remove, did not survive
	HS2_RANGE_TIME_MSGPACK = 0x00000f02,
};

