set(_q "")
if(EXPAT_FIND_QUIETLY)
    set(_q QUIET)
endif()
find_package(EXPAT ${EXPAT_FIND_VERSION} CONFIG ${_q})

if(NOT EXPAT_FIND_QUIETLY)
    if (NOT EXPAT_FOUND)
        message(STATUS "Falling back to MODULE search for EXPAT...")
    else()
        message(STATUS "EXPAT found in ${EXPAT_DIR}")
    endif()
endif()

if (NOT EXPAT_FOUND)
    set(_modpath ${CMAKE_MODULE_PATH})
    set(CMAKE_MODULE_PATH "")
    include(FindEXPAT)
    set(CMAKE_MODULE_PATH ${_modpath})

    if (NOT TARGET EXPAT::EXPAT)
        add_library(EXPAT::EXPAT INTERFACE)
        target_link_libraries(EXPAT::EXPAT INTERFACE ${EXPAT_LIBRARIES})
        target_include_directories(EXPAT::EXPAT INTERFACE ${EXPAT_INCLUDE_DIRS})
    endif ()
endif()

if (EXPAT_FOUND AND NOT TARGET EXPAT::EXPAT)
    add_library(libexpat INTERFACE)
    add_library(EXPAT::EXPAT ALIAS libexpat)
    target_link_libraries(libexpat INTERFACE expat::expat)
    if (NOT EXPAT_LIBRARIES)
        set(EXPAT_LIBRARIES expat::expat CACHE STRING "")
    endif ()
endif ()

