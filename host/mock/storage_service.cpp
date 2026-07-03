#include "gallus/services/storage_service.hpp"

#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "gallus/log.hpp"

namespace gallus::services {

namespace {

constexpr const char* kTag = "Storage";

std::mutex g_mutex;
std::map<std::string, std::vector<uint8_t>> g_files;
std::map<std::string, bool> g_dirs;

bool isDir(const std::string& path) {
    const auto it = g_dirs.find(path);
    return it != g_dirs.end() && it->second;
}

std::string parentPath(const std::string& path) {
    const auto pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return {};
    }
    if (pos == 0) {
        return "/";
    }
    return path.substr(0, pos);
}

void ensureParentDirs(const std::string& path) {
    if (path.empty() || path == "/") {
        g_dirs["/"] = true;
        return;
    }
    std::string current;
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] != '/') {
            continue;
        }
        current = path.substr(0, i == 0 ? 1 : i);
        if (!current.empty()) {
            g_dirs[current] = true;
        }
    }
}

}  // namespace

Status StorageService::init() {
    if (mounted_) {
        return Error::InvalidState;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_dirs[kBasePath] = true;
    g_dirs[std::string(kBasePath) + "/config"] = true;
    mounted_ = true;
    Log::info(kTag, "in-memory mount at %s (host)", kBasePath);
    return Status::success();
}

Result<size_t> StorageService::totalBytes() const {
    if (!mounted_) {
        return Error::InvalidState;
    }
    return static_cast<size_t>(1024 * 1024);
}

Result<size_t> StorageService::usedBytes() const {
    if (!mounted_) {
        return Error::InvalidState;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    size_t used = 0;
    for (const auto& [path, data] : g_files) {
        used += data.size();
        (void)path;
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

    std::lock_guard<std::mutex> lock(g_mutex);
    ensureParentDirs(parentPath(path));
    std::vector<uint8_t> bytes(len);
    if (len > 0) {
        std::memcpy(bytes.data(), data, len);
    }
    g_files[path] = std::move(bytes);
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

    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_files.find(path);
    if (it == g_files.end()) {
        return Error::NotFound;
    }
    const size_t n = it->second.size() < cap ? it->second.size() : cap;
    if (n > 0) {
        std::memcpy(buf, it->second.data(), n);
    }
    return n;
}

bool StorageService::exists(const char* path) const {
    if (!mounted_ || path == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_files.find(path) != g_files.end() || isDir(path);
}

Status StorageService::removeFile(const char* path) {
    if (!mounted_ || path == nullptr) {
        return Error::InvalidState;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_files.erase(path) > 0 ? Status::success() : Error::NotFound;
}

Status StorageService::makeDir(const char* path) {
    if (!mounted_ || path == nullptr) {
        return Error::InvalidState;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_dirs[path] = true;
    return Status::success();
}

Result<size_t> StorageService::listDir(const char* path, DirEntry* out,
                                      size_t max) const {
    if (!mounted_ || path == nullptr || out == nullptr) {
        return Error::InvalidArg;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    const std::string prefix = std::string(path);
    const std::string dir_prefix =
        prefix.empty() ? std::string() : (prefix.back() == '/' ? prefix
                                                                : prefix + "/");

    size_t count = 0;
    auto consider = [&](const std::string& candidate, bool is_dir,
                        size_t size) {
        if (count >= max) {
            return;
        }
        if (candidate == dir_prefix || candidate == prefix) {
            return;
        }
        if (candidate.rfind(dir_prefix, 0) != 0) {
            return;
        }
        const std::string rest = candidate.substr(dir_prefix.size());
        if (rest.find('/') != std::string::npos) {
            return;
        }
        DirEntry& entry = out[count++];
        snprintf(entry.name, sizeof(entry.name), "%s", rest.c_str());
        entry.is_dir = is_dir;
        entry.size = size;
    };

    for (const auto& [dir_path, dir_flag] : g_dirs) {
        if (dir_flag) {
            consider(dir_path, true, 0);
        }
    }
    for (const auto& [file_path, data] : g_files) {
        consider(file_path, false, data.size());
    }
    return count;
}

}  // namespace gallus::services
