/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __HLARCHIVE_HPP__
#define __HLARCHIVE_HPP__

#include <string>
#include <memory>
#include <cinttypes>
#include <vector>
#include <limits>

namespace hl
{
	class Archive
		: public std::enable_shared_from_this<Archive>
	{
	public:
		class Stream
		{
		public:
			Stream(Archive &archive,void *item,void *stream);
			~Stream();
			bool Read(std::vector<uint8_t> &data) const;
			uint32_t GetSize() const;
		private:
			void *m_stream = nullptr;
			void *m_item = nullptr;
			std::shared_ptr<Archive> m_archive = nullptr;
		};
		class Directory
		{
		public:
			Directory(void *item,const std::string &path="");
			void GetItems(std::vector<std::string> &files,std::vector<Directory> &dirs) const;
			void GetFiles(std::vector<std::string> &files) const;
			void GetDirectories(std::vector<Directory> &dirs) const;
			const std::string &GetPath() const;
		private:
			void GetItems(std::vector<std::string> *files,std::vector<Directory> *dirs) const;
			void *m_item = nullptr;
			std::string m_path;
		};

		Archive();
		~Archive();
		static std::shared_ptr<Archive> Create(const std::string &path);
		std::shared_ptr<Stream> OpenFile(const std::string &fname);
		Directory GetRoot() const;
	private:
		uint32_t m_uiPackage = std::numeric_limits<uint32_t>::max();
		bool Bind();
	};
	using PArchive = std::shared_ptr<Archive>;
};

#endif
