/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "util_archive.hpp"
#include "game_mount_info.hpp"
#include "util_vdf.hpp"
#include "hlarchive.hpp"
#include "archive_data.hpp"
#include <vector>
#include <sharedutils/util.h>
#include <sharedutils/util_string.h>
#include <sharedutils/util_file.h>
#include <sharedutils/util_path.hpp>
#include <algorithm>
#include <fsys/filesystem.h>
#include <Wrapper.h>
#include <HLLib.h>
#include <iostream>
#include <array>
#include <unordered_set>
#include <thread>
#include <atomic>

#ifdef __linux__
#include <cstdlib>
#endif

#ifdef ENABLE_BETHESDA_FORMATS
#include <libbsa/libbsa.h>
#include <bsa_asset.h>
#include <BA2.h>
#endif

#define UARCH_VERBOSE 0

#if UARCH_VERBOSE == 1
#include <iostream>
#endif

#pragma optimize("",off)

uarch::GameEngine uarch::engine_name_to_enum(const std::string &name)
{
	static std::unordered_map<std::string,uarch::GameEngine> engineNameToEnum {
		{"source_engine",GameEngine::SourceEngine},
		{"source2",GameEngine::Source2},
#ifdef ENABLE_BETHESDA_FORMATS
		{"gamebryo",GameEngine::Gamebryo},
		{"creation_engine",GameEngine::CreationEngine},
#endif
		{"other",GameEngine::Other}
	};
	static_assert(umath::to_integral(uarch::GameEngine::Count) == 5);
	auto it = engineNameToEnum.find(name);
	return (it != engineNameToEnum.end()) ? it->second : GameEngine::Invalid;
}

std::string uarch::to_string(GameEngine engine)
{
	switch(engine)
	{
	case GameEngine::SourceEngine:
		return "source_engine";
	case GameEngine::Source2:
		return "source2";
#ifdef ENABLE_BETHESDA_FORMATS
	case GameEngine::Gamebryo:
		return "gamebryo";
	case GameEngine::CreationEngine:
		return "creation_engine";
#endif
	case GameEngine::Other:
		return "other";
	}
	static_assert(umath::to_integral(uarch::GameEngine::Count) == 5);
	return "invalid";
}

uarch::BaseEngineSettings *uarch::GameMountInfo::SetEngine(GameEngine engine)
{
	switch(engine)
	{
	case GameEngine::SourceEngine:
		engineSettings = std::make_unique<SourceEngineSettings>();
		break;
	case GameEngine::Source2:
		engineSettings = std::make_unique<Source2Settings>();
		break;
#ifdef ENABLE_BETHESDA_FORMATS
	case GameEngine::Gamebryo:
		engineSettings = std::make_unique<GamebryoSettings>();
		break;
	case GameEngine::CreationEngine:
		engineSettings = std::make_unique<CreationEngineSettings>();
		break;
#endif
	}
	gameEngine = engine;
	return engineSettings.get();
}

namespace uarch
{
	void setup();
	void initialize(bool bWait);
	class BaseMountedGame
	{
	public:
		const std::vector<util::Path> &GetMountedPaths() const;
		const std::vector<ArchiveFileTable> &GetArchives() const;
		void FindFiles(const std::string &fpath,std::vector<std::string> *optOutFiles,std::vector<std::string> *optOutDirs,bool keepAbsPaths=false);
		bool Load(const std::string &path,std::vector<uint8_t> &data);
		VFilePtr Load(const std::string &path,std::optional<std::string> *optOutSourcePath=nullptr);

		void MountPath(const std::string &path);
		ArchiveFileTable &AddArchiveFileTable(const std::shared_ptr<void> &phandle);

		void SetGameMountInfoIndex(uint32_t gameMountInfoIdx) {m_gameMountInfoIdx = gameMountInfoIdx;}
		uint32_t GetGameMountInfoIndex() const {return m_gameMountInfoIdx;}
	protected:
		BaseMountedGame(GameEngine gameEngine);
	private:
		GameEngine m_gameEngine = GameEngine::Invalid;
		uint32_t m_gameMountInfoIdx = 0;
		std::vector<util::Path> m_mountedPaths {};
		std::vector<ArchiveFileTable> m_archives {};
	};

	class SourceEngineMountedGame
		: public BaseMountedGame
	{
	public:
		SourceEngineMountedGame(GameEngine gameEngine)
			: BaseMountedGame{gameEngine}
		{}
	};

	class Source2MountedGame
		: public SourceEngineMountedGame
	{
	public:
		Source2MountedGame(GameEngine gameEngine)
			: SourceEngineMountedGame{gameEngine}
		{}
	};

#ifdef ENABLE_BETHESDA_FORMATS
	class GamebryoMountedGame
		: public BaseMountedGame
	{
	public:
		GamebryoMountedGame(GameEngine gameEngine)
			: BaseMountedGame{gameEngine}
		{}
	};

