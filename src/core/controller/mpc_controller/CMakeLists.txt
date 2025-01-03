cmake_minimum_required(VERSION 3.0.2)
project(mpc_controller)

get_filename_component(PROJECT_ROOT_DIR ${CMAKE_SOURCE_DIR} DIRECTORY)
include(${PROJECT_ROOT_DIR}/3rd/conanbuildinfo.cmake)
conan_basic_setup()

find_package(catkin REQUIRED COMPONENTS
  angles
  costmap_2d
  geometry_msgs
  nav_core
  nav_msgs
  navfn
  pluginlib
  roscpp
  tf2_geometry_msgs
  tf2_ros
  base_local_planner
  controller
)

find_package(Eigen3 REQUIRED)

catkin_package(
  INCLUDE_DIRS include
  CATKIN_DEPENDS controller
)

include_directories(
  include
  ${catkin_INCLUDE_DIRS}
)

add_library(${PROJECT_NAME}
  src/mpc_controller.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
  ${CONAN_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME}
  ${catkin_LIBRARIES}
  ${CONAN_LIBS}
)

# add custom target to copy libmpc_controller.so from build to devel dir.
add_custom_command(
  TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> ${CATKIN_DEVEL_PREFIX}/lib/
)