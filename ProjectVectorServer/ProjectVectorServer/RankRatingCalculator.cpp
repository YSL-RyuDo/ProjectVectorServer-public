#include "RankRatingCalculator.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double ELO_SCALE = 400.0;
    constexpr int VISIBLE_RATING_K = 40;
    constexpr int MIN_VISIBLE_CHANGE = 8;
    constexpr int MAX_VISIBLE_CHANGE = 35;
    constexpr int MIN_MMR_K = 28;
    constexpr int MAX_MMR_K = 52;
    constexpr int RATING_DEVIATION_STEP = 27;
    constexpr int MAX_SCORE_MARGIN_BONUS = 3;
    constexpr int MAX_CONVERGENCE_ADJUSTMENT = 6;
    constexpr int MIN_INTERNAL_MMR = -10000;
    constexpr int MAX_INTERNAL_MMR = 100000;

    double CalculateExpectedWinRateInternal(
        double ownTeamAverageMmr,
        double opponentTeamAverageMmr)
    {
        double difference = std::clamp(
            opponentTeamAverageMmr - ownTeamAverageMmr,
            -1600.0,
            1600.0);
        return 1.0 / (1.0 + std::pow(10.0, difference / ELO_SCALE));
    }

    int CalculateMmrK(int ratingDeviation)
    {
        int normalizedDeviation = std::clamp(
            ratingDeviation,
            RankRatingCalculator::MIN_RATING_DEVIATION,
            RankRatingCalculator::DEFAULT_RATING_DEVIATION);
        double uncertainty = static_cast<double>(
            normalizedDeviation - RankRatingCalculator::MIN_RATING_DEVIATION) /
            static_cast<double>(
                RankRatingCalculator::DEFAULT_RATING_DEVIATION -
                RankRatingCalculator::MIN_RATING_DEVIATION);
        return static_cast<int>(std::lround(
            MIN_MMR_K + (MAX_MMR_K - MIN_MMR_K) * uncertainty));
    }

    int CalculateScoreMarginBonus(int scoreFor, int scoreAgainst)
    {
        int totalScore = std::max(0, scoreFor) + std::max(0, scoreAgainst);
        if (totalScore <= 0)
            return 0;

        double marginRatio = static_cast<double>(
            std::abs(scoreFor - scoreAgainst)) /
            static_cast<double>(totalScore);
        return std::clamp(
            static_cast<int>(std::lround(
                MAX_SCORE_MARGIN_BONUS * marginRatio)),
            0,
            MAX_SCORE_MARGIN_BONUS);
    }
}

RankRatingResult RankRatingCalculator::Calculate(
    const RankRatingInput& input)
{
    RankRatingResult result{};
    int currentRankPoints = std::max(0, input.rankPoints);
    int currentRatingDeviation = std::clamp(
        input.ratingDeviation,
        MIN_RATING_DEVIATION,
        DEFAULT_RATING_DEVIATION);
    double expectedWinRate = RankRatingCalculator::CalculateExpectedWinRate(
        input.ownTeamAverageMmr,
        input.opponentTeamAverageMmr);
    double actualResult = input.won ? 1.0 : 0.0;

    result.expectedWinRatePermille = static_cast<uint16_t>(std::clamp(
        static_cast<int>(std::lround(expectedWinRate * 1000.0)),
        0,
        1000));

    int mmrK = CalculateMmrK(currentRatingDeviation);
    result.matchmakingRatingDelta = static_cast<int>(std::lround(
        mmrK * (actualResult - expectedWinRate)));
    result.matchmakingRatingAfter = std::clamp(
        input.matchmakingRating + result.matchmakingRatingDelta,
        MIN_INTERNAL_MMR,
        MAX_INTERNAL_MMR);

    int visibleDelta = static_cast<int>(std::lround(
        VISIBLE_RATING_K * (actualResult - expectedWinRate)));
    int scoreMarginBonus = CalculateScoreMarginBonus(
        input.scoreFor,
        input.scoreAgainst);
    visibleDelta += input.won ? scoreMarginBonus : -scoreMarginBonus;

    int convergenceAdjustment = std::clamp(
        static_cast<int>(std::lround(
            static_cast<double>(
                input.matchmakingRating - currentRankPoints) /
            100.0)),
        -MAX_CONVERGENCE_ADJUSTMENT,
        MAX_CONVERGENCE_ADJUSTMENT);
    visibleDelta += convergenceAdjustment;

    if (input.updateVisibleRank)
    {
        visibleDelta = input.won
            ? std::clamp(
                visibleDelta,
                MIN_VISIBLE_CHANGE,
                MAX_VISIBLE_CHANGE)
            : std::clamp(
                visibleDelta,
                -MAX_VISIBLE_CHANGE,
                -MIN_VISIBLE_CHANGE);
    }
    else
    {
        visibleDelta = 0;
    }

    result.rankPointsAfter = input.updateVisibleRank
        ? std::max(0, currentRankPoints + visibleDelta)
        : currentRankPoints;
    result.rankPointDelta = result.rankPointsAfter - currentRankPoints;
    result.ratingDeviationAfter = std::max(
        MIN_RATING_DEVIATION,
        currentRatingDeviation - RATING_DEVIATION_STEP);

    if (expectedWinRate <= 0.43)
        result.modifierFlags |= RankModifierFlags::UNDERDOG;
    else if (expectedWinRate >= 0.57)
        result.modifierFlags |= RankModifierFlags::FAVORITE;

    if (input.rankedMatches < PLACEMENT_MATCH_COUNT ||
        currentRatingDeviation > 150)
    {
        result.modifierFlags |= RankModifierFlags::CALIBRATING;
    }

    if (input.updateVisibleRank)
    {
        if (convergenceAdjustment >= 2)
            result.modifierFlags |= RankModifierFlags::CONVERGING_UP;
        else if (convergenceAdjustment <= -2)
            result.modifierFlags |= RankModifierFlags::CONVERGING_DOWN;
    }

    if (scoreMarginBonus >= 2)
        result.modifierFlags |= RankModifierFlags::DECISIVE;

    if (input.updateVisibleRank &&
        !input.won &&
        result.rankPointDelta == 0)
        result.modifierFlags |= RankModifierFlags::FLOOR_PROTECTION;

    if (input.partyDisadvantage)
        result.modifierFlags |= RankModifierFlags::PARTY_DISADVANTAGE;

    return result;
}

double RankRatingCalculator::CalculateExpectedWinRate(
    double ownTeamAverageMmr,
    double opponentTeamAverageMmr)
{
    return CalculateExpectedWinRateInternal(
        ownTeamAverageMmr,
        opponentTeamAverageMmr);
}

int RankRatingCalculator::EstimateLegacyRatingDeviation(int rankedMatches)
{
    int completedMatches = std::clamp(
        rankedMatches,
        0,
        PLACEMENT_MATCH_COUNT);
    return std::max(
        MIN_RATING_DEVIATION,
        DEFAULT_RATING_DEVIATION -
            completedMatches * RATING_DEVIATION_STEP);
}
