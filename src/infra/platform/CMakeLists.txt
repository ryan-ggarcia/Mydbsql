add_library(mydb_platform STATIC UrlPolicy.cpp)
add_library(mydb::platform ALIAS mydb_platform)

if(WIN32)
    target_sources(mydb_platform PRIVATE
        WindowsPlatform.cpp
        DynamicLibraryWindows.cpp)
    target_compile_definitions(mydb_platform PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_link_libraries(mydb_platform PRIVATE psapi shell32 ole32)
elseif(APPLE)
    target_sources(mydb_platform PRIVATE
        MacPlatform.cpp
        PosixSupport.cpp
        DynamicLibraryPosix.cpp)
else()
    target_sources(mydb_platform PRIVATE
        LinuxPlatform.cpp
        PosixSupport.cpp
        DynamicLibraryPosix.cpp)
endif()

find_package(Threads REQUIRED)
target_link_libraries(mydb_platform
    PUBLIC mydb::core
    PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
mydb_set_warnings(mydb_platform)
