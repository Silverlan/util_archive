/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

module;

#include <unordered_map>
#include <vector>
#include <string>

export module pragma.gamemount:vdf;

export namespace pragma::gamemount::vdf {
	class DataBlock {
	  public:
		DataBlock() {}
		std::unordered_map<std::string, DataBlock> children;
		std::unordered_map<std::string, std::string> keyValues;
	};

	class Data {
	  public:
		Data() {}
		DataBlock dataBlock = {};
	};

	bool get_external_steam_locations(const std::string &steamRootPath, std::vector<std::string> &outExtLocations);
};
