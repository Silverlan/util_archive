/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

module;

#include <vector>
#include <sharedutils/util.h>
#include <sharedutils/util_log.hpp>
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

module pragma.gamemount;

import :info;
import :archive;
import :archivedata;

static util::LogHandler g_logHandler;
static util::LogSeverity g_logSeverity = util::LogSeverity::Info;
static std::vector<util::Path> g_steamRootPaths;

void pragma::gamemount::set_log_handler(const util::LogHandler &loghandler) { g_logHandler = loghandler; }
void pragma::gamemount::set_log_severity(util::LogSeverity severity) { g_logSeverity = severity; }
static bool should_log(util::LogSeverity severity) { return g_logHandler != nullptr && (umath::to_integral(severity) >= umath::to_integral(g_logSeverity)); }
static void log(const std::string &msg, util::LogSeverity severity)
{
	if(!should_log(severity))
		return;
	g_logHandler(msg, severity);
}

pragma::gamemount::GameEngine pragma::gamemount::engine_name_to_enum(const std::string &name)
{
	static std::unordered_map<std::string, pragma::gamemount::GameEngine> engineNameToEnum {{"source_engine", GameEngine::SourceEngine}, {"source2", GameEngine::Source2},
#ifdef ENABLE_BETHESDA_FORMATS
	  {"gamebryo", GameEngine::Gamebryo}, {"creation_engine", GameEngine::CreationEngine},
#endif
	  {"other", GameEngine::Other}};
	static_assert(umath::to_integral(pragma::gamemount::GameEngine::Count) == 5);
	auto it = engineNameToEnum.find(name);
	return (it != engineNameToEnum.end()) ? it->second : GameEngine::Invalid;
}

