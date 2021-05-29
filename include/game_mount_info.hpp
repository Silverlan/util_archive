/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __GAME_MOUNT_INFO_HPP__
#define __GAME_MOUNT_INFO_HPP__

#include <cinttypes>
#include "util_archive.hpp"

namespace uarch
{
	enum class GameEngine : uint8_t
	{
		SourceEngine = 0,
		Source2,
		Gamebryo,
		CreationEngine,
		Other,

		Count,
		Invalid = std::numeric_limits<uint8_t>::max()
	};
	DLLARCHLIB GameEngine engine_name_to_enum(const std::string &name);
	DLLARCHLIB std::string to_string(GameEngine engine);
	struct DLLARCHLIB SteamSettings
	{
		using AppId = uint32_t;
		AppId appId = std::numeric_limits<AppId>::max();
		std::vector<std::string> gamePaths;

		bool mountWorkshop = false;
	};
	struct BaseEngineSettings
	{
		virtual ~BaseEngineSettings()=default;
	};
	struct DLLARCHLIB SourceEngineSettings
		: public BaseEngineSettings
	{
		struct VPKInfo
		{
			std::string rootDir;
		};
		std::unordered_map<std::string,VPKInfo> vpkList;
	};
	using Source2Settings = SourceEngineSettings;
	struct DLLARCHLIB GamebryoSettings
		: public BaseEngineSettings
	{
		struct BSAInfo
		{

		};
		std::unordered_map<std::string,BSAInfo> bsaList;
	};
	struct DLLARCHLIB CreationEngineSettings
		: public BaseEngineSettings
	{
		struct BA2Info
		{

		};
		std::unordered_map<std::string,BA2Info> ba2List;
	};
	struct DLLARCHLIB GameMountInfo
	{
		GameMountInfo(const GameMountInfo&)=default;
		GameMountInfo &operator=(const GameMountInfo&)=default;
		std::string identifier;
		bool enabled = true;
		std::optional<SteamSettings> steamSettings {};
		std::optional<std::string> absolutePath {};
		std::string localizationName;
		int32_t priority = 0;
		GameEngine gameEngine = GameEngine::Invalid;
		std::shared_ptr<BaseEngineSettings> engineSettings = nullptr;

		BaseEngineSettings *SetEngine(GameEngine engine);
	};
};

#endif
