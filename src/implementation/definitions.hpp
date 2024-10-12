/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __UTIL_ARCHIVE_DEFINITIONS_HPP__
#define __UTIL_ARCHIVE_DEFINITIONS_HPP__

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

#endif
