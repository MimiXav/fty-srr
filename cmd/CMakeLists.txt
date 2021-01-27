cmake_minimum_required(VERSION 3.13)
cmake_policy(VERSION 3.13)

##############################################################################################################

file(GLOB_RECURSE SOURCES_FILES src/*.cc)

etn_target(exe ${PROJECT_NAME}-cmd
    SOURCES
        ${SOURCES_FILES}
        
    USES_PRIVATE
        cxxtools
        fty_common_dto
        fty_common_logging
        fty_common_messagebus
        fty_common_mlm
        fty-utils
        protobuf
)

##############################################################################################################

