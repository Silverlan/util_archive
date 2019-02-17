/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "util_archive.hpp"
#include "hlarchive.hpp"
#include <vector>
#include <sharedutils/util.h>
#include <sharedutils/util_string.h>
#include <sharedutils/util_file.h>
#include <algorithm>
#include <fsys/filesystem.h>
#include <Wrapper.h>
#include <HLLib.h>
#include <iostream>
#include <array>
#include <unordered_set>
#include <thread>
#include <atomic>

#ifdef _WIN32
#define ENABLE_BETHESDA_FORMATS
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

namespace uarch
{
	void initialize(bool bWait);
};

static std::string normalize_path(const std::string &path)
{
	auto cpy = path;
	ustring::to_lower(cpy);
	cpy = FileManager::GetNormalizedPath(cpy);
	return cpy;
}

static std::string get_hl_path(const std::string &path)
{
	auto npath = path;
	auto bSound = (ustring::substr(npath,0,7) == "sounds\\") ? true : false;
	if(bSound == true)
		npath = "sound\\" +npath.substr(7);
	return npath;
}

static std::string get_beth_path(const std::string &path)
{
	auto npath = path;
	auto bSound = (ustring::substr(npath,0,7) == "sounds\\") ? true : false;
	if(bSound == true)
		npath = "sound\\" +npath.substr(7);
	else
	{
		auto bMaterial = (ustring::substr(npath,0,10) == "materials\\") ? true : false;
		if(bMaterial == true)
			npath = "textures\\" +npath.substr(10);
		else
		{
			auto bModel = (ustring::substr(npath,0,7) == "models\\") ? true : false;
			if(bModel == true)
				npath = npath.substr(7);
		}
	}
	return npath;
}

template<class THandle>
	struct ArchiveData
{
	struct Item
	{
		Item(const std::string &name,bool bDir);
		std::vector<Item> children;
		std::string name;
		void Add(const std::string &fpath,bool bDir);
		bool directory = false;
	private:
		void Add(const std::string *path,uint32_t dirCount,bool bDir);
	};
	ArchiveData(const THandle &phandle);
	THandle handle = nullptr;
	Item root = {"",true};
};
template<class THandle>
	ArchiveData<THandle>::Item::Item(const std::string &pname,bool pbDir)
		: name(pname),directory(pbDir)
{}
template<class THandle>
	void ArchiveData<THandle>::Item::Add(const std::string &fpath,bool bDir)
{
	std::vector<std::string> pathList {};
	ustring::explode(fpath,"\\",pathList);
	if(pathList.empty() == true)
		return;
	Add(pathList.data(),pathList.size() -1,bDir);
}
template<class THandle>
	void ArchiveData<THandle>::Item::Add(const std::string *path,uint32_t dirCount,bool bDir)
{
	auto it = std::find_if(children.begin(),children.end(),[path](const Item &item) {
		return (item.name == *path) ? true : false;
	});
	if(it == children.end())
	{
		children.push_back({*path,(dirCount > 0) ? true : bDir});
		it = children.end() -1;
	}
	if(dirCount == 0)
		return;
	it->Add(path +1,dirCount -1,bDir);
}
template<class THandle>
	ArchiveData<THandle>::ArchiveData::ArchiveData(const THandle &phandle)
		: handle(phandle)
{}

static bool s_bInitialized = false;
#ifdef ENABLE_BETHESDA_FORMATS
static std::vector<ArchiveData<bsa_handle>> s_bsaHandles {};
static std::vector<ArchiveData<std::shared_ptr<BA2>>> s_ba2Handles {};
#endif
static std::vector<ArchiveData<hl::PArchive>> s_hlArchives {};

static std::unique_ptr<std::thread> s_initThread = nullptr;
static std::atomic<bool> s_threadCancel = {false};

static std::vector<std::string> s_sourcePaths;
static void add_source_game_path(const std::string &path)
{
	auto cpath = FileManager::GetCanonicalizedPath(path);
	if(FileManager::IsSystemDir(cpath) == false)
		return;
	auto it = std::find(s_sourcePaths.begin(),s_sourcePaths.end(),cpath);
	if(it != s_sourcePaths.end())
		return;
	s_sourcePaths.push_back(cpath);
}

