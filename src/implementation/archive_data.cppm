/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

module;

#include <string>
#include <vector>
#include <memory>

export module pragma.gamemount:archivedata;

export namespace pragma::gamemount {
	struct ArchiveFileTable {
		struct Item {
			Item(const std::string &name, bool bDir);
			std::vector<Item> children;
			std::string name;
			void Add(const std::string &fpath, bool bDir);
			bool directory = false;
		  private:
			void Add(const std::string *path, uint32_t dirCount, bool bDir);
		};
		ArchiveFileTable(const std::shared_ptr<void> &phandle);
		std::string identifier;
		std::shared_ptr<void> handle = nullptr;
		Item root = {"", true};
	};
};