	class CreationEngineMountedGame
		: public BaseMountedGame
	{
	public:
		CreationEngineMountedGame(GameEngine gameEngine)
			: BaseMountedGame{gameEngine}
		{}
	};
#endif

	class GameMountManager
	{
	public:
		GameMountManager()=default;
		GameMountManager(const GameMountManager&)=delete;
		GameMountManager &operator=(const GameMountManager&)=delete;
		~GameMountManager();
		bool MountGame(const GameMountInfo &mountInfo);
		void Start();
		void WaitUntilInitializationComplete();
		void SetVerbose(bool verbose);
		bool IsVerbose() const;

		void InitializeGame(const GameMountInfo &mountInfo,uint32_t gameMountInfoIdx);
		const std::vector<std::unique_ptr<BaseMountedGame>> &GetMountedGames() const;
		const std::vector<GameMountInfo> &GetGameMountInfos() const {return m_mountedGameInfos;}
		void UpdateGamePriorities();
		
		const GameMountInfo *FindGameMountInfo(const std::string &identifier) const
		{
			auto &gameMountInfos = GetGameMountInfos();
			auto it = std::find_if(gameMountInfos.begin(),gameMountInfos.end(),[&identifier](const GameMountInfo &mountInfo) {return ustring::compare(mountInfo.identifier,identifier,false);});
			if(it == gameMountInfos.end())
				return nullptr;
			return &*it;
		}

		const BaseMountedGame *FindMountedGameByIdentifier(const std::string &identifier) const {return const_cast<GameMountManager*>(this)->FindMountedGameByIdentifier(identifier);}
		BaseMountedGame *FindMountedGameByIdentifier(const std::string &identifier)
		{
			auto &gameMountInfos = GetGameMountInfos();
			auto it = std::find_if(gameMountInfos.begin(),gameMountInfos.end(),[&identifier](const GameMountInfo &mountInfo) {return ustring::compare(mountInfo.identifier,identifier,false);});
			if(it == gameMountInfos.end())
				return nullptr;
			auto idx = it -gameMountInfos.begin();
			auto itGame = std::find_if(m_mountedGames.begin(),m_mountedGames.end(),[idx](const std::unique_ptr<BaseMountedGame> &game) {
				return game->GetGameMountInfoIndex() == idx;
			});
			if(itGame == m_mountedGames.end())
				return nullptr;
			return itGame->get();
		}

		static std::string GetNormalizedPath(const std::string &path);
		static std::string GetNormalizedSourceEnginePath(const std::string &path);
#ifdef ENABLE_BETHESDA_FORMATS
		static std::string GetNormalizedGamebryoPath(const std::string &path);
#endif
	private:
		static void InitializeArchiveFileTable(uarch::ArchiveFileTable::Item &archiveDir,const hl::Archive::Directory &dir);

		std::vector<util::Path> FindSteamGamePaths(const std::string &relPath);
		void MountWorkshopAddons(BaseMountedGame &game,SteamSettings::AppId appId);

		std::vector<GameMountInfo> m_mountedGameInfos {};
		std::vector<std::unique_ptr<BaseMountedGame>> m_mountedGames {};

		std::thread m_loadThread;
		bool m_initialized = false;
		std::atomic<bool> m_cancel = false;
		std::atomic<bool> m_verbose = false;

		std::vector<util::Path> m_steamRootPaths;
		std::unordered_set<std::string> m_mountedVPKArchives {};
	};
};

uarch::BaseMountedGame::BaseMountedGame(GameEngine gameEngine)
	: m_gameEngine{gameEngine}
{}
void uarch::BaseMountedGame::MountPath(const std::string &path)
{
	if(m_mountedPaths.size() == m_mountedPaths.capacity())
		m_mountedPaths.reserve(m_mountedPaths.size() *1.5f +100);
	m_mountedPaths.push_back(path);
}
uarch::ArchiveFileTable &uarch::BaseMountedGame::AddArchiveFileTable(const std::shared_ptr<void> &phandle)
{
	if(m_archives.size() == m_archives.capacity())
		m_archives.reserve(m_archives.size() *1.5 +50);
	m_archives.push_back({phandle});
	return m_archives.back();
}

const std::vector<util::Path> &uarch::BaseMountedGame::GetMountedPaths() const {return m_mountedPaths;}
const std::vector<uarch::ArchiveFileTable> &uarch::BaseMountedGame::GetArchives() const {return m_archives;}

