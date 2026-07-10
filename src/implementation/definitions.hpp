// SPDX-FileCopyrightText: (c) 2020 Silverlan <opensource@pragma-engine.com>
// SPDX-License-Identifier: MIT

#ifndef __UTIL_ARCHIVE_DEFINITIONS_HPP__
#define __UTIL_ARCHIVE_DEFINITIONS_HPP__

#ifdef ARCHIVELIB_STATIC
#define DLLARCHLIB
#elif ARCHIVELIB_DLL
#ifdef __linux__
#define DLLARCHLIB __attribute__((visibility("default")))
#else
#define DLLARCHLIB __declspec(dllexport) // export DLL information
#endif
#else
#ifdef __linux__
#define DLLARCHLIB
#else
#define DLLARCHLIB __declspec(dllimport) // import DLL information
#endif
#endif

#endif
