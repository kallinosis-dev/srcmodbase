#if !defined CS_PLAYER_RANK_SHARED_H
#define CS_PLAYER_RANK_SHARED_H

enum MedalCategory_t
{
	MEDAL_CATEGORY_NONE = -1, 
	MEDAL_CATEGORY_START = 0,
	MEDAL_CATEGORY_TEAM_AND_OBJECTIVE = MEDAL_CATEGORY_START,
	MEDAL_CATEGORY_COMBAT,
	MEDAL_CATEGORY_WEAPON,
	MEDAL_CATEGORY_MAP,
	MEDAL_CATEGORY_ARSENAL,
	MEDAL_CATEGORY_ACHIEVEMENTS_END,
	MEDAL_CATEGORY_SEASON_COIN = MEDAL_CATEGORY_ACHIEVEMENTS_END,
	MEDAL_CATEGORY_COUNT,
};

#define MEDAL_SEASON_ACCESS_OPERATION_NAME "op06"

enum MedalSeasonCoinItemIds_t
{
	MEDAL_SEASON_ACCESS_ENABLED = 0,
	MEDAL_SEASON_ACCESS_FREETOPLAY = 1,		// 0: must own to play or must be sponsored by friend; 1: free to play with an upsell; 2: absolutely free without any notice
	MEDAL_SEASON_ACCESS_VALUE = 6,
	MEDAL_SEASON_COIN_BRONZE = 1336,
	MEDAL_SEASON_COIN_SILVER = 1337,
	MEDAL_SEASON_COIN_GOLD = 1338,
};

enum MedalRank_t
{
	MEDAL_RANK_NONE,
	MEDAL_RANK_BRONZE,
	MEDAL_RANK_SILVER,
	MEDAL_RANK_GOLD,
	MEDAL_RANK_COUNT
};

#endif