void uarch::BaseMountedGame::FindFiles(const std::string &fpath,std::vector<std::string> *optOutFiles,std::vector<std::string> *optOutDirs,bool keepAbsPaths)
{
	auto npath = fpath;
	switch(m_gameEngine)
	{
	case GameEngine::SourceEngine:
	case GameEngine::Source2:
		npath = GameMountManager::GetNormalizedSourceEnginePath(npath);
		break;
#ifdef ENABLE_BETHESDA_FORMATS
	case GameEngine::Gamebryo:
	case GameEngine::CreationEngine:
		npath = GameMountManager::GetNormalizedGamebryoPath(npath);
		break;
#endif
	}

	for(auto &path : GetMountedPaths())
	{
		auto foffset = optOutFiles ? optOutFiles->size() : 0;
		auto doffset = optOutDirs ? optOutDirs->size() : 0;
		auto searchPath = util::Path::CreatePath(FileManager::GetCanonicalizedPath(path.GetString() +ufile::get_path_from_filename(npath)));
		FileManager::FindSystemFiles((searchPath.GetString() +ufile::get_file_from_filename(npath)).c_str(),optOutFiles,optOutDirs);
		if(keepAbsPaths)
		{
			if(optOutFiles)
			{
				for(auto i=foffset;i<optOutFiles->size();++i)
					(*optOutFiles)[i] = (searchPath +util::Path::CreateFile((*optOutFiles)[i])).GetString();
			}
			if(optOutDirs)
			{
				for(auto i=doffset;i<optOutDirs->size();++i)
					(*optOutDirs)[i] = (searchPath +util::Path::CreateFile((*optOutDirs)[i])).GetString();
			}
		}
	}
	if(keepAbsPaths)
		return;
	npath = GameMountManager::GetNormalizedPath(npath);
	const auto fSearchArchive = 
		[optOutFiles,optOutDirs,&npath](const auto &data) {
		util::Path archPath {npath};
		auto pathList = archPath.ToComponents();
		auto itBegin = pathList.begin();
		auto itEnd = pathList.end();
		for(auto &arch : data)
		{
			auto *dir = &arch.root;
			for(auto it=itBegin;it!=itEnd;++it)
			{
				auto &d = *it;
				if(it == itEnd -1)
				{
					for(auto &child : dir->children)
					{
						if(ustring::match(child.name,d) == false)
							continue;
						if(child.directory == false)
						{
							if(optOutFiles != nullptr)
								optOutFiles->push_back(child.name);
						}
						else
						{
							if(optOutDirs != nullptr)
								optOutDirs->push_back(child.name);
						}
					}
				}
				else
				{
					auto itChild = std::find_if(dir->children.begin(),dir->children.end(),[&d](const decltype(*dir) &dirSub) {
						return (dirSub.directory == true && ustring::match(dirSub.name,d) == true) ? true : false;
					});
					if(itChild == dir->children.end())
						break;
					dir = &(*itChild);
				}
			}
		}
	};
	fSearchArchive(m_archives);
}
VFilePtr uarch::BaseMountedGame::Load(const std::string &fileName,std::optional<std::string> *optOutSourcePath)
{
	auto npath = fileName;
	switch(m_gameEngine)
	{
	case GameEngine::SourceEngine:
	case GameEngine::Source2:
		npath = GameMountManager::GetNormalizedSourceEnginePath(npath);
		break;
#ifdef ENABLE_BETHESDA_FORMATS
	case GameEngine::Gamebryo:
	case GameEngine::CreationEngine:
		npath = GameMountManager::GetNormalizedGamebryoPath(npath);
		break;
#endif
	}

	for(auto &path : GetMountedPaths())
	{
		auto filePath = path;
		filePath += npath;
		auto f = FileManager::OpenSystemFile(filePath.GetString().c_str(),"rb");
		if(f)
		{
			if(optOutSourcePath)
				*optOutSourcePath = filePath.GetString();
			return f;
		}
	}
	auto data = std::make_shared<std::vector<uint8_t>>();
	if(Load(fileName,*data) == false)
		return nullptr;
	if(optOutSourcePath)
		*optOutSourcePath = npath;
	FileManager::AddVirtualFile(npath,data);
	return FileManager::OpenFile(npath.c_str(),"rb");
}
bool uarch::BaseMountedGame::Load(const std::string &fileName,std::vector<uint8_t> &data)
{
	initialize(true);

	switch(m_gameEngine)
	{
	case GameEngine::SourceEngine:
	case GameEngine::Source2:
	{
		auto srcPath = GameMountManager::GetNormalizedSourceEnginePath(fileName);
		for(auto &archive : m_archives)
		{
			auto pArchive = std::static_pointer_cast<hl::Archive>(archive.handle);
			auto stream = pArchive->OpenFile(srcPath);
			if(stream == nullptr)
				continue;
			if(stream->Read(data) == true)
				return true;
		}
		break;
	}
#ifdef ENABLE_BETHESDA_FORMATS
	case GameEngine::Gamebryo:
	{
		auto gamebyroPath = GameMountManager::GetNormalizedGamebryoPath(fileName);
		for(auto &archive : m_archives)
		{
			auto bsaHandle = std::static_pointer_cast<bsa_handle>(archive.handle);
			bool result;
			auto r = bsa_contains_asset(*bsaHandle,gamebyroPath.c_str(),&result);
			if(r != LIBBSA_OK || result == false)
				continue;
			const uint8_t *pdata = nullptr;
			std::size_t size = 0;
			r = bsa_extract_asset_to_memory(*bsaHandle,gamebyroPath.c_str(),&pdata,&size);
			if(r != LIBBSA_OK)
				continue;
			data.resize(size);
			memcpy(data.data(),pdata,size);
			return true;
		}
		break;
	}
	case GameEngine::CreationEngine:
	{
		auto creationPath = GameMountManager::GetNormalizedGamebryoPath(fileName);
		for(auto &archive : m_archives)
		{
			auto ba2Handle = std::static_pointer_cast<BA2>(archive.handle);
			auto it = std::find_if(ba2Handle->nameTable.begin(),ba2Handle->nameTable.end(),[&creationPath](const std::string &other) {
				return ustring::compare(other,creationPath,false);
			});
			if(it == ba2Handle->nameTable.end())
				continue;
			data.clear();
			if(ba2Handle->Extract(it -ba2Handle->nameTable.begin(),data) != 1)
				continue;
			return true;
		}
		break;
	}
#endif
	}
	return false;
}

