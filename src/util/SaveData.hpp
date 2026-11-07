#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

/**
 * SaveData
 *
 * Keeps the data folder under a size limit (config/general.json "data_limit_gb", 5 GB unless set). Sizes are file
 * contents in decimal gigabytes (1 GB = 1,000,000,000 bytes), as Finder shows them; `du` reports a little more
 * because files take whole disk blocks. Everything that
 * saves or downloads a file into data/ tells it so:
 *
 *   SaveData::saved(path);
 *
 * When data/ goes over the limit, the oldest files (by when they were last written) are deleted until it's
 * back under. data/live/ is never touched: it holds the live account's trades and positions. Neither is the
 * file that was just saved.
 *
 * main() sets it up once with SaveData::configure(dataRoot, gigabytes); until then saved() does nothing
 * (e.g. in tests). Safe to call from several threads.
 */
class SaveData
{
    public:
        static constexpr double DEFAULT_LIMIT_GB = 5.0;

        // data/ and its limit for the whole program; checks the limit straight away
        static void configure(const std::string& dataRoot, double limitGigabytes = DEFAULT_LIMIT_GB);

        // A file was written under data/
        static void saved(const std::string& path);

        // Write a whole file (replacing it), then saved(path). False if it couldn't be written.
        static bool write(const std::string& path, const std::string& contents);

        // One data folder and its limit (what configure sets up; also usable on its own, e.g. in tests)
        SaveData(const std::string& dataRoot, std::uintmax_t limitBytes);

        // The size of every file under the folder
        std::uintmax_t bytesUsed() const;

        // If over the limit, delete the oldest files (never protected ones, nor `keep`). Returns bytes freed.
        std::uintmax_t enforceLimit(const std::string& keep = "");

        // Count a newly written file; checks the folder for real only when the running total passes the limit
        void recordSave(const std::string& path);

    private:
        bool isProtected(const std::string& path) const;

        std::string dataRoot;
        std::string protectedFolder;   // data/live
        std::uintmax_t limitBytes;
        std::uintmax_t estimatedBytes = 0;   // the last real size plus files saved since
        std::map<std::string, std::uintmax_t> savedSizes;   // each file saved this run, at its last size
        std::mutex lock;
};
