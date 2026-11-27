// SPDX-License-Identifier: Apache-2.0
#include "gba_save.hpp"
#include <fstream>
#include <random>
#include <utility>

namespace rse::session {
namespace {
namespace fs = std::filesystem;

// Reserve a unique staging directory on the destination filesystem. A failed
// flush must never truncate the destination or remove another writer's file.
class StagingFile {
public:
    explicit StagingFile(const fs::path& parent) {
        std::random_device random;
        for (unsigned attempt = 0; attempt < 16; ++attempt) {
            auto directory = parent / (".rse-save-" + std::to_string(random()));
            auto file = directory / "flash";
            if (!fs::create_directory(directory)) continue;
            directory_ = std::move(directory);
            file_ = std::move(file);
            return;
        }
        throw std::runtime_error("Could not reserve a temporary save file");
    }
    StagingFile(const StagingFile&) = delete;
    StagingFile& operator=(const StagingFile&) = delete;
    ~StagingFile() {
        std::error_code ignored;
        fs::remove(file_, ignored);
        fs::remove(directory_, ignored);
    }
    const fs::path& path() const noexcept { return file_; }

private:
    fs::path directory_, file_;
};

void replace_save(const fs::path& path, std::span<const std::uint8_t> image) {
    StagingFile staging(path.parent_path());
    std::ofstream output(staging.path(), std::ios::binary);
    output.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    output.close();
    if (!output) throw std::runtime_error("Could not write and close the temporary save file");
    fs::rename(staging.path(), path);
}
}

FileBackedGba::FileBackedGba(std::span<const std::uint8_t> bios, std::span<const std::uint8_t> rom,
                             const fs::path& cartridge)
    : gba::Session(bios, rom) {
    if (!flash()) return;
    save_ = fs::canonical(cartridge);
    save_ += ".sav";
    const auto status = fs::symlink_status(save_);
    if (!fs::exists(status)) return;
    if (!fs::is_regular_file(status)) throw std::runtime_error("GBA save is not a regular file: " + save_.string());
    std::ifstream input(save_, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Cannot open GBA save: " + save_.string());
    std::array<std::uint8_t, cartridge::Flash1M::image_size> image;
    if (input.tellg() != static_cast<std::streamoff>(image.size()))
        throw std::runtime_error("GBA save must be exactly 131072 bytes: " + save_.string());
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(image.data()), static_cast<std::streamsize>(image.size())))
        throw std::runtime_error("Cannot read complete GBA save: " + save_.string());
    flash()->load_image(image);
}

void FileBackedGba::flush_save() {
    if (!flash() || !flash()->dirty()) return;
    try {
        replace_save(save_, flash()->image());
    } catch (const std::exception& error) {
        throw std::runtime_error("Cannot flush GBA save " + save_.string() + ": " + error.what());
    }
    flash()->mark_clean();
}

}