uarch::GameMountManager::~GameMountManager()
{
	m_cancel = true;
	if(m_loadThread.joinable())
		m_loadThread.join();
	hlShutdown();
}

std::vector<util::Path> uarch::GameMountManager::FindSteamGamePaths(const std::string &relPath)
{
	if(IsVerbose())
		std::cout<<"[uarch] Searching for steam game path '"<<relPath<<"'..."<<std::endl;

	auto rootRelPath = util::Path::CreatePath(relPath);
	while(rootRelPath.GetComponentCount() > 2) // strip down to 'common/<game>'
		rootRelPath.PopBack();

	std::vector<util::Path> candidates {};
	for(auto &steamPath : m_steamRootPaths)
	{
		auto fullPath = steamPath +"steamapps/" +relPath;
		if(IsVerbose())
			std::cout<<"[uarch] Checking '"<<fullPath<<"'...";
		auto result = FileManager::IsSystemDir(fullPath.GetString());
		if(IsVerbose())
			std::cout<<' '<<(result ? "Found!" : "Not found!")<<std::endl;
		if(result == false)
			continue;
		candidates.push_back(fullPath);
	}
	return candidates;
}

void uarch::GameMountManager::InitializeArchiveFileTable(uarch::ArchiveFileTable::Item &archiveDir,const hl::Archive::Directory &dir)
{
	std::vector<std::string> files;
	std::vector<hl::Archive::Directory> dirs;
	dir.GetItems(files,dirs);
	auto fConvertArchiveName = [](const std::string &f) {
		util::Path archFile {GetNormalizedPath(f)};
		if(archFile.IsEmpty() == false)
		{
			auto front = archFile.GetFront();
			if(front == "root")
				archFile.PopFront();
		}
		return archFile.GetString();
	};
	archiveDir.children.reserve(archiveDir.children.size() +files.size() +dirs.size());
	for(auto &f : files)
		archiveDir.children.push_back({fConvertArchiveName(f),false});
	for(auto &d : dirs)
	{
		archiveDir.children.push_back({fConvertArchiveName(d.GetPath()),true});
		InitializeArchiveFileTable(archiveDir.children.back(),d);
	}
}

void uarch::GameMountManager::MountWorkshopAddons(BaseMountedGame &game,SteamSettings::AppId appId)
{
	for(auto &steamPath : m_steamRootPaths)
	{
		auto path = steamPath +"/steamapps/workshop/content/" +std::to_string(appId) +"/";

		std::vector<std::string> workshopAddonPaths;
		FileManager::FindSystemFiles((path.GetString() +"*").c_str(),nullptr,&workshopAddonPaths,true);
		if(IsVerbose())
			std::cout<<"[uarch] Mounting "<<workshopAddonPaths.size()<<" workshop addons in '"<<path<<"'..."<<std::endl;
		for(auto &workshopAddonPath : workshopAddonPaths)
		{
			auto absWorkshopAddonPath = path +util::get_normalized_path(workshopAddonPath);
			if(IsVerbose())
				std::cout<<"[uarch] Mounting workshop addon '"<<absWorkshopAddonPath<<"'..."<<std::endl;
			// TODO

			std::vector<std::string> vpkFilePaths {};
			FileManager::FindSystemFiles((absWorkshopAddonPath.GetString() +"*.vpk").c_str(),&vpkFilePaths,nullptr,true);
			if(IsVerbose() && vpkFilePaths.empty() == false)
				std::cout<<"[uarch] Found "<<vpkFilePaths.size()<<" VPK archive files in workshop addon '"<<path<<"'! Mounting..."<<std::endl;
			for(auto &vpkFilePath : vpkFilePaths)
			{
				auto archive = hl::Archive::Create(absWorkshopAddonPath.GetString() +vpkFilePath);
				if(archive == nullptr)
					continue;
				if(IsVerbose())
					std::cout<<"[uarch] Mounting VPK archive '"<<absWorkshopAddonPath +vpkFilePath<<"'..."<<std::endl;
				// TODO
				//s_hlArchives.push_back(archive);
				//traverse_vpk_archive(s_hlArchives.back().root,archive->GetRoot());
			}
		}
	}
}

