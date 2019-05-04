/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "util_archive.hpp"
#include <fsys/filesystem.h>

#ifdef TEST_EXE

#include <iostream>
#include <chrono>

int main(int argc,char *argv[])
{
	std::size_t size = 0;
	auto data = std::make_shared<std::vector<uint8_t>>();
	//auto r = bsa::load("meshes\\creatures\\dog\\ine.nif",data);
	//"models\\props_c17\\awning001a.mdl"

	std::vector<std::string> files;
	std::vector<std::string> dirs;
	uarch::initialize();
	auto t0 = std::chrono::high_resolution_clock::now();
	uarch::find_files("sounds/songs/*",files,dirs);
	auto t1 = std::chrono::high_resolution_clock::now();
	auto tDelta = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 -t0).count();
	std::cout<<"Time passed: "<<(tDelta /1'000'000'000.0)<<std::endl;
	for(auto &f : files)
		std::cout<<"Found file: "<<f<<std::endl;
	for(auto &d : dirs)
		std::cout<<"Found dir: "<<d<<std::endl;

	auto r = uarch::load("models\\props_c17\\awning001a.mdl",*data);
	if(r == true)
	{
		std::cout<<"Data: "<<reinterpret_cast<char*>(data->data())<<std::endl;
	}

	r = uarch::load("Meshes\\Landscape\\Plants\\Marshberry02.nif",*data);
	if(r == true)
	{
		FileManager::AddVirtualFile("Meshes\\Landscape\\Plants\\Marshberry02.nif",data);//reinterpret_cast<char*>(data.data()),data.size());
		auto f = FileManager::OpenFile("Meshes\\Landscape\\Plants\\Marshberry02.nif","rb");
		if(f != nullptr)
		{
			std::cout<<"!!"<<std::endl;
		}
		/*auto *cpy = new uint8_t[data.size()];
		memcpy(cpy,data.data(),data.size());
		FileManager::AddVirtualFile("Meshes\\Landscape\\Plants\\Marshberry02.nif",reinterpret_cast<char*>(cpy),data.size());//reinterpret_cast<char*>(data.data()),data.size());
		auto f = FileManager::OpenFile("Meshes\\Landscape\\Plants\\Marshberry02.nif","rb");
		if(f != nullptr)
		{
			auto str = f->ReadString();
			std::cout<<"Str: "<<str<<std::endl;
			std::cout<<"Success!"<<std::endl;
		}*/
		for(;;);
	}
	//auto *ptr = data.get();
	//std::cout<<"Data: "<<static_cast<const void*>(ptr)<<std::endl;
	for(;;);
	return EXIT_SUCCESS;
}

#endif
