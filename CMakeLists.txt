include(${CMAKE_SOURCE_DIR}/cmake/pr_common.cmake)

set(PROJ_NAME util_archive)
pr_add_library(${PROJ_NAME} SHARED)

pr_add_dependency(${PROJ_NAME} vfilesystem TARGET PUBLIC)

# hllib
add_subdirectory(third_party_libs/hllib)
pr_add_dependency(${PROJ_NAME} HLLib TARGET)

pr_init_module(${PROJ_NAME})

pr_add_compile_definitions(${PROJ_NAME} -DARCHIVELIB_DLL)

if(CONFIG_ENABLE_BETHESDA_FORMATS)
	pr_add_compile_definitions(${PROJ_NAME} -DENABLE_BETHESDA_FORMATS)
endif()

pr_finalize(${PROJ_NAME})