static bool get_custom_steam_game_paths(const std::string &steamPath,std::vector<std::string> &paths)
{
#if UARCH_VERBOSE == 1
	std::cout<<"[uarch] Looking for custom steam game paths in '"<<(steamPath +"/steamapps/libraryfolders.vdf")<<"'..."<<std::endl;
#endif
	auto f = FileManager::OpenSystemFile((steamPath +"/steamapps/libraryfolders.vdf").c_str(),"r");
#if UARCH_VERBOSE == 1
	if(f == nullptr)
		std::cout<<"[uarch] libraryfolders.vdf not found!"<<std::endl;
#endif
	if(f == nullptr)
		return false;
	while(f->Eof() == false)
	{
		auto l = f->ReadLine();
		ustring::remove_quotes(l);
		if(ustring::compare(l,"LibraryFolders",false) == true)
		{
			while(f->ReadChar() != '{')
			{
				if(f->Eof())
					return false;
			}
			auto content = f->ReadUntil("}");
			std::vector<std::string> lines;
			ustring::explode(content,"\n",lines);
			for(auto &l : lines)
			{
				ustring::remove_whitespace(l);
					
				std::vector<std::string> keyVal;
				ustring::explode_whitespace(l,keyVal);
				if(keyVal.size() < 2)
					continue;
				auto &k = keyVal.at(0);
				auto &v = keyVal.at(1);
				ustring::remove_quotes(k);
				ustring::remove_quotes(v);
				auto id = atoi(k.c_str());
				if(id == 0)
					continue;
#if UARCH_VERBOSE == 1
				std::cout<<"[uarch] Found custom path: '"<<v<<"'"<<std::endl;
#endif
				paths.push_back(v);
			}
			break;
		}
	}
	return true;
}

