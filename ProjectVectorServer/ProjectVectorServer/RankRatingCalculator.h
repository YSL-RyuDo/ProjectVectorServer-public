#pragma once

#include <cstdint>

namespace RankModifierFlags
{
    constexpr uint8_t UNDERDOG = 1 << 0;
    constexpr uint8_t FAVORITE = 1 << 1;
    constexpr uint8_t CALIBRATING = 1 << 2;
    constexpr uint8_t CONVERGING_UP = 1 << 3;
    constexpr uint8_t CONVERGING_DOWN = 1 << 4;
    constexpr uint8_t DECISIVE = 1 << 5;
    constexpr uint8_t FLOOR_PROTECTION = 1 << 6;
    constexpr uint8_t PARTY_DISADVANTAGE = 1 << 7;
}

struct RankRatingInput
{
    int rankPoints = 0;
    int matchmakingRating = 0;
    int ratingDeviation = 350;
    int rankedMatches = 0;
    double ownTeamAverageMmr = 0.0;
    double opponentTeamAverageMmr = 0.0;
    bool won = false;
    bool updateVisibleRank = true;
    bool partyDisadvantage = false;
    int scoreFor = 0;
    int scoreAgainst = 0;
};

struct RankRatingResult
{
    int rankPointsAfter = 0;
    int matchmakingRatingAfter = 0;
    int ratingDeviationAfter = 350;
    int rankPointDelta = 0;
    int matchmakingRatingDelta = 0;
    uint16_t expectedWinRatePermille = 500;
    uint8_t modifierFlags = 0;
};

class RankRatingCalculator
{
public:
    static constexpr int DEFAULT_RATING_DEVIATION = 350;
    static constexpr int MIN_RATING_DEVIATION = 80;
    static constexpr int PLACEMENT_MATCH_COUNT = 10;

    static RankRatingResult Calculate(const RankRatingInput& input);
    static double CalculateExpectedWinRate(
        double ownTeamAverageMmr,
        double opponentTeamAverageMmr);
    static int EstimateLegacyRatingDeviation(int rankedMatches);
};
