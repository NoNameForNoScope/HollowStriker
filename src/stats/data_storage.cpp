#include "data_storage.hpp"
#include <filesystem>

namespace bodycam {

bool DataStorage::ensure_dir() const {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return std::filesystem::is_directory(dir, ec);
}

}
