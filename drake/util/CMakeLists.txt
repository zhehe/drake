if(simulink_FOUND)
  add_mex(realtime realtime.cpp)
endif()

add_mex(barycentricInterpolation barycentricInterpolation.cpp)

if(lcm_FOUND AND MATLAB_FOUND)
  if(simulink_FOUND)
    add_mex(lcmLogger lcmLogger.cpp)
    target_link_libraries(lcmLogger lcm)
    if(WIN32)
      target_link_libraries(lcmLogger ws2_32)
    endif()
  endif()

  add_mex(publishLCMLog publishLCMLog.cpp)
  target_link_libraries(publishLCMLog lcm)
endif()

add_library_with_exports(LIB_NAME drakeUtil SOURCE_FILES drakeUtil.cpp)
target_link_libraries(drakeUtil drakeCommon)
drake_install_headers(drakeUtil.h)
pods_install_libraries(drakeUtil)
pods_install_pkg_config_file(drake-util
  LIBS -ldrakeUtil
  REQUIRES
  VERSION 0.0.1)

drake_install_headers(drakeAppUtil.h)

if(MATLAB_FOUND)
  add_mex(drakeMexUtil SHARED drakeMexUtil.cpp MexWrapper.cpp)
  target_link_libraries(drakeMexUtil drakeCommon)
  # TODO: use this again once I can assume everyone has CMAKE version >= 2.8.8
  # add_mex(drakeUtil OBJECT drakeUtil.cpp)

  drake_install_headers(drakeMexUtil.h MexWrapper.h)
  pods_install_libraries(drakeMexUtil)
  pods_install_pkg_config_file(drake-mex-util
    LIBS -ldrakeMexUtil
    REQUIRES
    VERSION 0.0.1)
endif()

if(lcm_FOUND)
  # note: requires drakeUtil header file, but not linking against it because
  # we only need methods that are templated and defined in the header

  add_library_with_exports(LIB_NAME drakeLCMUtil SOURCE_FILES lcmUtil.cpp)
  add_dependencies(drakeLCMUtil drake_lcmtypes_hpp)
  target_link_libraries(drakeLCMUtil drakeTrajectories)
  drake_install_headers(lcmUtil.h)
  pods_install_libraries(drakeLCMUtil)
  pods_install_pkg_config_file(drake-lcm-util
    LIBS -ldrakeLCMUtil
    REQUIRES
    VERSION 0.0.1)
endif()

drake_install_headers(drakeGradientUtil.h)

add_library_with_exports(LIB_NAME drakeGeometryUtil SOURCE_FILES drakeGeometryUtil.cpp)
target_link_libraries(drakeGeometryUtil drakeCommon drakeMath)
pods_install_libraries(drakeGeometryUtil)
drake_install_headers(drakeGeometryUtil.h)
pods_install_pkg_config_file(drake-geometry-util
  LIBS -ldrakeGeometryUtil -ldrakeCommon
  REQUIRES
  VERSION 0.0.1)

add_library_with_exports(LIB_NAME drakeConvexHull SOURCE_FILES convexHull.cpp)
pods_install_libraries(drakeConvexHull)
drake_install_headers(convexHull.h)
pods_install_pkg_config_file(drake-convex-hull
  LIBS -ldrakeConvexHull
  REQUIRES
  VERSION 0.0.1)

if(MATLAB_FOUND)
  add_mex(cylindrical2cartesianmex cylindrical2cartesianmex.cpp)
  target_link_libraries(cylindrical2cartesianmex drakeMath)
  add_mex(cartesian2cylindricalmex cartesian2cylindricalmex.cpp)
  target_link_libraries(cartesian2cylindricalmex drakeMath)

  add_mex(quat2expmapmex quat2expmapmex.cpp)
  target_link_libraries(quat2expmapmex drakeMexUtil drakeGeometryUtil)

  add_mex(expmap2quatImplmex expmap2quatImplmex.cpp)
  target_link_libraries(expmap2quatImplmex drakeMexUtil drakeGeometryUtil)

  add_mex(flipExpmapmex flipExpmapmex.cpp)
  target_link_libraries(flipExpmapmex drakeMexUtil drakeGeometryUtil)

  add_mex(unwrapExpmapmex unwrapExpmapmex.cpp)
  target_link_libraries(unwrapExpmapmex drakeMexUtil drakeGeometryUtil)

  add_mex(quat2expmapSequencemex quat2expmapSequencemex.cpp)
  target_link_libraries(quat2expmapSequencemex drakeMexUtil drakeGeometryUtil)

  add_mex(closestExpmapmex closestExpmapmex.cpp)
  target_link_libraries(closestExpmapmex drakeMexUtil drakeGeometryUtil)

  add_mex(resolveCenterOfPressuremex resolveCenterOfPressuremex.cpp)
  target_link_libraries(resolveCenterOfPressuremex drakeMexUtil drakeUtil)
endif()

add_subdirectory(test)

# Mex debugging tools:
if(0)  # NOT WIN32
  add_mex(drake_debug_mex EXECUTABLE drakeDebugMex.cpp)
  target_link_libraries(drake_debug_mex drakeMexUtil -ldl)

  message(STATUS "Writing drake_debug_mex.sh")
  file(WRITE ${CMAKE_BINARY_DIR}/bin/drake_debug_mex.sh
    "#!/bin/bash\n"
    "\n"
    "# Usage:\n"
    "#   % drake_debug_mex.sh [args]\n"
    "# will set up the environment and then run:\n"
    "#   % args drake-debug-mex\n"
    "#\n"
    "# For example,\n"
    "#   % drake_debug_mex.sh\n"
    "# will simply run the executable,\n"
    "#   % drake_debug_mex.sh gdb\n"
    "# will run gdb on drake-debug-mex, and\n"
    "#   % drake_debug_mex.sh valgrind --leak-check=full --dsymutil=yes --track-origins=yes --xml=yes\n"
    "# will run valgrind with the appropriate arguments passed in.\n"
    "#\n"
    "# It's not pretty, but seems to work for the use\n"
    "# cases I've imagined so far. - RussT\n"
    "\n")

  if(APPLE)
    file(APPEND ${CMAKE_BINARY_DIR}/bin/drake_debug_mex.sh
      "export DYLD_LIBRARY_PATH=$DYLD_LIBRARY_PATH:${MATLAB_ROOT}/bin/${MATLAB_CPU}\n"
      "export DYLD_FORCE_FLAT_NAMESPACE=1\n"
      "export DYLD_INSERT_LIBRARIES=${CMAKE_BINARY_DIR}/lib/libdebugMex.dylib\n")
  else()
    file(APPEND ${CMAKE_BINARY_DIR}/bin/drake_debug_mex.sh
      "export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${MATLAB_ROOT}/bin/${MATLAB_CPU}\n"
      "export LD_PRELOAD=${CMAKE_BINARY_DIR}/lib/libdebugMex.so\n")
  endif()

  file(APPEND ${CMAKE_BINARY_DIR}/bin/drake_debug_mex.sh
    "\n"
    "\"\$@\" ${CMAKE_BINARY_DIR}/bin/drake_debug_mex\n")

  install(FILES ${CMAKE_BINARY_DIR}/bin/drake_debug_mex.sh
    DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
    PERMISSIONS OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()  # NOT WIN32

add_subdirectory(yaml)
add_subdirectory(geometry/test)
