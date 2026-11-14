#include "ResearchProgress.hpp"

#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../util/MarketTime.hpp"
#include "../util/SaveData.hpp"

namespace fs = std::filesystem;

namespace
{
    const std::string PROGRESS_FILE = "progress.json";
    const std::chrono::seconds SAVE_EVERY{2};   // a fast stage shouldn't spend its time writing this
}

ResearchProgress::ResearchProgress(const std::string& folder)
    : path((fs::path(folder) / PROGRESS_FILE).string()), started(std::time(nullptr))
{
    fs::create_directories(folder);
}

void
ResearchProgress::save(const std::string& state, const std::string& stageName, size_t done, size_t total, bool force)
{
    auto now = std::chrono::steady_clock::now();
    if (!force && now - lastSaved < SAVE_EVERY)
        return;
    lastSaved = now;
    nlohmann::json progress = {{"state", state}, {"stage", stageName}, {"done", done}, {"total", total},
                               {"started", started}, {"updated", std::time(nullptr)}};
    SaveData::write(path, progress.dump());
}

void
ResearchProgress::stage(const std::string& stageName, size_t done, size_t total)
{
    bool newStage = stageName != lastStage;
    if (newStage)
        std::cout << stageName << "..." << std::endl;
    lastStage = stageName;
    save(RUNNING, stageName, done, total, newStage);
}

std::string
ResearchProgress::yesterday()
{
    std::string today = MarketTime::datePart(MarketTime::toNewYorkTime(std::time(nullptr)));
    return MarketTime::datePart(MarketTime::formatUtc(MarketTime::parseDate(today) - MarketTime::SECONDS_PER_DAY));
}