const std::vector<std::unique_ptr<uarch::BaseMountedGame>> &uarch::GameMountManager::GetMountedGames() const {return m_mountedGames;}

void uarch::GameMountManager::InitializeGame(const GameMountInfo &mountInfo,uint32_t gameMountInfoIdx)
{
	// Determine absolute game path on disk
	std::vector<std::string> absoluteGamePaths {};
	if(mountInfo.steamSettings.has_value())
	{
		if(IsVerbose())
			std::cout<<"[uarch] Found steam settings for game '"<<mountInfo.identifier<<"'! Attempting to locate game directory..."<<std::endl;
		for(auto &gamePath : mountInfo.steamSettings->gamePaths)
		{
			auto steamGamePaths = FindSteamGamePaths(gamePath);
			for(auto &steamGamePath : steamGamePaths)
			{
				if(IsVerbose())
					std::cout<<"[uarch] Successfully located game in '"<<steamGamePath<<"'! Adding to mount list..."<<std::endl;
				absoluteGamePaths.push_back(steamGamePath.GetString());
			}
		}
	}
	if(absoluteGamePaths.empty())
	{
		if(mountInfo.absolutePath.has_value())
		{
			auto result = FileManager::IsSystemDir(*mountInfo.absolutePath);
			if(IsVerbose())
			{
				if(result)
					std::cout<<"[uarch] Found game location for '"<<mountInfo.identifier<<"' in '"<<*mountInfo.absolutePath<<"'! Adding to mount list..."<<std::endl;
				else
					std::cout<<"[uarch] WARNING: Could not find directory '"<<*mountInfo.absolutePath<<"' for game '"<<mountInfo.identifier<<"'! Ignoring..."<<std::endl;
			}
			if(result)
				absoluteGamePaths.push_back(*mountInfo.absolutePath);
		}
		else if(IsVerbose())
			std::cout<<"[uarch] WARNING: No steam game path or absolute game path have been specified for game '"<<mountInfo.identifier<<"'! Is this intended?"<<std::endl;
	}

	if(absoluteGamePaths.empty())
	{
		if(IsVerbose())
			std::cout<<"[uarch] WARNING: Unable to locate absolute game path for game '"<<mountInfo.identifier<<"'! Skipping..."<<std::endl;
		return;
	}
	std::unique_ptr<BaseMountedGame> game = nullptr;
	switch(mountInfo.gameEngine)
	{
	case GameEngine::SourceEngine:
		game = std::make_unique<SourceEngineMountedGame>(GameEngine::SourceEngine);
		break;
	case GameEngine::Source2:
		game = std::make_unique<Source2MountedGame>(GameEngine::Source2);
		break;
#ifdef ENABLE_BETHESDA_FORMATS
	case GameEngine::Gamebryo:
		game = std::make_unique<GamebryoMountedGame>(GameEngine::Gamebryo);
		break;
	case GameEngine::CreationEngine:
		game = std::make_unique<CreationEngineMountedGame>(GameEngine::CreationEngine);
		break;
#endif
	}
	if(game == nullptr)
	{
		if(IsVerbose())
			std::cout<<"[uarch] Unsupported engine "<<to_string(mountInfo.gameEngine)<<" for game '"<<mountInfo.identifier<<"'! Skipping..."<<std::endl;
		return;
	}
	for(auto &absPath : absoluteGamePaths)
		game->MountPath(absPath);

	// Load archive files
	switch(mountInfo.gameEngine)
	{
	case uarch::GameEngine::SourceEngine:
	case uarch::GameEngine::Source2:
	{
		auto *engineData = static_cast<uarch::SourceEngineSettings*>(mountInfo.engineSettings.get());
		if(engineData)
		{
			if(IsVerbose())
				std::cout<<"[uarch] Mounting "<<engineData->vpkList.size()<<" VPK archive files for game '"<<mountInfo.identifier<<"'..."<<std::endl;
			for(auto &pair : engineData->vpkList)
			{
				auto found = false;
				for(auto &absGamePath : absoluteGamePaths)
				{
					util::Path vpkPath {absGamePath +pair.first};
					auto fileName = std::string{vpkPath.GetFileName()};
					ustring::to_lower(fileName);
					// pak01_dir is a common name across multiple Source Engine games, so it can appear multiple times
					if(m_mountedVPKArchives.find(fileName) != m_mountedVPKArchives.end() && ustring::compare<std::string>(fileName,"pak01_dir.vpk",false) == false)
					{
						if(IsVerbose())
							std::cout<<"[uarch] VPK '"<<fileName<<"' has already been loaded before! Ignoring..."<<std::endl;
						continue;
					}

					if(IsVerbose())
						std::cout<<"[uarch] Mounting VPK '"<<vpkPath.GetString()<<"'..."<<std::endl;
					auto archive = hl::Archive::Create(vpkPath.GetString());
					if(archive == nullptr)
						continue;
					found = true;
					m_mountedVPKArchives.insert(fileName);
					archive->SetRootDirectory(pair.second.rootDir);
					auto &fileTable = game->AddArchiveFileTable(archive);
					InitializeArchiveFileTable(fileTable.root,archive->GetRoot());
					break;
				}
				if(found == false && IsVerbose())
					std::cout<<"[uarch] WARNING: Unable to find VPK archive '"<<pair.first<<"' for game '"<<mountInfo.identifier<<"'!"<<std::endl;
			}
		}
		break;
	}
#ifdef ENABLE_BETHESDA_FORMATS
	case uarch::GameEngine::Gamebryo:
	{
		auto *engineData = static_cast<uarch::GamebryoSettings*>(mountInfo.engineSettings.get());
		if(engineData)
		{
			if(IsVerbose())
				std::cout<<"[uarch] Mounting "<<engineData->bsaList.size()<<" BSA archive files for game '"<<mountInfo.identifier<<"'..."<<std::endl;
			for(auto &pair : engineData->bsaList)
			{
				auto found = false;
				for(auto &absGamePath : absoluteGamePaths)
				{
					util::Path bsaPath {absGamePath +pair.first};
					if(IsVerbose())
						std::cout<<"[uarch] Mounting BSA '"<<bsaPath.GetString()<<"'..."<<std::endl;

					bsa_handle hBsa = nullptr;
					auto r = bsa_open(&hBsa,bsaPath.GetString().c_str());
					if(r != LIBBSA_OK)
						continue;
					found = true;
					auto &fileTable = game->AddArchiveFileTable(std::make_shared<bsa_handle>(hBsa));
					auto &assets = bsa_get_raw_assets(hBsa);
					for(auto &asset : assets)
						fileTable.root.Add(GetNormalizedGamebryoPath(asset.path),false);
				}
				if(found == false && IsVerbose())
					std::cout<<"[uarch] WARNING: Unable to find BSA archive '"<<pair.first<<"' for game '"<<mountInfo.identifier<<"'!"<<std::endl;
			}
		}
		break;
	}
	case uarch::GameEngine::CreationEngine:
	{
		auto *engineData = static_cast<uarch::CreationEngineSettings*>(mountInfo.engineSettings.get());
		if(engineData)
		{
			if(IsVerbose())
				std::cout<<"[uarch] Mounting "<<engineData->ba2List.size()<<" BA2 archive files for game '"<<mountInfo.identifier<<"'..."<<std::endl;
			for(auto &pair : engineData->ba2List)
			{
				auto found = false;
				for(auto &absGamePath : absoluteGamePaths)
				{
					util::Path bsaPath {absGamePath +pair.first};
					if(IsVerbose())
						std::cout<<"[uarch] Mounting BA2 '"<<bsaPath.GetString()<<"'..."<<std::endl;

					auto ba2 = std::make_shared<BA2>();
					try
					{
						if(ba2->Open(bsaPath.GetString().c_str()) == false)
							continue;
					}
					catch(const std::exception &e)
					{
						continue;
					}
					found = true;
					auto &fileTable = game->AddArchiveFileTable(ba2);
					for(auto &asset : ba2->nameTable)
						fileTable.root.Add(GetNormalizedGamebryoPath(asset),false);
				}
				if(found == false && IsVerbose())
					std::cout<<"[uarch] WARNING: Unable to find BA2 archive '"<<pair.first<<"' for game '"<<mountInfo.identifier<<"'!"<<std::endl;
			}
		}
		break;
	}
#endif
	}

	// Mount workshop
	if(mountInfo.steamSettings.has_value())
	{
		if(mountInfo.steamSettings->appId != std::numeric_limits<uarch::SteamSettings::AppId>::max())
			MountWorkshopAddons(*game,mountInfo.steamSettings->appId);
	}

	game->SetGameMountInfoIndex(gameMountInfoIdx);
	m_mountedGames.push_back(std::move(game));
}

