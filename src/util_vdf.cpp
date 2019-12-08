#include "util_vdf.hpp"
#include <sharedutils/util_markup_file.hpp>
#include <fsys/filesystem.h>

static util::MarkupFile::ResultCode read_vdf_block(util::MarkupFile &mf,vdf::DataBlock &block,bool bRoot=false)
{
	std::string blockName;
	util::MarkupFile::ResultCode r {};
	if((r=mf.ReadNextString(blockName)) != util::MarkupFile::ResultCode::Ok)
		return r;
	char token {};
	if((r=mf.ReadNextToken(token)) != util::MarkupFile::ResultCode::Ok)
		return r;
	if(token != '{')
		return util::MarkupFile::ResultCode::Error;
	mf.IncrementFilePos();
	for(;;)
	{
		if((r=mf.ReadNextToken(token)) != util::MarkupFile::ResultCode::Ok)
		{
			if(bRoot == true)
				return util::MarkupFile::ResultCode::Ok;
			return util::MarkupFile::ResultCode::Error; // Missing closing bracket '}'
		}
		if(token == '}')
		{
			mf.GetDataStream()->SetOffset(mf.GetDataStream()->GetOffset() +1u);
			return util::MarkupFile::ResultCode::Ok;
		}

		std::string key,val;
		if((r=mf.ReadNextString(key)) != util::MarkupFile::ResultCode::Ok || (r=mf.ReadNextString(val)) != util::MarkupFile::ResultCode::Ok)
			return util::MarkupFile::ResultCode::Error;
		block.keyValues[key] = val;
	}
	return util::MarkupFile::ResultCode::Error;
}

bool vdf::get_external_steam_locations(const std::string &steamRootPath,std::vector<std::string> &outExtLocations)
{
	auto f = FileManager::OpenSystemFile((steamRootPath +"/steamapps/libraryfolders.vdf").c_str(),"r");
	if(f == nullptr)
		return false;
	auto lenContents = f->GetSize();

	DataStream dsContents {static_cast<uint32_t>(lenContents)};
	f->Read(dsContents->GetData(),lenContents);

	util::MarkupFile mf {dsContents};
	auto vdfData = std::make_shared<vdf::Data>();
	auto r = read_vdf_block(mf,vdfData->dataBlock,true);
	if(r != util::MarkupFile::ResultCode::Ok)
		return false;
	for(uint8_t i=1;i<=8;++i) // 8 is supposedly the max number of external locations you can specify in steam
	{
		auto it = vdfData->dataBlock.keyValues.find(std::to_string(i));
		if(it != vdfData->dataBlock.keyValues.end())
		{
			auto path = it->second;
			ustring::replace(path,"\\\\","/");
			if(path.empty() == false && path.back() == '/')
				path.pop_back();
			outExtLocations.push_back(path);
		}
	}
	return true;
}
