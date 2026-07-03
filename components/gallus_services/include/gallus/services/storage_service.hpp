#pragma once

#include <cstddef>

#include "gallus/error.hpp"

/// @file storage_service.hpp
/// @brief Persistent storage on LittleFS.
///
/// Mounts the "littlefs" partition at kBasePath and offers small file
/// helpers. Anything needing more than whole-file read/write uses
/// standard POSIX/VFS calls against paths under basePath().

namespace gallus::services {

/// One entry returned by listDir().
struct DirEntry {
    char name[48];
    bool is_dir;
    size_t size;
};

class StorageService {
public:
    static constexpr const char* kBasePath = "/fs";

    StorageService() = default;
    StorageService(const StorageService&) = delete;
    StorageService& operator=(const StorageService&) = delete;

    /// Mount LittleFS (formatting the partition on first use).
    Status init();

    [[nodiscard]] bool mounted() const { return mounted_; }
    [[nodiscard]] const char* basePath() const { return kBasePath; }

    /// Filesystem capacity in bytes.
    Result<size_t> totalBytes() const;

    /// Bytes currently in use.
    Result<size_t> usedBytes() const;

    /// Replace the contents of @p path (absolute VFS path) with @p data.
    Status writeFile(const char* path, const void* data, size_t len);

    /// Read up to @p cap bytes from @p path. Returns bytes read.
    Result<size_t> readFile(const char* path, void* buf, size_t cap) const;

    [[nodiscard]] bool exists(const char* path) const;

    Status removeFile(const char* path);

    /// Create a directory (no error if it already exists).
    Status makeDir(const char* path);

    /// List entries in @p path. Returns the number written to @p out.
    Result<size_t> listDir(const char* path, DirEntry* out,
                           size_t max) const;

private:
    bool mounted_ = false;
};

}  // namespace gallus::services
