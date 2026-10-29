#pragma once

#include <string>

/**
 * DotEnv
 *
 * Loads secrets (e.g. API keys) from a .env file in the project root into environment variables.
 * The .env file is git-ignored; copy .env.example to .env and fill it in.
 *
 * Format, one per line:
 *   # comment
 *   APCA_API_KEY_ID=your_key_id
 *   APCA_API_SECRET_KEY="your secret"
 *
 * Variables already set in your shell win, so you can still override a value with export.
 */
class DotEnv
{
    public:
        // Returns the number of variables set. A missing file is not an error (returns 0).
        static int load(const std::string& filePath);

        // Parse one line into key and value. Returns false for blank lines, comments and junk.
        static bool parseLine(const std::string& line, std::string& key, std::string& value);
};