void uarch::GameMountManager::SetVerbose(bool verbose) {m_verbose = verbose;}
bool uarch::GameMountManager::IsVerbose() const {return m_verbose;}

void uarch::GameMountManager::UpdateGamePriorities()
{
	auto &mountedGameInfos = GetGameMountInfos();
	auto &mountedGames = m_mountedGames;
	std::sort(mountedGames.begin(),mountedGames.end(),[&mountedGameInfos](const std::unique_ptr<uarch::BaseMountedGame> &game0,const std::unique_ptr<uarch::BaseMountedGame> &game1) {
		auto &info0 = mountedGameInfos[game0->GetGameMountInfoIndex()];
		auto &info1 = mountedGameInfos[game1->GetGameMountInfoIndex()];
		return info0.priority > info1.priority;
	});
}

void uarch::GameMountManager::WaitUntilInitializationComplete()
{
	if(m_loadThread.joinable())
		m_loadThread.join();
}
void uarch::GameMountManager::Start()
{
	if(m_initialized)
		return;
	m_initialized = true;
	m_loadThread = std::thread{[this]() {
		hlInitialize();
		std::string rootSteamPath;
#ifdef _WIN32
		if(util::get_registry_key_value(util::HKey::CurrentUser,"SOFTWARE\\Valve\\Steam","SteamPath",rootSteamPath) == true)
#else
		auto *pHomePath = getenv("HOME");
		if(pHomePath != nullptr)
			rootSteamPath = pHomePath;
		else
			rootSteamPath = "";
        rootSteamPath += "/.steam/root";
        char rootSteamPathLink[PATH_MAX];
        auto *result = realpath(rootSteamPath.c_str(),rootSteamPathLink);
        if(result != nullptr)
        {
            rootSteamPath = rootSteamPathLink;
        }
        else
        {
            std::cout<<"[uarch] Cannot find steam installation.";
            return;
        }
        //rootSteamPath += "/.local/share/Steam";
#endif
		{
			m_steamRootPaths.push_back(util::get_normalized_path(rootSteamPath));

			std::vector<std::string> additionalSteamPaths {};
			vdf::get_external_steam_locations(rootSteamPath,additionalSteamPaths);
			m_steamRootPaths.reserve(m_steamRootPaths.size() +additionalSteamPaths.size());
			for(auto &path : additionalSteamPaths)
				m_steamRootPaths.push_back(util::get_normalized_path(path));

			if(IsVerbose())
			{
				std::cout<<"[uarch] Found "<<m_steamRootPaths.size()<<" steam locations:"<<std::endl;
				for(auto &path : m_steamRootPaths)
					std::cout<<"[uarch] "<<path<<std::endl;
			}

			for(auto i=decltype(m_mountedGameInfos.size()){0u};i<m_mountedGameInfos.size();++i)
			{
				if(m_cancel)
					break;
				InitializeGame(m_mountedGameInfos[i],i);
			}

			if(m_cancel == false)
			{
				// Determine gmod addon paths
				// TODO
#if 0
				for(auto &steamPath : s_steamRootPaths)
				{
					std::string gmodAddonPath = steamPath +"/steamapps/common/GarrysMod/garrysmod/addons/";
					std::vector<std::string> addonDirs;
					FileManager::FindSystemFiles((gmodAddonPath +"*").c_str(),nullptr,&addonDirs);
					for(auto &d : addonDirs)
						add_source_game_path(gmodAddonPath +d +"/");
				}
#endif
			}
		}
	}};
	util::set_thread_name(m_loadThread,"uarch_game_mount");
}