void uarch::initialize() {initialize(false);}
void uarch::initialize(bool bWait)
{
	if(s_bInitialized == true)
	{
		if(s_initThread != nullptr)
			s_initThread->join();
		s_initThread = nullptr;
		return;
	}
	s_bInitialized = true;

	s_initThread = std::make_unique<std::thread>([]() {
		hlInitialize();
#ifdef ENABLE_BETHESDA_FORMATS
		std::vector<std::string> bsaPaths {};
		std::vector<std::string> ba2Paths {};
#endif
		std::string rootSteamPath;
		if(util::get_registry_key_value(util::HKey::CurrentUser,"SOFTWARE\\Valve\\Steam","SteamPath",rootSteamPath) == true)
		{
#if UARCH_VERBOSE == 1
			std::cout<<"[uarch] Found root steam path: '"<<rootSteamPath<<"'"<<std::endl;
#endif
			std::vector<std::string> gamePaths = {rootSteamPath};
			get_custom_steam_game_paths(rootSteamPath,gamePaths);
			// Source Games
			struct GameInfo
			{
				GameInfo(const std::string &_path,const std::vector<std::string> &_vpks)
					: path(_path),vpks(_vpks)
				{}
				std::string path;
				std::vector<std::string> vpks;
			};
			std::vector<GameInfo> gameList = {
				{"half-life 2/ep2/",{"ep2_pak_dir.vpk"}},
				{"half-life 2/episodic/",{"ep1_pak_dir.vpk"}},
				{"half-life 2/hl2/",{"hl2_misc_dir.vpk","hl2_pak_dir.vpk","hl2_sound_misc_dir.vpk","hl2_sound_vo_english_dir.vpk","hl2_textures_dir.vpk"}},
				{"counter-strike source/cstrike/",{"cstrike_pak_dir.vpk"}},
				{"day of defeat source/dod/",{"dod_pak_dir.vpk"}},
				{"GarrysMod/garrysmod/",{"fallbacks_dir.vpk","garrysmod_dir.vpk"}},
				{"GarrysMod/sourceengine/",{"hl2_misc_dir.vpk","hl2_sound_misc_dir.vpk","hl2_sound_vo_english_dir.vpk","hl2_textures_dir.vpk"}},
				{"half-life 2/hl1/",{"hl1_pak_dir.vpk"}},
				{"half-life 2/hl1_hd/",{"hl1_hd_pak_dir.vpk"}},
				{"half-life 2/lostcoast/",{"lostcoast_pak_dir.vpk"}},
				{"half-life 2 deathmatch/hl2mp/",{"hl2mp_pak_dir.vpk"}},
				{"Half-Life 1 Source Deathmatch/hl1/",{"hl1_pak_dir.vpk"}},
				{"Half-Life 1 Source Deathmatch/hl1mp/",{"hl1mp_pak_dir.vpk"}},
				{"Half-Life 1 Source Deathmatch/hl2/",{"hl2_misc_dir.vpk","hl2_sound_misc_dir.vpk","hl2_sound_vo_english_dir.vpk","hl2_textures_dir.vpk",}},
				{"Half-Life 1 Source Deathmatch/platform/",{"platform_misc_dir.vpk"}},
				{"Portal/portal/",{"portal_pak_dir.vpk"}},
				{"team fortress 2/hl2/",{"hl2_misc_dir.vpk","hl2_sound_misc_dir.vpk","hl2_sound_vo_english_dir.vpk","hl2_textures_dir.vpk"}},
				{"team fortress 2/tf/",{"tf2_misc_dir.vpk","tf2_sound_misc_dir.vpk","tf2_sound_vo_english_dir.vpk","tf2_textures_dir.vpk"}},
				{"Dark Messiah Might and Magic Single Player/vpks/",{"depot_2101_dir.vpk","depot_2102_dir.vpk","depot_2103_dir.vpk","depot_2104_dir.vpk","depot_2105_dir.vpk","depot_2106_dir.vpk","depot_2107_dir.vpk","depot_2108_dir.vpk","depot_2109_dir.vpk"}},
				{"Dark Messiah Might and Magic Multi-Player/vpks/",{"depot_2131_dir.vpk","depot_2132_dir.vpk","depot_2133_dir.vpk","depot_2134_dir.vpk","depot_2135_dir.vpk","depot_2136_dir.vpk"}},
				{"SinEpisodes Emergence/vpks/",{"depot_1301_dir.vpk","depot_1302_dir.vpk","depot_1303_dir.vpk","depot_1304_dir.vpk","depot_1305_dir.vpk","depot_1308_dir.vpk"}},
				{"Black Mesa/bms/",{"bms_maps_dir.vpk","bms_materials_dir.vpk","bms_misc_dir.vpk","bms_models_dir.vpk","bms_sound_vo_english_dir.vpk","bms_sounds_misc_dir.vpk","bms_textures_dir.vpk"}}
			};

			// Determine game paths
			s_sourcePaths.reserve((gameList.size() +2) *gamePaths.size());
			for(auto &steamPath : gamePaths)
			{
				for(auto &gi : gameList)
				{
					add_source_game_path(steamPath +"/steamapps/common/" +gi.path);

					auto subPath = gi.path;
					subPath.pop_back();
					subPath = ufile::get_path_from_filename(subPath);
					add_source_game_path(steamPath +"/steamapps/common/" +subPath);
				}
				add_source_game_path(steamPath +"/steamapps/common/Half-Life 1 Source Deathmatch/hl1/");
				add_source_game_path(steamPath +"/steamapps/common/Half-Life 1 Source Deathmatch/hl2/");
			}

			// Determine gmod addon paths
			for(auto &steamPath : gamePaths)
			{
				std::string gmodAddonPath = steamPath +"/steamapps/common/GarrysMod/garrysmod/addons/";
				std::vector<std::string> addonDirs;
				FileManager::FindSystemFiles((gmodAddonPath +"*").c_str(),nullptr,&addonDirs);
				for(auto &d : addonDirs)
					add_source_game_path(gmodAddonPath +d +"/");
			}

			std::function<void(ArchiveData<hl::PArchive>::Item&,const hl::Archive::Directory&)> fTraverseArchive = nullptr;
			fTraverseArchive = [&fTraverseArchive](ArchiveData<hl::PArchive>::Item &archiveDir,const hl::Archive::Directory &dir) {
				std::vector<std::string> files;
				std::vector<hl::Archive::Directory> dirs;
				dir.GetItems(files,dirs);
				auto fConvertArchiveName = [](const std::string &f) {
					auto archFile = normalize_path(f);
					if(archFile.length() == 4 && archFile.substr(0,4) == "root")
						archFile = archFile.substr(4);
					if(archFile.length() > 4 && archFile.substr(0,5) == "root\\")
						archFile = archFile.substr(5);
					return archFile;
				};
				archiveDir.children.reserve(archiveDir.children.size() +files.size() +dirs.size());
				for(auto &f : files)
					archiveDir.children.push_back({fConvertArchiveName(f),false});
				for(auto &d : dirs)
				{
					archiveDir.children.push_back({fConvertArchiveName(d.GetPath()),true});
					fTraverseArchive(archiveDir.children.back(),d);
				}
			};
			std::unordered_map<std::string,bool> loadedVpks;
			for(auto &gi : gameList)
			{
				for(auto &steamPath : gamePaths)
				{
					auto path = steamPath +"/steamapps/common/" +gi.path;
					for(auto &vpk : gi.vpks)
					{
						if(s_threadCancel == true)
							break;
#if UARCH_VERBOSE == 1
						std::cout<<"[uarch] Mounting source VPK archive '"<<(path +vpk)<<"'..."<<std::endl;
#endif
						auto archive = hl::Archive::Create(path +vpk);
						if(archive == nullptr)
							continue;
						auto fname = ufile::get_file_from_filename(vpk);
						auto it = loadedVpks.find(fname);
						if(it != loadedVpks.end())
							continue;
						loadedVpks.insert(std::make_pair(fname,true));
						s_hlArchives.push_back(archive);
						fTraverseArchive(s_hlArchives.back().root,archive->GetRoot());
					}
				}
			}

#ifdef ENABLE_BETHESDA_FORMATS
			// Oblivion
			for(auto &bsa : {
				"DLCBattlehornCastle.bsa",
				"DLCFrostcrag.bsa",
				"DLCHorseArmor.bsa",
				"DLCOrrery.bsa",
				"DLCShiveringIsles - Meshes.bsa",
				"DLCShiveringIsles - Sounds.bsa",
				"DLCShiveringIsles - Textures.bsa",
				"DLCShiveringIsles - Voices.bsa",
				"DLCThievesDen.bsa",
				"DLCVileLair.bsa",
				"Knights.bsa",
				"Oblivion - Meshes.bsa",
				"Oblivion - Misc.bsa",
				"Oblivion - Sounds.bsa",
				"Oblivion - Textures - Compressed.bsa",
				"Oblivion - Voices1.bsa",
				"Oblivion - Voices2.bsa"
			})
			{
				for(auto &steamPath : gamePaths)
					bsaPaths.push_back(steamPath +"/steamapps/common/Oblivion/Data/" +bsa);
			}

			// Fallout 3
			// TODO

			// Fallout New Vegas
			for(auto &bsa : {
				"DeadMoney - Main.bsa",
				"DeadMoney - Sounds.bsa",
				"Fallout - Meshes.bsa",
				"Fallout - Misc.bsa",
				"Fallout - Sound.bsa",
				"Fallout - Textures.bsa",
				"Fallout - Textures2.bsa",
				"Fallout - Voices1.bsa",
				"GunRunnersArsenal - Main.bsa",
				"GunRunnersArsenal - Sounds.bsa",
				"HonestHearts - Main.bsa",
				"HonestHearts - Sounds.bsa",
				"LonesomeRoad - Main.bsa",
				"LonesomeRoad - Sounds.bsa",
				"OldWorldBlues - Main.bsa",
				"OldWorldBlues - Sounds.bsa",
				"Update.bsa"
			})
			{
				for(auto &steamPath : gamePaths)
					bsaPaths.push_back(steamPath +"/steamapps/common/Fallout New Vegas/Data/" +bsa);
			}

			// Fallout 4
			for(auto &ba2 : {
				"Fallout4 - Animations.ba2",
				"Fallout4 - Interface.ba2",
				"Fallout4 - Materials.ba2",
				"Fallout4 - Meshes.ba2",
				//"Fallout4 - MeshesExtra.ba2",
				"Fallout4 - Misc.ba2",
				"Fallout4 - Nvflex.ba2",
				"Fallout4 - Shaders.ba2",
				"Fallout4 - Sounds.ba2",
				"Fallout4 - Startup.ba2",
				"Fallout4 - Textures1.ba2",
				"Fallout4 - Textures2.ba2",
				"Fallout4 - Textures3.ba2",
				"Fallout4 - Textures4.ba2",
				"Fallout4 - Textures5.ba2",
				"Fallout4 - Textures6.ba2",
				"Fallout4 - Textures7.ba2",
				"Fallout4 - Textures8.ba2",
				"Fallout4 - Textures9.ba2",
				"Fallout4 - Voices.ba2"
			})
			{
				for(auto &steamPath : gamePaths)
					ba2Paths.push_back(steamPath +"/steamapps/common/Fallout 4/Data/" +ba2);
			}
#endif
		}

#ifdef ENABLE_BETHESDA_FORMATS
		s_bsaHandles.reserve(bsaPaths.size());
		for(auto &path : bsaPaths)
		{
			if(s_threadCancel == true)
				break;
			bsa_handle hBsa = nullptr;
#if UARCH_VERBOSE == 1
			std::cout<<"[uarch] Mounting BSA archive '"<<path<<"'..."<<std::endl;
#endif
			auto r = bsa_open(&hBsa,path.c_str());
			if(r != LIBBSA_OK)
				continue;
			s_bsaHandles.push_back(hBsa);
			auto &archData = s_bsaHandles.back();
			auto &assets = bsa_get_raw_assets(hBsa);
			for(auto &asset : assets)
				archData.root.Add(get_beth_path(normalize_path(asset.path)),false);
		}

		s_ba2Handles.reserve(ba2Paths.size());
		for(auto &path : ba2Paths)
		{
			if(s_threadCancel == true)
				break;
#if UARCH_VERBOSE == 1
			std::cout<<"[uarch] Mounting BA2 archive '"<<path<<"'..."<<std::endl;
#endif
			auto ba2 = std::make_shared<BA2>();
			try
			{
			if(ba2->Open(path.c_str()) == false)
				continue;
			}
			catch(const std::exception &e)
			{
#if UARCH_VERBOSE == 1
			std::cout<<"[uarch] Exception: '"<<e.what()<<"'!"<<std::endl;
			break;
#endif
			}
			s_ba2Handles.push_back(ba2);
			auto &archData = s_ba2Handles.back();
#if UARCH_VERBOSE == 1
			std::cout<<"[uarch] Adding archive paths...!"<<std::endl;
#endif
			for(auto &asset : ba2->nameTable)
				archData.root.Add(get_beth_path(normalize_path(asset)),false);
#if UARCH_VERBOSE == 1
			std::cout<<"[uarch] Done!"<<std::endl;
#endif
		}
#endif
#if UARCH_VERBOSE == 1
		std::cout<<"[uarch] Mounting complete!"<<std::endl;
#endif
	});
	if(bWait == true)
	{
		s_initThread->join();
		s_initThread = nullptr;
	}
}

