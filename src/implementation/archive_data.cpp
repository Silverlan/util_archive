// SPDX-FileCopyrightText: (c) 2020 Silverlan <opensource@pragma-engine.com>
// SPDX-License-Identifier: MIT

module;

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
