cmake_minimum_required(VERSION 3.13)
cmake_policy(VERSION 3.13)

project(fty-srr-cmd
    VERSION 1.0.0
)

##############################################################################################################
find_package(fty-cmake PATHS ${CMAKE_BINARY_DIR}/fty-cmake)
##############################################################################################################


##############################################################################################################

file(GLOB_RECURSE SOURCES_FILES src/*.cc)

etn_target(exe ${PROJECT_NAME}
    SOURCES
        ${SOURCES_FILES}
        
    USES_PRIVATE
        cxxtools
        fty_common_dto
        fty_common_logging
        fty_common_messagebus
        fty_common_mlm
        protobuf
)

##############################################################################################################