void uarch::close()
{
	s_threadCancel = true;
	if(s_initThread != nullptr && s_initThread->joinable())
		s_initThread->join();
#ifdef ENABLE_BETHESDA_FORMATS
	for(auto &hBsa : s_bsaHandles)
		bsa_close(hBsa.handle);
	s_bsaHandles.clear();
#endif

	hlShutdown();
}

void uarch::find_files(const std::string &fpath,std::vector<std::string> *files,std::vector<std::string> *dirs)
{
	initialize(true);
	auto npath = normalize_path(fpath);
	const auto fSearchArchive = 
		[files,dirs,&npath](const auto &data,std::string(*fPathConv)(const std::string&)) {
		auto archPath = fPathConv(npath);
		auto path = ufile::get_path_from_filename(archPath);
		std::vector<std::string> pathList;
		ustring::explode(archPath,"\\",pathList);
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
							if(files != nullptr)
								files->push_back(child.name);
						}
						else
						{
							if(dirs != nullptr)
								dirs->push_back(child.name);
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
	fSearchArchive(s_hlArchives,get_hl_path);
#ifdef ENABLE_BETHESDA_FORMATS
	fSearchArchive(s_bsaHandles,get_beth_path);
	fSearchArchive(s_ba2Handles,get_beth_path);
#endif
}

VFilePtr uarch::load(const std::string &path)
{
	auto hlPath = get_hl_path(path);
	for(auto &p : s_sourcePaths)
	{
		auto f = FileManager::OpenSystemFile((p +hlPath).c_str(),"rb");
		if(f != nullptr)
			return f;
	}
	auto data = std::make_shared<std::vector<uint8_t>>();
	if(load(path,*data) == false)
		return nullptr;
	FileManager::AddVirtualFile(path,data);
	return FileManager::OpenFile(path.c_str(),"rb");
}

bool uarch::load(const std::string &path,std::vector<uint8_t> &data)
{
	auto npath = normalize_path(path);
	initialize(true);

	auto hlPath = get_hl_path(npath);
	for(auto &hlArch : s_hlArchives)
	{
		auto stream = hlArch.handle->OpenFile(hlPath);
		if(stream == nullptr)
			continue;
		if(stream->Read(data) == true)
			return true;
	}

#ifdef ENABLE_BETHESDA_FORMATS
	auto bsaPath = get_beth_path(npath);
	for(auto &hBsa : s_bsaHandles)
	{
		bool result;
		auto r = bsa_contains_asset(hBsa.handle,bsaPath.c_str(),&result);
		if(r != LIBBSA_OK || result == false)
			continue;
		const uint8_t *pdata = nullptr;
		std::size_t size = 0;
		r = bsa_extract_asset_to_memory(hBsa.handle,bsaPath.c_str(),&pdata,&size);
		if(r != LIBBSA_OK)
			continue;
		data.resize(size);
		memcpy(data.data(),pdata,size);
		return true;
	}
	for(auto &hBa2 : s_ba2Handles)
	{
		if(hBa2.handle == nullptr)
			continue;
		auto it = std::find_if(hBa2.handle->nameTable.begin(),hBa2.handle->nameTable.end(),[&npath](const std::string &other) {
			return (other == npath) ? true : false;
		});
		if(it == hBa2.handle->nameTable.end())
			continue;
		data.clear();
		if(hBa2.handle->Extract(it -hBa2.handle->nameTable.begin(),data) != 1)
			continue;
		return true;
	}
#endif
	return false;
}