bool uarch::GameMountManager::MountGame(const GameMountInfo &mountInfo)
{
	if(m_initialized)
		throw std::logic_error{"New games cannot be mounted after mount manager has been initialized!"};
	if(m_mountedGameInfos.size() == m_mountedGameInfos.capacity())
		m_mountedGameInfos.reserve(m_mountedGameInfos.size() *1.5f +50);
	m_mountedGameInfos.push_back(mountInfo);
	return true;
}

std::string uarch::GameMountManager::GetNormalizedPath(const std::string &path)
{
	auto cpy = path;
	ustring::to_lower(cpy);
	cpy = FileManager::GetNormalizedPath(cpy);
	return cpy;
}

std::string uarch::GameMountManager::GetNormalizedSourceEnginePath(const std::string &strPath)
{
	util::Path path {strPath};
	auto isRoot = (path.GetFront() == "..");
	path.Canonicalize();
	if(isRoot)
		path = "../" +path;
	if(path.IsEmpty() == false && ustring::compare<std::string_view>(path.GetFront(),"sounds",false))
	{
		path.PopFront();
		path = "sound/" +path;
	}
	return path.GetString();
}
#ifdef ENABLE_BETHESDA_FORMATS
std::string uarch::GameMountManager::GetNormalizedGamebryoPath(const std::string &strPath)
{
	util::Path path {GetNormalizedPath(strPath)};
	if(path.IsEmpty() == false)
	{
		auto front = path.GetFront();
		if(ustring::compare<std::string_view>(front,"sounds",false))
		{
			path.PopFront();
			path = "sound/" +path;
		}
		else if(ustring::compare<std::string_view>(front,"materials",false))
		{
			path.PopFront();
			path = "textures/" +path;
		}
		else if(ustring::compare<std::string_view>(front,"models",false))
			path.PopFront();
	}
	auto outPath = path.GetString();
	std::replace(outPath.begin(),outPath.end(),'/','\\');
	return outPath;
}
#endif

