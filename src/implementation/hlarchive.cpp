/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

module;

#include <HLLib.h>
#include <Wrapper.h>
#include <memory>
#include <array>
#include <string>

module pragma.gamemount;

import :archive;

pragma::gamemount::hl::Archive::Stream::Stream(Archive &archive, HLDirectoryItem *item, HLStream *stream) : m_archive(archive.shared_from_this()), m_item(item), m_stream(stream) {}
pragma::gamemount::hl::Archive::Stream::~Stream()
{
	hlStreamClose(m_stream);
	hlFileReleaseStream(m_item, m_stream);
}
uint32_t pragma::gamemount::hl::Archive::Stream::GetSize() const { return hlStreamGetStreamSize(m_stream); }
bool pragma::gamemount::hl::Archive::Stream::Read(std::vector<uint8_t> &data) const
{
	if(m_archive->Bind() == false)
		return false;
	auto size = GetSize();
	data.resize(size);
	for(auto i = decltype(size) {0}; i < size; ++i) {
		if(hlStreamReadChar(m_stream, reinterpret_cast<char *>(&data.at(i))) == hlFalse)
			break;
	}
	return true;
}

//////////////////

pragma::gamemount::hl::Archive::Directory::Directory(HLDirectoryItem *item, const std::string &path) : m_item(item), m_path(path) {}
void pragma::gamemount::hl::Archive::Directory::GetItems(std::vector<std::string> *files, std::vector<Directory> *dirs) const
{
	auto numItems = hlFolderGetCount(m_item);
	if(files != nullptr)
		files->reserve(numItems);
	if(dirs != nullptr)
		dirs->reserve(numItems);
	for(auto i = decltype(numItems) {0}; i < numItems; ++i) {
		auto *item = hlFolderGetItem(m_item, i);
		auto type = hlItemGetType(item);
		auto *name = hlItemGetName(item);
		if(type == HLDirectoryItemType::HL_ITEM_FILE) {
			if(files != nullptr)
				files->push_back(name);
		}
		else if(type == HLDirectoryItemType::HL_ITEM_FOLDER && dirs != nullptr)
			dirs->push_back(Directory(item, name));
	}
}
const std::string &pragma::gamemount::hl::Archive::Directory::GetPath() const { return m_path; }
void pragma::gamemount::hl::Archive::Directory::GetItems(std::vector<std::string> &files, std::vector<Directory> &dirs) const { GetItems(&files, &dirs); }
void pragma::gamemount::hl::Archive::Directory::GetFiles(std::vector<std::string> &files) const { GetItems(&files, nullptr); }
void pragma::gamemount::hl::Archive::Directory::GetDirectories(std::vector<Directory> &dirs) const { GetItems(nullptr, &dirs); }

//////////////////

std::shared_ptr<pragma::gamemount::hl::Archive> pragma::gamemount::hl::Archive::Create(const std::string &path)
{
	auto type = hlGetPackageTypeFromName(path.c_str());
	if(type == HLPackageType::HL_PACKAGE_NONE)
		return nullptr;
	auto parchive = std::shared_ptr<Archive>(new Archive());
	if(hlCreatePackage(type, &parchive->m_uiPackage) == hlFalse || parchive->Bind() == false || hlPackageOpenFile(path.c_str(), HL_MODE_READ) == hlFalse)
		return nullptr;
	return parchive;
}

pragma::gamemount::hl::Archive::Archive() {}

bool pragma::gamemount::hl::Archive::Bind() { return static_cast<bool>(hlBindPackage(m_uiPackage)); }

pragma::gamemount::hl::Archive::Directory pragma::gamemount::hl::Archive::GetRoot() const
{
	auto *root = hlPackageGetRoot();
	return Directory(root);
}

void pragma::gamemount::hl::Archive::SetRootDirectory(const std::string &path)
{
	auto *root = hlPackageGetRoot();
	m_rootDir = hlFolderGetItemByPath(root, path.c_str(), HLFindType::HL_FIND_FOLDERS);
}

std::shared_ptr<pragma::gamemount::hl::Archive::Stream> pragma::gamemount::hl::Archive::OpenFile(const std::string &fname)
{
	if(Bind() == false)
		return nullptr;
	auto *root = m_rootDir ? m_rootDir : hlPackageGetRoot();
	auto *item = hlFolderGetItemByPath(root, fname.c_str(), HLFindType::HL_FIND_FILES);
	if(item == nullptr)
		return nullptr;
	HLStream *pStream = nullptr;
	if(hlFileCreateStream(item, &pStream) == hlFalse)
		return nullptr;
	auto stream = std::make_shared<Stream>(*this, item, pStream);
	if(hlStreamOpen(pStream, HL_MODE_READ) == hlFalse)
		return nullptr;
	return stream;
}

pragma::gamemount::hl::Archive::~Archive()
{
	if(m_uiPackage != std::numeric_limits<hlUInt>::max()) {
		if(Bind() == true)
			hlPackageClose();
		hlDeletePackage(m_uiPackage);
	}
}
