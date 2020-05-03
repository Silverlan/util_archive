#ifndef __ARCHIVE_DATA_HPP__
#define __ARCHIVE_DATA_HPP__

#include <string>
#include <vector>
#include <memory>

namespace uarch
{
	struct ArchiveFileTable
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
		ArchiveFileTable(const std::shared_ptr<void> &phandle);
		std::shared_ptr<void> handle = nullptr;
		Item root = {"",true};
	};
};

#endif