std::string pragma::gamemount::to_string(GameEngine engine)
{
	switch(engine) {
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
	static_assert(umath::to_integral(pragma::gamemount::GameEngine::Count) == 5);
	return "invalid";
}

pragma::gamemount::BaseEngineSettings *pragma::gamemount::GameMountInfo::SetEngine(GameEngine engine)
{
	switch(engine) {
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

namespace pragma::gamemount {
	void setup();
	void initialize(bool bWait);
	class BaseMountedGame {
	  public:
		const std::vector<util::Path> &GetMountedPaths() const;
		const std::vector<ArchiveFileTable> &GetArchives() const;
		void FindFiles(const std::string &fpath, std::vector<std::string> *optOutFiles, std::vector<std::string> *optOutDirs, bool keepAbsPaths = false);
		bool Load(const std::string &path, std::vector<uint8_t> &data);
		VFilePtr Load(const std::string &path, std::optional<std::string> *optOutSourcePath = nullptr);

		void MountPath(const std::string &path);
		ArchiveFileTable &AddArchiveFileTable(const std::string &fileName, const std::shared_ptr<void> &phandle);
		const std::string &GetIdentifier() const { return m_identifier; }

		void SetGameMountInfoIndex(uint32_t gameMountInfoIdx) { m_gameMountInfoIdx = gameMountInfoIdx; }
		uint32_t GetGameMountInfoIndex() const { return m_gameMountInfoIdx; }
	  protected:
		BaseMountedGame(const std::string &identifier, GameEngine gameEngine);
	  private:
		GameEngine m_gameEngine = GameEngine::Invalid;
		uint32_t m_gameMountInfoIdx = 0;
		std::string m_identifier;
		std::vector<util::Path> m_mountedPaths {};
		std::vector<ArchiveFileTable> m_archives {};
	};

	class SourceEngineMountedGame : public BaseMountedGame {
	  public:
		SourceEngineMountedGame(const std::string &identifier, GameEngine gameEngine) : BaseMountedGame {identifier, gameEngine} {}
	};

	class Source2MountedGame : public SourceEngineMountedGame {
	  public:
		Source2MountedGame(const std::string &identifier, GameEngine gameEngine) : SourceEngineMountedGame {identifier, gameEngine} {}
	};

#ifdef ENABLE_BETHESDA_FORMATS
	class GamebryoMountedGame : public BaseMountedGame {
	  public:
		GamebryoMountedGame(const std::string &identifier, GameEngine gameEngine) : BaseMountedGame {identifier, gameEngine} {}
	};

	class CreationEngineMountedGame : public BaseMountedGame {
	  public:
		CreationEngineMountedGame(const std::string &identifier, GameEngine gameEngine) : BaseMountedGame {identifier, gameEngine} {}
	};
#endif

	class GameMountManager {
	  public:
		GameMountManager() = default;
		GameMountManager(const GameMountManager &) = delete;
		GameMountManager &operator=(const GameMountManager &) = delete;
		~GameMountManager();
		bool MountGame(const GameMountInfo &mountInfo);
		void Start();
		void WaitUntilInitializationComplete();

		void InitializeGame(const GameMountInfo &mountInfo, uint32_t gameMountInfoIdx);
		const std::vector<std::unique_ptr<BaseMountedGame>> &GetMountedGames() const;
		const std::vector<GameMountInfo> &GetGameMountInfos() const { return m_mountedGameInfos; }
		void UpdateGamePriorities();

		const GameMountInfo *FindGameMountInfo(const std::string &identifier) const
		{
			auto &gameMountInfos = GetGameMountInfos();
			auto it = std::find_if(gameMountInfos.begin(), gameMountInfos.end(), [&identifier](const GameMountInfo &mountInfo) { return ustring::compare(mountInfo.identifier, identifier, false); });
			if(it == gameMountInfos.end())
				return nullptr;
			return &*it;
		}

		const BaseMountedGame *FindMountedGameByIdentifier(const std::string &identifier) const { return const_cast<GameMountManager *>(this)->FindMountedGameByIdentifier(identifier); }
		BaseMountedGame *FindMountedGameByIdentifier(const std::string &identifier)
		{
			auto &gameMountInfos = GetGameMountInfos();
			auto it = std::find_if(gameMountInfos.begin(), gameMountInfos.end(), [&identifier](const GameMountInfo &mountInfo) { return ustring::compare(mountInfo.identifier, identifier, false); });
			if(it == gameMountInfos.end())
				return nullptr;
			auto idx = it - gameMountInfos.begin();
			auto itGame = std::find_if(m_mountedGames.begin(), m_mountedGames.end(), [idx](const std::unique_ptr<BaseMountedGame> &game) { return game->GetGameMountInfoIndex() == idx; });
			if(itGame == m_mountedGames.end())
				return nullptr;
			return itGame->get();
		}

		const std::unordered_map<std::string, util::Path> &GetMountedVpkArchives() const { return m_mountedVPKArchives; }

		static std::string GetNormalizedPath(const std::string &path);
		static std::string GetNormalizedSourceEnginePath(const std::string &path);
#ifdef ENABLE_BETHESDA_FORMATS
		static std::string GetNormalizedGamebryoPath(const std::string &path);
#endif
	  private:
		static void InitializeArchiveFileTable(pragma::gamemount::ArchiveFileTable::Item &archiveDir, const pragma::gamemount::hl::Archive::Directory &dir);

		std::vector<util::Path> FindSteamGamePaths(const std::string &relPath);
		void MountWorkshopAddons(BaseMountedGame &game, SteamSettings::AppId appId);

		std::vector<GameMountInfo> m_mountedGameInfos {};
		std::vector<std::unique_ptr<BaseMountedGame>> m_mountedGames {};

		std::thread m_loadThread;
		bool m_initialized = false;
		std::atomic<bool> m_cancel = false;

		std::unordered_map<std::string, util::Path> m_mountedVPKArchives {};
	};
};

pragma::gamemount::BaseMountedGame::BaseMountedGame(const std::string &identifier, GameEngine gameEngine) : m_gameEngine {gameEngine}, m_identifier {identifier} {}
void pragma::gamemount::BaseMountedGame::MountPath(const std::string &path)
{
	if(m_mountedPaths.size() == m_mountedPaths.capacity())
		m_mountedPaths.reserve(m_mountedPaths.size() * 1.5f + 100);
	m_mountedPaths.push_back(path);
}
pragma::gamemount::ArchiveFileTable &pragma::gamemount::BaseMountedGame::AddArchiveFileTable(const std::string &fileName, const std::shared_ptr<void> &phandle)
{
	if(m_archives.size() == m_archives.capacity())
		m_archives.reserve(m_archives.size() * 1.5 + 50);
	m_archives.push_back({phandle});
	m_archives.back().identifier = fileName;
	return m_archives.back();
}

const std::vector<util::Path> &pragma::gamemount::BaseMountedGame::GetMountedPaths() const { return m_mountedPaths; }
const std::vector<pragma::gamemount::ArchiveFileTable> &pragma::gamemount::BaseMountedGame::GetArchives() const { return m_archives; }

void pragma::gamemount::BaseMountedGame::FindFiles(const std::string &fpath, std::vector<std::string> *optOutFiles, std::vector<std::string> *optOutDirs, bool keepAbsPaths)
{
	auto npath = fpath;
	switch(m_gameEngine) {
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

	for(auto &path : GetMountedPaths()) {
		auto foffset = optOutFiles ? optOutFiles->size() : 0;
		auto doffset = optOutDirs ? optOutDirs->size() : 0;
		auto searchPath = util::Path::CreatePath(FileManager::GetCanonicalizedPath(path.GetString() + ufile::get_path_from_filename(npath)));
		FileManager::FindSystemFiles((searchPath.GetString() + ufile::get_file_from_filename(npath)).c_str(), optOutFiles, optOutDirs);
		if(keepAbsPaths) {
			if(optOutFiles) {
				for(auto i = foffset; i < optOutFiles->size(); ++i)
					(*optOutFiles)[i] = (searchPath + util::Path::CreateFile((*optOutFiles)[i])).GetString();
			}
			if(optOutDirs) {
				for(auto i = doffset; i < optOutDirs->size(); ++i)
					(*optOutDirs)[i] = (searchPath + util::Path::CreateFile((*optOutDirs)[i])).GetString();
			}
		}
	}
	if(keepAbsPaths)
		return;
	npath = GameMountManager::GetNormalizedPath(npath);
	const auto fSearchArchive = [optOutFiles, optOutDirs, &npath](const auto &data) {
		util::Path archPath {npath};
		auto pathList = archPath.ToComponents();
		auto itBegin = pathList.begin();
		auto itEnd = pathList.end();
		for(auto &arch : data) {
			auto *dir = &arch.root;
			for(auto it = itBegin; it != itEnd; ++it) {
				auto &d = *it;
				if(it == itEnd - 1) {
					for(auto &child : dir->children) {
						if(ustring::match(child.name, d) == false)
							continue;
						if(child.directory == false) {
							if(optOutFiles != nullptr)
								optOutFiles->push_back(child.name);
						}
						else {
							if(optOutDirs != nullptr)
								optOutDirs->push_back(child.name);
						}
					}
				}
				else {
					auto itChild = std::find_if(dir->children.begin(), dir->children.end(), [&d](const decltype(*dir) &dirSub) { return (dirSub.directory == true && ustring::match(dirSub.name, d) == true) ? true : false; });
					if(itChild == dir->children.end())
						break;
					dir = &(*itChild);
				}
			}
		}
	};
	fSearchArchive(m_archives);
}
VFilePtr pragma::gamemount::BaseMountedGame::Load(const std::string &fileName, std::optional<std::string> *optOutSourcePath)
{
	if(should_log(util::LogSeverity::Trace))
		log("[" + GetIdentifier() + "] Loading file '" + fileName + "'...", util::LogSeverity::Trace);
	auto npath = fileName;
	switch(m_gameEngine) {
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

	for(auto &path : GetMountedPaths()) {
		auto filePath = path;
		filePath += npath;
		if(should_log(util::LogSeverity::Trace))
			log("[" + GetIdentifier() + "] Checking system file '" + filePath.GetString() + "'...", util::LogSeverity::Trace);
		auto f = FileManager::OpenSystemFile(filePath.GetString().c_str(), "rb");
		if(f) {
			if(optOutSourcePath)
				*optOutSourcePath = filePath.GetString();
			if(should_log(util::LogSeverity::Trace))
				log("[" + GetIdentifier() + "] Found!", util::LogSeverity::Trace);
			return f;
		}
	}
	if(should_log(util::LogSeverity::Trace))
		log("[" + GetIdentifier() + "] File not found on disk within mounted games!", util::LogSeverity::Trace);
	auto data = std::make_shared<std::vector<uint8_t>>();
	if(Load(fileName, *data) == false)
		return nullptr;
	if(optOutSourcePath)
		*optOutSourcePath = npath;
	FileManager::AddVirtualFile(npath, data);
	return FileManager::OpenFile(npath.c_str(), "rb");
}
bool pragma::gamemount::BaseMountedGame::Load(const std::string &fileName, std::vector<uint8_t> &data)
{
	if(should_log(util::LogSeverity::Trace))
		log("[" + GetIdentifier() + "] Loading file '" + fileName + "' from mounted archives...", util::LogSeverity::Trace);
	initialize(true);

	switch(m_gameEngine) {
	case GameEngine::SourceEngine:
	case GameEngine::Source2:
		{
			auto srcPath = GameMountManager::GetNormalizedSourceEnginePath(fileName);
			for(auto &archive : m_archives) {
				auto pArchive = std::static_pointer_cast<pragma::gamemount::hl::Archive>(archive.handle);
				if(should_log(util::LogSeverity::Trace))
					log("[" + GetIdentifier() + "] Checking archive '" + archive.identifier + "'...", util::LogSeverity::Trace);
				auto stream = pArchive->OpenFile(srcPath);
				if(stream == nullptr)
					continue;
				if(should_log(util::LogSeverity::Trace))
					log("[" + GetIdentifier() + "] Found!", util::LogSeverity::Trace);
				if(stream->Read(data) == true)
					return true;
				if(should_log(util::LogSeverity::Trace))
					log("[" + GetIdentifier() + "] Failed to read data stream.", util::LogSeverity::Trace);
			}
			break;
		}
#ifdef ENABLE_BETHESDA_FORMATS
	case GameEngine::Gamebryo:
		{
			auto gamebyroPath = GameMountManager::GetNormalizedGamebryoPath(fileName);
			for(auto &archive : m_archives) {
				auto bsaHandle = std::static_pointer_cast<bsa_handle>(archive.handle);
				bool result;
				auto r = bsa_contains_asset(*bsaHandle, gamebyroPath.c_str(), &result);
				if(r != LIBBSA_OK || result == false)
					continue;
				const uint8_t *pdata = nullptr;
				std::size_t size = 0;
				r = bsa_extract_asset_to_memory(*bsaHandle, gamebyroPath.c_str(), &pdata, &size);
				if(r != LIBBSA_OK)
					continue;
				data.resize(size);
				memcpy(data.data(), pdata, size);
				return true;
			}
			break;
		}
	case GameEngine::CreationEngine:
		{
			auto creationPath = GameMountManager::GetNormalizedGamebryoPath(fileName);
			for(auto &archive : m_archives) {
				auto ba2Handle = std::static_pointer_cast<BA2>(archive.handle);
				auto it = std::find_if(ba2Handle->nameTable.begin(), ba2Handle->nameTable.end(), [&creationPath](const std::string &other) { return ustring::compare(other, creationPath, false); });
				if(it == ba2Handle->nameTable.end())
					continue;
				data.clear();
				if(ba2Handle->Extract(it - ba2Handle->nameTable.begin(), data) != 1)
					continue;
				return true;
			}
			break;
		}
#endif
	}
	if(should_log(util::LogSeverity::Trace))
		log("[" + GetIdentifier() + "] Not found in mounted archives...", util::LogSeverity::Trace);
	return false;
}

pragma::gamemount::GameMountManager::~GameMountManager()
{
	m_cancel = true;
	if(m_loadThread.joinable())
		m_loadThread.join();
	hlShutdown();
}

std::vector<util::Path> pragma::gamemount::GameMountManager::FindSteamGamePaths(const std::string &relPath)
{
	if(should_log(util::LogSeverity::Info))
		log("Searching for steam game path '" + relPath + "'...", util::LogSeverity::Info);

	auto rootRelPath = util::Path::CreatePath(relPath);
	while(rootRelPath.GetComponentCount() > 2) // strip down to 'common/<game>'
		rootRelPath.PopBack();

	std::vector<util::Path> candidates {};
	for(auto &steamPath : g_steamRootPaths) {
		auto fullPath = steamPath + "steamapps/" + relPath;
		if(should_log(util::LogSeverity::Info))
			log("Checking '" + fullPath.GetString() + "'...", util::LogSeverity::Info);
		auto result = FileManager::IsSystemDir(fullPath.GetString());
		if(should_log(util::LogSeverity::Info))
			log(result ? "Found!" : "Not found!", util::LogSeverity::Info);
		if(result == false)
			continue;
		candidates.push_back(fullPath);
	}
	return candidates;
}

void pragma::gamemount::GameMountManager::InitializeArchiveFileTable(pragma::gamemount::ArchiveFileTable::Item &archiveDir, const pragma::gamemount::hl::Archive::Directory &dir)
{
	std::vector<std::string> files;
	std::vector<pragma::gamemount::hl::Archive::Directory> dirs;
	dir.GetItems(files, dirs);
	auto fConvertArchiveName = [](const std::string &f) {
		util::Path archFile {GetNormalizedPath(f)};
		if(archFile.IsEmpty() == false) {
			auto front = archFile.GetFront();
			if(front == "root")
				archFile.PopFront();
		}
		return archFile.GetString();
	};
	archiveDir.children.reserve(archiveDir.children.size() + files.size() + dirs.size());
	for(auto &f : files)
		archiveDir.children.push_back({fConvertArchiveName(f), false});
	for(auto &d : dirs) {
		archiveDir.children.push_back({fConvertArchiveName(d.GetPath()), true});
		InitializeArchiveFileTable(archiveDir.children.back(), d);
	}
}

void pragma::gamemount::GameMountManager::MountWorkshopAddons(BaseMountedGame &game, SteamSettings::AppId appId)
{
	for(auto &steamPath : g_steamRootPaths) {
		auto path = steamPath + "/steamapps/workshop/content/" + std::to_string(appId) + "/";

		std::vector<std::string> workshopAddonPaths;
		FileManager::FindSystemFiles((path.GetString() + "*").c_str(), nullptr, &workshopAddonPaths, true);
		if(should_log(util::LogSeverity::Info))
			log("Mounting " + std::to_string(workshopAddonPaths.size()) + " workshop addons in '" + path.GetString() + "'...", util::LogSeverity::Info);
		for(auto &workshopAddonPath : workshopAddonPaths) {
			auto absWorkshopAddonPath = path + util::get_normalized_path(workshopAddonPath);
			if(should_log(util::LogSeverity::Info))
				log("Mounting workshop addon '" + absWorkshopAddonPath.GetString() + "'...", util::LogSeverity::Info);
			// TODO

			std::vector<std::string> vpkFilePaths {};
			FileManager::FindSystemFiles((absWorkshopAddonPath.GetString() + "*.vpk").c_str(), &vpkFilePaths, nullptr, true);
			if(should_log(util::LogSeverity::Info) && vpkFilePaths.empty() == false)
				log("Found " + std::to_string(vpkFilePaths.size()) + " VPK archive files in workshop addon '" + path.GetString() + "'! Mounting...", util::LogSeverity::Info);
			for(auto &vpkFilePath : vpkFilePaths) {
				auto archive = pragma::gamemount::hl::Archive::Create(absWorkshopAddonPath.GetString() + vpkFilePath);
				if(archive == nullptr)
					continue;
				if(should_log(util::LogSeverity::Info))
					log("Mounting VPK archive '" + absWorkshopAddonPath.GetString() + vpkFilePath + "'...", util::LogSeverity::Info);
				// TODO
				//s_hlArchives.push_back(archive);
				//traverse_vpk_archive(s_hlArchives.back().root,archive->GetRoot());
			}
		}
	}
}

const std::vector<std::unique_ptr<pragma::gamemount::BaseMountedGame>> &pragma::gamemount::GameMountManager::GetMountedGames() const { return m_mountedGames; }

void pragma::gamemount::GameMountManager::InitializeGame(const GameMountInfo &mountInfo, uint32_t gameMountInfoIdx)
{
	// Determine absolute game path on disk
	std::vector<std::string> absoluteGamePaths {};
	if(mountInfo.steamSettings.has_value()) {
		if(should_log(util::LogSeverity::Info))
			log("Found steam settings for game '" + mountInfo.identifier + "'! Attempting to locate game directory...", util::LogSeverity::Info);
		for(auto &gamePath : mountInfo.steamSettings->gamePaths) {
			auto steamGamePaths = FindSteamGamePaths(gamePath);
			for(auto &steamGamePath : steamGamePaths) {
				if(should_log(util::LogSeverity::Info))
					log("Successfully located game in '" + steamGamePath.GetString() + "'! Adding to mount list...", util::LogSeverity::Info);
				absoluteGamePaths.push_back(steamGamePath.GetString());
			}
		}
	}
	if(absoluteGamePaths.empty()) {
		if(mountInfo.absolutePath.has_value()) {
			auto result = FileManager::IsSystemDir(*mountInfo.absolutePath);
			if(should_log(util::LogSeverity::Info)) {
				if(result)
					log("Found game location for '" + mountInfo.identifier + "' in '" + *mountInfo.absolutePath + "'! Adding to mount list...", util::LogSeverity::Info);
				else
					log("Could not find directory '" + *mountInfo.absolutePath + "' for game '" + mountInfo.identifier + "'! Ignoring...", util::LogSeverity::Warning);
			}
			if(result)
				absoluteGamePaths.push_back(*mountInfo.absolutePath);
		}
		else if(should_log(util::LogSeverity::Warning))
			log("No steam game path or absolute game path have been specified for game '" + mountInfo.identifier + "'! Is this intended?", util::LogSeverity::Warning);
	}

	if(absoluteGamePaths.empty()) {
		if(should_log(util::LogSeverity::Warning))
			log("Unable to locate absolute game path for game '" + mountInfo.identifier + "'! Skipping...", util::LogSeverity::Warning);
		return;
	}
	std::unique_ptr<BaseMountedGame> game = nullptr;
	switch(mountInfo.gameEngine) {
	case GameEngine::SourceEngine:
		game = std::make_unique<SourceEngineMountedGame>(mountInfo.identifier, mountInfo.gameEngine);
		break;
	case GameEngine::Source2:
		game = std::make_unique<Source2MountedGame>(mountInfo.identifier, mountInfo.gameEngine);
		break;
#ifdef ENABLE_BETHESDA_FORMATS
	case GameEngine::Gamebryo:
		game = std::make_unique<GamebryoMountedGame>(mountInfo.identifier, mountInfo.gameEngine);
		break;
	case GameEngine::CreationEngine:
		game = std::make_unique<CreationEngineMountedGame>(mountInfo.identifier, mountInfo.gameEngine);
		break;
#endif
	}
	if(game == nullptr) {
		if(should_log(util::LogSeverity::Warning))
			log("Unsupported engine " + to_string(mountInfo.gameEngine) + " for game '" + mountInfo.identifier + "'! Skipping...", util::LogSeverity::Warning);
		return;
	}
	for(auto &absPath : absoluteGamePaths)
		game->MountPath(absPath);

	// Load archive files
	switch(mountInfo.gameEngine) {
	case pragma::gamemount::GameEngine::SourceEngine:
	case pragma::gamemount::GameEngine::Source2:
		{
			auto *engineData = static_cast<pragma::gamemount::SourceEngineSettings *>(mountInfo.engineSettings.get());
			if(engineData) {
				if(should_log(util::LogSeverity::Info))
					log("Mounting " + std::to_string(engineData->vpkList.size()) + " VPK archive files for game '" + mountInfo.identifier + "'...", util::LogSeverity::Info);
				for(auto &pair : engineData->vpkList) {
					auto found = false;
					for(auto &absGamePath : absoluteGamePaths) {
						util::Path vpkPath {absGamePath + pair.first};
						auto fileName = std::string {vpkPath.GetFileName()};
						ustring::to_lower(fileName);
						// pak01_dir is a common name across multiple Source Engine games, so it can appear multiple times
						if(m_mountedVPKArchives.find(fileName) != m_mountedVPKArchives.end() && ustring::compare<std::string>(fileName, "pak01_dir.vpk", false) == false) {
							if(should_log(util::LogSeverity::Info))
								log("VPK '" + fileName + "' has already been loaded before! Ignoring...", util::LogSeverity::Info);
							continue;
						}

						if(should_log(util::LogSeverity::Info))
							log("Mounting VPK '" + vpkPath.GetString() + "'...", util::LogSeverity::Info);
						auto archive = pragma::gamemount::hl::Archive::Create(vpkPath.GetString());
						if(archive == nullptr)
							continue;
						found = true;
						m_mountedVPKArchives.insert(std::make_pair(fileName, vpkPath));
						archive->SetRootDirectory(pair.second.rootDir);
						auto &fileTable = game->AddArchiveFileTable(fileName, archive);
						InitializeArchiveFileTable(fileTable.root, archive->GetRoot());
						break;
					}
					if(found == false && should_log(util::LogSeverity::Warning))
						log("Unable to find VPK archive '" + pair.first + "' for game '" + mountInfo.identifier + "'!", util::LogSeverity::Warning);
				}
			}
			break;
		}
#ifdef ENABLE_BETHESDA_FORMATS
	case pragma::gamemount::GameEngine::Gamebryo:
		{
			auto *engineData = static_cast<pragma::gamemount::GamebryoSettings *>(mountInfo.engineSettings.get());
			if(engineData) {
				if(should_log(util::LogSeverity::Info))
					log("Mounting " << engineData->bsaList.size() << " BSA archive files for game '" << mountInfo.identifier << "'...", util::LogSeverity::Info);
				for(auto &pair : engineData->bsaList) {
					auto found = false;
					for(auto &absGamePath : absoluteGamePaths) {
						util::Path bsaPath {absGamePath + pair.first};
						if(should_log(util::LogSeverity::Info))
							log("Mounting BSA '" << bsaPath.GetString() << "'...", util::LogSeverity::Info);

						bsa_handle hBsa = nullptr;
						auto r = bsa_open(&hBsa, bsaPath.GetString().c_str());
						if(r != LIBBSA_OK)
							continue;
						found = true;
						auto &fileTable = game->AddArchiveFileTable(std::make_shared<bsa_handle>(hBsa));
						auto &assets = bsa_get_raw_assets(hBsa);
						for(auto &asset : assets)
							fileTable.root.Add(GetNormalizedGamebryoPath(asset.path), false);
					}
					if(found == false && IsVerbose())
						log("Unable to find BSA archive '" << pair.first << "' for game '" << mountInfo.identifier << "'!", util::LogSeverity::Warning);
				}
			}
			break;
		}
	case pragma::gamemount::GameEngine::CreationEngine:
		{
			auto *engineData = static_cast<pragma::gamemount::CreationEngineSettings *>(mountInfo.engineSettings.get());
			if(engineData) {
				if(should_log(util::LogSeverity::Info))
					log("Mounting " << engineData->ba2List.size() << " BA2 archive files for game '" << mountInfo.identifier << "'...", util::LogSeverity::Info);
				for(auto &pair : engineData->ba2List) {
					auto found = false;
					for(auto &absGamePath : absoluteGamePaths) {
						util::Path bsaPath {absGamePath + pair.first};
						if(should_log(util::LogSeverity::Info))
							log("Mounting BA2 '" << bsaPath.GetString() << "'...", util::LogSeverity::Info);

						auto ba2 = std::make_shared<BA2>();
						try {
							if(ba2->Open(bsaPath.GetString().c_str()) == false)
								continue;
						}
						catch(const std::exception &e) {
							continue;
						}
						found = true;
						auto &fileTable = game->AddArchiveFileTable(ba2);
						for(auto &asset : ba2->nameTable)
							fileTable.root.Add(GetNormalizedGamebryoPath(asset), false);
					}
					if(found == false && IsVerbose())
						log("Unable to find BA2 archive '" << pair.first << "' for game '" << mountInfo.identifier << "'!", util::LogSeverity::Warning);
				}
			}
			break;
		}
#endif
	}

	// Mount workshop
	if(mountInfo.steamSettings.has_value()) {
		if(mountInfo.steamSettings->appId != std::numeric_limits<pragma::gamemount::SteamSettings::AppId>::max())
			MountWorkshopAddons(*game, mountInfo.steamSettings->appId);
	}

	game->SetGameMountInfoIndex(gameMountInfoIdx);
	m_mountedGames.push_back(std::move(game));
}

void pragma::gamemount::GameMountManager::UpdateGamePriorities()
{
	auto &mountedGameInfos = GetGameMountInfos();
	auto &mountedGames = m_mountedGames;
	std::sort(mountedGames.begin(), mountedGames.end(), [&mountedGameInfos](const std::unique_ptr<pragma::gamemount::BaseMountedGame> &game0, const std::unique_ptr<pragma::gamemount::BaseMountedGame> &game1) {
		auto &info0 = mountedGameInfos[game0->GetGameMountInfoIndex()];
		auto &info1 = mountedGameInfos[game1->GetGameMountInfoIndex()];
		return info0.priority > info1.priority;
	});
}

void pragma::gamemount::GameMountManager::WaitUntilInitializationComplete()
{
	if(m_loadThread.joinable())
		m_loadThread.join();
}
void pragma::gamemount::GameMountManager::Start()
{
	if(m_initialized)
		return;
	m_initialized = true;
	m_loadThread = std::thread {[this]() {
		hlInitialize();
		
		if(!g_steamRootPaths.empty())
		{
			if(should_log(util::LogSeverity::Info)) {
				log("Found " + std::to_string(g_steamRootPaths.size()) + " steam locations:", util::LogSeverity::Info);
				for(auto &path : g_steamRootPaths)
					log(path.GetString(), util::LogSeverity::Info);
			}

			for(auto i = decltype(m_mountedGameInfos.size()) {0u}; i < m_mountedGameInfos.size(); ++i) {
				if(m_cancel)
					break;
				InitializeGame(m_mountedGameInfos[i], i);
			}

			if(m_cancel == false) {
				// Determine gmod addon paths
				// TODO
#if 0
				for(auto &steamPath : g_steamRootPaths)
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
	util::set_thread_name(m_loadThread, "uarch_game_mount");
}

bool pragma::gamemount::GameMountManager::MountGame(const GameMountInfo &mountInfo)
{
	if(m_initialized)
		throw std::logic_error {"New games cannot be mounted after mount manager has been initialized!"};
	if(m_mountedGameInfos.size() == m_mountedGameInfos.capacity())
		m_mountedGameInfos.reserve(m_mountedGameInfos.size() * 1.5f + 50);
	m_mountedGameInfos.push_back(mountInfo);
	return true;
}

std::string pragma::gamemount::GameMountManager::GetNormalizedPath(const std::string &path)
{
	auto cpy = path;
	ustring::to_lower(cpy);
	cpy = FileManager::GetNormalizedPath(cpy);
	return cpy;
}

std::string pragma::gamemount::GameMountManager::GetNormalizedSourceEnginePath(const std::string &strPath)
{
	util::Path path {strPath};
	auto isRoot = (path.GetFront() == "..");
	path.Canonicalize();
	if(isRoot)
		path = "../" + path;
	if(path.IsEmpty() == false && ustring::compare<std::string_view>(path.GetFront(), "sounds", false)) {
		path.PopFront();
		path = "sound/" + path;
	}
	return path.GetString();
}
#ifdef ENABLE_BETHESDA_FORMATS
std::string pragma::gamemount::GameMountManager::GetNormalizedGamebryoPath(const std::string &strPath)
{
	util::Path path {GetNormalizedPath(strPath)};
	if(path.IsEmpty() == false) {
		auto front = path.GetFront();
		if(ustring::compare<std::string_view>(front, "sounds", false)) {
			path.PopFront();
			path = "sound/" + path;
		}
		else if(ustring::compare<std::string_view>(front, "materials", false)) {
			path.PopFront();
			path = "textures/" + path;
		}
		else if(ustring::compare<std::string_view>(front, "models", false))
			path.PopFront();
	}
	auto outPath = path.GetString();
	std::replace(outPath.begin(), outPath.end(), '/', '\\');
	return outPath;
}
#endif

static std::unique_ptr<pragma::gamemount::GameMountManager> g_gameMountManager = nullptr;

void pragma::gamemount::setup()
{
	if(g_gameMountManager)
		return;
	g_gameMountManager = std::make_unique<pragma::gamemount::GameMountManager>();
}
void pragma::gamemount::initialize(bool bWait)
{
	if(g_gameMountManager == nullptr)
		return;
	g_gameMountManager->Start();
	if(bWait)
		g_gameMountManager->WaitUntilInitializationComplete();
}

void pragma::gamemount::initialize() { initialize(false); }

std::optional<int32_t> pragma::gamemount::get_mounted_game_priority(const std::string &gameIdentifier)
{
	setup();
	initialize(true);

	auto *game = g_gameMountManager->FindMountedGameByIdentifier(gameIdentifier);
	if(game == nullptr)
		return {};
	return g_gameMountManager->GetGameMountInfos()[game->GetGameMountInfoIndex()].priority;
}
void pragma::gamemount::set_mounted_game_priority(const std::string &gameIdentifier, int32_t priority)
{
	setup();
	initialize(true);

	auto *game = g_gameMountManager->FindMountedGameByIdentifier(gameIdentifier);
	if(game == nullptr)
		return;
	const_cast<pragma::gamemount::GameMountInfo &>(g_gameMountManager->GetGameMountInfos()[game->GetGameMountInfoIndex()]).priority = priority;
	g_gameMountManager->UpdateGamePriorities();
}

bool pragma::gamemount::mount_game(const GameMountInfo &mountInfo)
{
	setup();
	if(g_gameMountManager == nullptr)
		return false;
	g_gameMountManager->MountGame(mountInfo);
	return true;
}

const std::unordered_map<std::string, util::Path> &pragma::gamemount::get_mounted_vpk_archives()
{
	setup();
	initialize(false);
	return g_gameMountManager->GetMountedVpkArchives();
}

const std::vector<pragma::gamemount::GameMountInfo> &pragma::gamemount::get_game_mount_infos()
{
	setup();
	initialize(false);
	return g_gameMountManager->GetGameMountInfos();
}

void pragma::gamemount::close() { g_gameMountManager = nullptr; }

bool pragma::gamemount::get_mounted_game_paths(const std::string &gameIdentifier, std::vector<std::string> &outPaths)
{
	setup();
	initialize(true);

	auto *game = g_gameMountManager->FindMountedGameByIdentifier(gameIdentifier);
	if(game == nullptr)
		return false;
	auto &mountedPaths = game->GetMountedPaths();
	outPaths.reserve(outPaths.size() + mountedPaths.size());
	for(auto &path : mountedPaths)
		outPaths.push_back(path.GetString());
	return true;
}

bool pragma::gamemount::find_files(const std::string &fpath, std::vector<std::string> *files, std::vector<std::string> *dirs, bool keepAbsPaths, const std::optional<std::string> &gameIdentifier)
{
	setup();
	initialize(true);

	if(g_gameMountManager == nullptr)
		return false;
	auto &mountedGames = g_gameMountManager->GetMountedGames();
	if(gameIdentifier.has_value()) {
		auto *game = g_gameMountManager->FindMountedGameByIdentifier(*gameIdentifier);
		if(game == nullptr)
			return false;
		game->FindFiles(fpath, files, dirs, keepAbsPaths);
	}
	else {
		for(auto &game : mountedGames)
			game->FindFiles(fpath, files, dirs, keepAbsPaths);
	}
	return true;
}

VFilePtr pragma::gamemount::load(const std::string &path, std::optional<std::string> *optOutSourcePath, const std::optional<std::string> &gameIdentifier)
{
	setup();
	initialize(true);

	if(gameIdentifier.has_value()) {
		auto *game = g_gameMountManager->FindMountedGameByIdentifier(*gameIdentifier);
		if(game == nullptr)
			return nullptr;
		return game->Load(path, optOutSourcePath);
	}
	for(auto &game : g_gameMountManager->GetMountedGames()) {
		auto f = game->Load(path, optOutSourcePath);
		if(f)
			return f;
	}
	return nullptr;
}

bool pragma::gamemount::load(const std::string &path, std::vector<uint8_t> &data)
{
	setup();
	initialize(true);

	for(auto &game : g_gameMountManager->GetMountedGames()) {
		if(game->Load(path, data))
			return true;
	}
	return false;
}

void pragma::gamemount::set_steam_root_paths(const std::vector<util::Path> &paths) { g_steamRootPaths = paths; }
