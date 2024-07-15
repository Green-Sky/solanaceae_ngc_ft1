#include "./file_constructor.hpp"

#include "./file_rw_mapped.hpp"

std::unique_ptr<File2I> construct_file2_rw_mapped(std::string_view file_path, int64_t file_size) {
	return std::make_unique<File2RWMapped>(file_path, file_size);
}

