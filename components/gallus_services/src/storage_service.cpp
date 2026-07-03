#include "gallus/services/storage_service.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_littlefs.h"

#include "gallus/log.hpp"

namespace gallus::services {

namespace {
constexpr const char* kTag = "Storage";
constexpr const char* kPartitionLabel = "littlefs";
}  // namespace

Status StorageService::init() {
    if (mounted_) {
        return Error::InvalidState;
    }

    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = kBasePath;
    conf.partition_label = kPartitionLabel;
    conf.format_if_mount_failed = true;

    const esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        Log::error(kTag, "mount failed: %s", esp_err_to_name(err));
        return fromEspErr(err);
    }
    mounted_ = true;

    size_t total = 0;
    size_t used = 0;
    if (esp_littlefs_info(kPartitionLabel, &total, &used) == ESP_OK) {
        Log::info(kTag, "mounted %s: %u / %u bytes used", kBasePath,
                  static_cast<unsigned>(used), static_cast<unsigned>(total));
    }
    return Status::success();
}

Result<size_t> StorageService::totalBytes() const {
    if (!mounted_) {
        return Error::InvalidState;
    }
    size_t total = 0;
    size_t used = 0;
    const esp_err_t err = esp_littlefs_info(kPartitionLabel, &total, &used);
    if (err != ESP_OK) {
        return fromEspErr(err);
    }
    return total;
}

Result<size_t> StorageService::usedBytes() const {
    if (!mounted_) {
        return Error::InvalidState;
    }
    size_t total = 0;
    size_t used = 0;
    const esp_err_t err = esp_littlefs_info(kPartitionLabel, &total, &used);
    if (err != ESP_OK) {
        return fromEspErr(err);
    }
    return used;
}

Status StorageService::writeFile(const char* path, const void* data,
                                 size_t len) {
    if (!mounted_) {
        return Error::InvalidState;
    }
    if (path == nullptr || (data == nullptr && len > 0)) {
        return Error::InvalidArg;
    }

    FILE* file = fopen(path, "wb");
    if (file == nullptr) {
        Log::error(kTag, "open for write failed: %s (errno %d)", path, errno);
        return Error::NotFound;
    }

    const size_t written = fwrite(data, 1, len, file);
    fclose(file);

    if (written != len) {
        Log::error(kTag, "short write: %s (%u of %u bytes)", path,
                   static_cast<unsigned>(written), static_cast<unsigned>(len));
        return Error::NoMemory;
    }
    return Status::success();
}

Result<size_t> StorageService::readFile(const char* path, void* buf,
                                        size_t cap) const {
    if (!mounted_) {
        return Error::InvalidState;
    }
    if (path == nullptr || buf == nullptr) {
        return Error::InvalidArg;
    }

    FILE* file = fopen(path, "rb");
    if (file == nullptr) {
        return Error::NotFound;
    }

    const size_t read = fread(buf, 1, cap, file);
    fclose(file);
    return read;
}

bool StorageService::exists(const char* path) const {
    if (!mounted_ || path == nullptr) {
        return false;
    }
    struct stat st = {};
    return stat(path, &st) == 0;
}

Status StorageService::removeFile(const char* path) {
    if (!mounted_) {
        return Error::InvalidState;
    }
    if (path == nullptr) {
        return Error::InvalidArg;
    }
    if (unlink(path) != 0) {
        return Error::NotFound;
    }
    return Status::success();
}

Status StorageService::makeDir(const char* path) {
    if (!mounted_) {
        return Error::InvalidState;
    }
    if (path == nullptr) {
        return Error::InvalidArg;
    }
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        Log::error(kTag, "mkdir failed: %s (errno %d)", path, errno);
        return Error::Internal;
    }
    return Status::success();
}

Result<size_t> StorageService::listDir(const char* path, DirEntry* out,
                                       size_t max) const {
    if (!mounted_) {
        return Error::InvalidState;
    }
    if (path == nullptr || out == nullptr || max == 0) {
        return Error::InvalidArg;
    }

    DIR* dir = opendir(path);
    if (dir == nullptr) {
        return Error::NotFound;
    }

    size_t count = 0;
    while (count < max) {
        const dirent* entry = readdir(dir);
        if (entry == nullptr) {
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        const size_t name_len =
            strnlen(entry->d_name, sizeof(DirEntry::name) - 1);
        if (name_len >= sizeof(DirEntry::name) - 1) {
            continue;
        }

        char full_path[160];
        const int path_len = snprintf(full_path, sizeof(full_path), "%s/%.*s",
                                      path, static_cast<int>(name_len),
                                      entry->d_name);
        if (path_len <= 0 ||
            static_cast<size_t>(path_len) >= sizeof(full_path)) {
            continue;
        }

        struct stat st = {};
        if (stat(full_path, &st) != 0) {
            continue;
        }

        DirEntry& row = out[count++];
        memcpy(row.name, entry->d_name, name_len);
        row.name[name_len] = '\0';
        row.is_dir = S_ISDIR(st.st_mode);
        row.size = static_cast<size_t>(st.st_size);
    }
    closedir(dir);
    return count;
}

}  // namespace gallus::services
