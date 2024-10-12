/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

module;

#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <fsys/filesystem.h>
#include <sharedutils/util_log.hpp>
#include "definitions.hpp"

export module pragma.gamemount;

export import :info;
export import :archive;
export import :vdf;

export namespace pragma::gamemount {
	DLLARCHLIB VFilePtr load(const std::string &path, std::optional<std::string> *optOutSourcePath = nullptr, const std::optional<std::string> &game = {});
	DLLARCHLIB bool load(const std::string &path, std::vector<uint8_t> &data);
	DLLARCHLIB bool find_files(const std::string &path, std::vector<std::string> *files, std::vector<std::string> *dirs, bool keepAbsPaths = false, const std::optional<std::string> &game = {});
	DLLARCHLIB bool get_mounted_game_paths(const std::string &game, std::vector<std::string> &outPaths);
	DLLARCHLIB std::optional<int32_t> get_mounted_game_priority(const std::string &game);
	DLLARCHLIB void set_mounted_game_priority(const std::string &game, int32_t priority);
	DLLARCHLIB void set_log_handler(const util::LogHandler &loghandler);
	DLLARCHLIB void set_log_severity(util::LogSeverity severity);
	DLLARCHLIB void close();

	struct GameMountInfo;
	DLLARCHLIB bool mount_game(const GameMountInfo &mountInfo);
	DLLARCHLIB const std::vector<GameMountInfo> &get_game_mount_infos();
	DLLARCHLIB void initialize();
};
