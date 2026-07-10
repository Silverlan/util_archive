// SPDX-FileCopyrightText: (c) 2020 Silverlan <opensource@pragma-engine.com>
// SPDX-License-Identifier: MIT

module;

export module pragma.gamemount:archivedata;

export import std.compat;

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
