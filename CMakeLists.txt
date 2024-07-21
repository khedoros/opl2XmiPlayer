cmake_minimum_required(VERSION 3.5...3.28)
project(opl2XmiPlayer LANGUAGES CXX)

set(SOURCES play_opl_xmi.cpp midi_event.cpp util.cpp uw_patch.cpp xmi.cpp yamahaYm3812.cpp)
set(HEADERS midi_event.h opl.h uw_patch.h util.h xmi.h yamahaYm3812.h)
add_executable(${PROJECT_NAME} ${SOURCES})

target_sources(${PROJECT_NAME} PRIVATE ${SOURCES} ${HEADERS})

find_package(SFML 2.5 COMPONENTS audio system REQUIRED)
if(SFML_FOUND)
  include_directories(${SFML_INCLUDE_DIR})
  target_link_libraries(${PROJECT_NAME} sfml-audio sfml-system)
endif()

target_link_libraries(${PROJECT_NAME} sfml-audio sfml-system)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
