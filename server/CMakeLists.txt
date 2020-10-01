cmake_minimum_required(VERSION 3.13)
cmake_policy(VERSION 3.13)

include (GNUInstallDirs)

project(fty-srr
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

    INCLUDE_DIRS
        include
        
    USES_PRIVATE
        cxxtools
        fty_common
        fty_common_dto
        fty_common_logging
        fty_common_messagebus
        fty_common_mlm
        fty_lib_certificate
        protobuf
        pthread
)

#install files

configure_file(fty-srr.service.in
  "${PROJECT_BINARY_DIR}/fty-srr.service" @ONLY)

install(FILES
  "fty-srr.cfg"
  DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/fty-srr/
)

install(FILES
  "${PROJECT_BINARY_DIR}/fty-srr.service"
  DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/systemd/system/"
)

##############################################################################################################

