#include "CSVParser.hpp"

#include <fstream>


CSVParser::CSVParser()
{
}

CSVParser::~CSVParser()
{
}

void
CSVParser::Read(const std::string& filePath)
{
    std::ifstream csvFile{filePath};
    std::string line;

    if (!std::filesystem::exists(filePath)) 
    {
        throw std::runtime_error("File not found: " + filePath);
        return;
    }

    if (csvFile.is_open())
    {        
        // Skip first line
        std::getline(csvFile, line);

        while (std::getline(csvFile, line))
        {
            MarketCondition recentLine = ParseToMarketCondition(line);
            data.push_back(recentLine);
        }

        std::cout << data.size() << " lines read from file" << std::endl;
    }

    csvFile.close();
}

MarketCondition
CSVParser::ParseToMarketCondition(std::string line)
{
    // We are using comma separated values in CSV
    std::vector<std::string> tokens = tokenise(line, ',');

    if (tokens.size() != 6) {
        throw std::runtime_error("Not enough CSV Tokens");
    }

    return MarketCondition(
        tokens[0],              // Datetime
        tokens[1],              // Ticker
        std::stof(tokens[2]),   // Open
        std::stof(tokens[3]),   // Close
        std::stoi(tokens[4]),   // Volume
        tokens[5]               // TimeInterval
    );
}

std::vector<std::string> CSVParser::tokenise(std::string csvLine, char separator)
{
    std::vector<std::string> tokens;
    std::string::size_type start, end;
    std::string token;
    start = csvLine.find_first_not_of(separator, 0);

    do{
        end = csvLine.find_first_of(separator, start);
        if (start == csvLine.length() || start == end) break;
        if (end >= 0 ) token = csvLine.substr(start, end - start);
        else token = csvLine.substr(start, csvLine.length() - start);
        tokens.push_back(token);
        start = end + 1;
    }while(end != std::string::npos);

    return tokens;
}
