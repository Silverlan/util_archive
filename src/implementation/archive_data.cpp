/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

module;

#include <sharedutils/util_string.h>
#include <sharedutils/util_path.hpp>
#include <algorithm>
#include <memory>

module pragma.gamemount;

import :archivedata;

pragma::gamemount::ArchiveFileTable::Item::Item(const std::string &pname, bool pbDir) : name(pname), directory(pbDir) {}
void pragma::gamemount::ArchiveFileTable::Item::Add(const std::string &fpath, bool bDir)
{
	util::Path path {fpath};
	auto pathList = path.ToComponents();
	if(pathList.empty() == true)
		return;
	Add(pathList.data(), pathList.size() - 1, bDir);
}
void pragma::gamemount::ArchiveFileTable::Item::Add(const std::string *path, uint32_t dirCount, bool bDir)
{
	auto it = std::find_if(children.begin(), children.end(), [path](const Item &item) { return (item.name == *path) ? true : false; });
	if(it == children.end()) {
		children.push_back({*path, (dirCount > 0) ? true : bDir});
		it = children.end() - 1;
	}
	if(dirCount == 0)
		return;
	it->Add(path + 1, dirCount - 1, bDir);
}
pragma::gamemount::ArchiveFileTable::ArchiveFileTable(const std::shared_ptr<void> &phandle) : handle(phandle) {}
