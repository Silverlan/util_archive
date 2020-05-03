/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __UTIL_ARCHIVE_HPP__
#define __UTIL_ARCHIVE_HPP__

#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <fsys/filesystem.h>

#ifdef ARCHIVELIB_STATIC
	#define DLLARCHLIB
#elif ARCHIVELIB_DLL
	#ifdef __linux__
		#define DLLARCHLIB __attribute__((visibility("default")))
	#else
		#define DLLARCHLIB  __declspec(dllexport)   // export DLL information
	#endif
#else
	#ifdef __linux__
		#define DLLARCHLIB
	#else
		#define DLLARCHLIB  __declspec(dllimport)   // import DLL information
	#endif
#endif

namespace uarch
{
	DLLARCHLIB VFilePtr load(const std::string &path,std::optional<std::string> *optOutSourcePath=nullptr);
	DLLARCHLIB bool load(const std::string &path,std::vector<uint8_t> &data);
	DLLARCHLIB void find_files(const std::string &path,std::vector<std::string> *files,std::vector<std::string> *dirs);
	DLLARCHLIB void set_verbose(bool verbose);
	DLLARCHLIB void close();

	struct GameMountInfo;
	DLLARCHLIB bool mount_game(const GameMountInfo &mountInfo);
	DLLARCHLIB void initialize();
};

#endif
