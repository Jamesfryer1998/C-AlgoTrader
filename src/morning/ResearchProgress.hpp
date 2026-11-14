#pragma once

#include <chrono>
#include <ctime>
#include <string>

/**
 * ResearchProgress
 *
 * How far a research run has got, in <folder>/progress.json, for the dashboard's progress bar:
 *
 *   {"state": "running", "stage": "Picking and trading each day", "done": 120, "total": 790,
 *    "started": 1794600000, "updated": 1794600360}
 *
 * Written at most every couple of seconds, unless forced (a new stage, the end).
 */
class ResearchProgress
{
    public:
        static inline const std::string RUNNING = "running";
        static inline const std::string FINISHED = "finished";
        static inline const std::string FAILED = "failed";

        explicit ResearchProgress(const std::string& folder);

        void save(const std::string& state, const std::string& stage, size_t done, size_t total, bool force = false);

        // A stage's progress, saved (and printed) when the stage changes or it's been a while
        void stage(const std::string& stage, size_t done, size_t total);

        // The day before today in New York: where a run ends when its config says "end": ""
        static std::string yesterday();

    private:
        std::string path;
        std::time_t started;
        std::string lastStage;
        std::chrono::steady_clock::time_point lastSaved;
};
