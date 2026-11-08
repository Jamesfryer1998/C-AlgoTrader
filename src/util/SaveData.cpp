#include "SaveData.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
#include "Format.hpp"

namespace fs = std::filesystem;

namespace
{
    const std::string PROTECTED_FOLDER = "live";   // data/live: the live account's records
    // Gigabytes as macOS (Finder) counts them: 1 GB = 1,000,000,000 bytes of file contents
    const double BYTES_PER_GIGABYTE = 1e9;
    const double BYTES_PER_MEGABYTE = 1e6;

    std::unique_ptr<SaveData> programData;   // set by configure()
    std::mutex configureLock;

    struct SavedFile
    {
        fs::path path;
        std::uintmax_t bytes;
        fs::file_time_type written;
    };

    std::string normal(const fs::path& path)
    {
        std::error_code error;
        fs::path absolute = fs::absolute(path, error);
        return (error ? path : absolute).lexically_normal().string();
    }

    // "4.97 GB", or "512.3 MB" under a gigabyte
    std::string describeSize(std::uintmax_t bytes)
    {
        if (bytes >= BYTES_PER_GIGABYTE)
            return Format::number(bytes / BYTES_PER_GIGABYTE, 2) + " GB";
        return Format::number(bytes / BYTES_PER_MEGABYTE, 1) + " MB";
    }
}

void
SaveData::configure(const std::string& dataRoot, double limitGigabytes)
{
    std::lock_guard<std::mutex> guard(configureLock);
    programData = std::make_unique<SaveData>(dataRoot, static_cast<std::uintmax_t>(limitGigabytes * BYTES_PER_GIGABYTE));
    programData->enforceLimit();
}

void
SaveData::saved(const std::string& path)
{
    std::lock_guard<std::mutex> guard(configureLock);
    if (programData)
        programData->recordSave(path);
}

bool
SaveData::write(const std::string& path, const std::string& contents)
{
    {
        std::ofstream file(path, std::ios::trunc);
        if (!file)
            return false;
        file << contents;
        if (!file)
            return false;
    }
    saved(path);
    return true;
}

SaveData::SaveData(const std::string& dataRoot, std::uintmax_t limitBytes)
    : dataRoot(normal(dataRoot)),
      protectedFolder(normal(fs::path(dataRoot) / PROTECTED_FOLDER)),
      limitBytes(limitBytes)
{
    estimatedBytes = bytesUsed();
}

std::uintmax_t
SaveData::bytesUsed() const
{
    std::uintmax_t total = 0;
    std::error_code error;
    for (fs::recursive_directory_iterator it(dataRoot, fs::directory_options::skip_permission_denied, error), end;
         !error && it != end; it.increment(error))
        if (it->is_regular_file(error))
            total += it->file_size(error);
    return total;
}

bool
SaveData::isProtected(const std::string& path) const
{
    std::string file = normal(path);
    return file == protectedFolder || file.rfind(protectedFolder + "/", 0) == 0;
}

void
SaveData::recordSave(const std::string& path)
{
    std::error_code error;
    std::uintmax_t bytes = fs::file_size(path, error);
    {
        // A file saved before (e.g. performance.json, rewritten often) only adds what it grew by
        std::lock_guard<std::mutex> guard(lock);
        auto known = savedSizes.find(path);
        std::uintmax_t before = known == savedSizes.end() ? 0 : known->second;
        estimatedBytes = estimatedBytes + bytes > before ? estimatedBytes + bytes - before : 0;
        savedSizes[path] = bytes;
        if (estimatedBytes <= limitBytes)
            return;
    }

    // Over by our count: measure properly, and trim if really over
    std::uintmax_t actual = bytesUsed();
    {
        std::lock_guard<std::mutex> guard(lock);
        estimatedBytes = actual;
    }
    if (actual > limitBytes)
        enforceLimit(path);
}

std::uintmax_t
SaveData::enforceLimit(const std::string& keep)
{
    std::vector<SavedFile> files;
    std::uintmax_t total = 0;
    std::string kept = keep.empty() ? "" : normal(keep);
    std::error_code error;
    for (fs::recursive_directory_iterator it(dataRoot, fs::directory_options::skip_permission_denied, error), end;
         !error && it != end; it.increment(error))
    {
        if (!it->is_regular_file(error))
            continue;
        std::uintmax_t bytes = it->file_size(error);
        total += bytes;
        std::string path = it->path().lexically_normal().string();
        if (!isProtected(path) && path != kept)
            files.push_back({it->path(), bytes, it->last_write_time(error)});
    }
    if (total <= limitBytes)
        return 0;

    // Oldest first
    std::sort(files.begin(), files.end(), [](const SavedFile& a, const SavedFile& b) { return a.written < b.written; });
    std::uintmax_t freed = 0;
    size_t deleted = 0;
    for (const auto& file : files)
    {
        if (total - freed <= limitBytes)
            break;
        if (fs::remove(file.path, error))
        {
            freed += file.bytes;
            deleted++;
        }
    }

    std::cout << "Data folder was " << describeSize(total) << ", over its " << describeSize(limitBytes)
              << " limit: deleted the " << deleted << " oldest file(s) (" << describeSize(freed) << ")" << std::endl;
    std::lock_guard<std::mutex> guard(lock);
    estimatedBytes = total - freed;
    return freed;
}