static std::unique_ptr<uarch::GameMountManager> g_gameMountManager = nullptr;

void uarch::setup()
{
	if(g_gameMountManager)
		return;
	g_gameMountManager = std::make_unique<uarch::GameMountManager>();
}
void uarch::initialize(bool bWait)
{
	if(g_gameMountManager == nullptr)
		return;
	g_gameMountManager->Start();
	if(bWait)
		g_gameMountManager->WaitUntilInitializationComplete();
}

void uarch::initialize()
{
	initialize(false);
}

std::optional<int32_t> uarch::get_mounted_game_priority(const std::string &gameIdentifier)
{
	setup();
	initialize(true);

	auto *game = g_gameMountManager->FindMountedGameByIdentifier(gameIdentifier);
	if(game == nullptr)
		return {};
	return g_gameMountManager->GetGameMountInfos()[game->GetGameMountInfoIndex()].priority;
}
void uarch::set_mounted_game_priority(const std::string &gameIdentifier,int32_t priority)
{
	setup();
	initialize(true);

	auto *game = g_gameMountManager->FindMountedGameByIdentifier(gameIdentifier);
	if(game == nullptr)
		return;
	const_cast<uarch::GameMountInfo&>(g_gameMountManager->GetGameMountInfos()[game->GetGameMountInfoIndex()]).priority = priority;
	g_gameMountManager->UpdateGamePriorities();
}

void uarch::set_verbose(bool verbose)
{
	if(g_gameMountManager)
		g_gameMountManager->SetVerbose(verbose);
}

bool uarch::mount_game(const GameMountInfo &mountInfo)
{
	setup();
	if(g_gameMountManager == nullptr)
		return false;
	g_gameMountManager->MountGame(mountInfo);
	return true;
}

const std::vector<uarch::GameMountInfo> &uarch::get_game_mount_infos()
{
	setup();
	initialize(false);
	return g_gameMountManager->GetGameMountInfos();
}

void uarch::close()
{
	g_gameMountManager = nullptr;
}

bool uarch::get_mounted_game_paths(const std::string &gameIdentifier,std::vector<std::string> &outPaths)
{
	setup();
	initialize(true);

	auto *game = g_gameMountManager->FindMountedGameByIdentifier(gameIdentifier);
	if(game == nullptr)
		return false;
	auto &mountedPaths = game->GetMountedPaths();
	outPaths.reserve(outPaths.size() +mountedPaths.size());
	for(auto &path : mountedPaths)
		outPaths.push_back(path.GetString());
	return true;
}

bool uarch::find_files(const std::string &fpath,std::vector<std::string> *files,std::vector<std::string> *dirs,bool keepAbsPaths,const std::optional<std::string> &gameIdentifier)
{
	setup();
	initialize(true);

	if(g_gameMountManager == nullptr)
		return false;
	auto &mountedGames = g_gameMountManager->GetMountedGames();
	if(gameIdentifier.has_value())
	{
		auto *game = g_gameMountManager->FindMountedGameByIdentifier(*gameIdentifier);
		if(game == nullptr)
			return false;
		game->FindFiles(fpath,files,dirs,keepAbsPaths);
	}
	else
	{
		for(auto &game : mountedGames)
			game->FindFiles(fpath,files,dirs,keepAbsPaths);
	}
	return true;
}

VFilePtr uarch::load(const std::string &path,std::optional<std::string> *optOutSourcePath,const std::optional<std::string> &gameIdentifier)
{
	setup();
	initialize(true);

	if(gameIdentifier.has_value())
	{
		auto *game = g_gameMountManager->FindMountedGameByIdentifier(*gameIdentifier);
		if(game == nullptr)
			return nullptr;
		return game->Load(path,optOutSourcePath);
	}
	for(auto &game : g_gameMountManager->GetMountedGames())
	{
		auto f = game->Load(path,optOutSourcePath);
		if(f)
			return f;
	}
	return nullptr;
}

bool uarch::load(const std::string &path,std::vector<uint8_t> &data)
{
	setup();
	initialize(true);

	for(auto &game : g_gameMountManager->GetMountedGames())
	{
		if(game->Load(path,data))
			return true;
	}
	return false;
}
#pragma optimize("",on)
