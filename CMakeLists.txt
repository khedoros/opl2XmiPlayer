cmake_minimum_required(VERSION 3.5...3.28)
project(opl2XmiPlayer LANGUAGES CXX)

# set(CMAKE_BUILD_TYPE Debug)
# add_compile_definitions(_GLIBCXX_DEBUG)
set(SOURCES playOplXmi.cpp midiEvent.cpp util.cpp uwPatch.cpp xmi.cpp yamahaYm3812.cpp)
set(HEADERS midiEvent.h opl.h uwPatch.h util.h xmi.h yamahaYm3812.h)
set(DROPLAY_SOURCES yamahaYm3812.cpp droPlay.cpp)
set(DROPLAY_HEADERS yamahaYm3812.h util.h oplStream.h)
add_executable(${PROJECT_NAME} ${SOURCES})
add_executable(droPlay ${DROPLAY_SOURCES})

target_sources(${PROJECT_NAME} PRIVATE ${SOURCES} ${HEADERS})
target_sources(droPlay PRIVATE ${DROPLAY_SOURCES} ${DROPLAY_HEADERS})


# find_package(SFML 2.5 COMPONENTS audio system REQUIRED)
# if(SFML_FOUND)
#  include_directories(${SFML_INCLUDE_DIR})
#  target_link_libraries(${PROJECT_NAME} sfml-audio sfml-system)
#  target_link_libraries(droPlay sfml-audio sfml-system)
# endif()

find_package(SDL2 REQUIRED)
if(SDL2_FOUND)
    include_directories(${SDL2_INCLUDE_DIR})
    target_link_libraries(droPlay SDL2)
    target_link_libraries(${PROJECT_NAME} SDL2)
endif()

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_compile_features(droPlay PRIVATE cxx_std_17)
