#pragma once

#include <solanaceae/file/file2.hpp>

#include <memory>
#include <string_view>

std::unique_ptr<File2I> construct_file2_rw_mapped(std::string_view file_path, int64_t file_size = -1);
std::unique_ptr<File2I> construct_file2_r_mapped(std::string_view file_path);

