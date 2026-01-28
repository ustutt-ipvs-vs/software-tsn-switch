set(SOURCE_FILES 
	NestedAttrBuilder.cpp
	NetlinkSocket.cpp
	QdiscManager.cpp
	NetconfNetlinkMapper.cpp
        LldpDaemon.cpp
	LinkManager.cpp
	InterfacesCache.cpp
)

add_library(tsnctrld_lib STATIC ${SOURCE_FILES})

target_include_directories(tsnctrld_lib
	PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(
        tsnctrld_lib
        PRIVATE
        PkgConfig::LLDPCTL
)
