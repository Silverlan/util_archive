#ifndef __UTIL_VDF_HPP__
#define __UTIL_VDF_HPP__

#include <unordered_map>
#include <vector>
#include <string>

namespace vdf
{
	class DataBlock
	{
	public:
		DataBlock() {}
		std::unordered_map<std::string,std::string> keyValues;
	};

	class Data
	{
	public:
		Data() {}
		DataBlock dataBlock = {};
	};

	bool get_external_steam_locations(const std::string &steamRootPath,std::vector<std::string> &outExtLocations);
};

#endif
