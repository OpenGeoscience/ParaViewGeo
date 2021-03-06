PROJECT(VTKPythonWrapping)

IF(NOT VTKPythonWrapping_INSTALL_LIBRARIES)
  SET(VTKPythonWrapping_INSTALL_LIBRARIES 1)
ENDIF(NOT VTKPythonWrapping_INSTALL_LIBRARIES)
IF(NOT VTKPythonWrapping_INSTALL_LIB_DIR)
  SET(VTKPythonWrapping_INSTALL_LIB_DIR "/lib/${PROJECT_NAME}")
ENDIF(NOT VTKPythonWrapping_INSTALL_LIB_DIR)
IF(NOT VTKPythonWrapping_INSTALL_BIN_DIR)
  SET(VTKPythonWrapping_INSTALL_BIN_DIR "/bin")
ENDIF(NOT VTKPythonWrapping_INSTALL_BIN_DIR)

# We only wrap subportion of VTK, so include only that
INCLUDE_DIRECTORIES(
  ${VTK_INCLUDE_DIR}
  ${PYTHON_INCLUDE_PATH}
  )

# OpenGL include directories.
IF(APPLE)
  IF(VTK_USE_X)
    SET(PVWRAP_OPEN_GL_DIRS ${PVWRAP_OPEN_GL_DIRS}
      ${OPENGL_INCLUDE_DIR})
  ENDIF(VTK_USE_X)
ELSE(APPLE)
  SET(PVWRAP_OPEN_GL_DIRS ${PVWRAP_OPEN_GL_DIRS}
    ${OPENGL_INCLUDE_DIR})
ENDIF(APPLE)

IF(VTK_USE_X)
  # X include directories.
  SET(PVWRAP_OPEN_GL_DIRS ${PVWRAP_OPEN_GL_DIRS}
    ${CMAKE_Xlib_INCLUDE_PATH} ${CMAKE_Xutil_INCLUDE_PATH})
ENDIF(VTK_USE_X)

IF(VTK_HAVE_VP1000)
  # VolumePro VP 1000 include directory.
  SET(PVWRAP_OPEN_GL_DIRS ${PVWRAP_OPEN_GL_DIRS}
    ${VLI_INCLUDE_PATH_FOR_VP1000})
ENDIF(VTK_HAVE_VP1000)

IF(VTK_USE_MANGLED_MESA)
  # Mangled Mesa include directory.
  SET(PVWRAP_OPEN_GL_DIRS ${PVWRAP_OPEN_GL_DIRS}
    ${MESA_INCLUDE_PATH})
ELSE(VTK_USE_MANGLED_MESA)
  # Off-Screen Mesa include directory.
  IF(VTK_OPENGL_HAS_OSMESA)
    IF(OSMESA_INCLUDE_DIR)
      SET(PVWRAP_OPEN_GL_DIRS ${PVWRAP_OPEN_GL_DIRS}
        ${OSMESA_INCLUDE_DIR})
    ENDIF(OSMESA_INCLUDE_DIR)
  ENDIF(VTK_OPENGL_HAS_OSMESA)
ENDIF(VTK_USE_MANGLED_MESA)

INCLUDE_DIRECTORIES(${PVWRAP_OPEN_GL_DIRS})
SET(CMAKE_C_FLAGS "${CMAKE_ANSI_CFLAGS} ${CMAKE_C_FLAGS}")

IF (PARAVIEW_ENABLE_PYTHON)
  SET(PV_PYTHON_MODULE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/paraview")
  SET(PV_PYTHON_MODULE_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/site-packages/paraview")
  SET(PV_PYTHON_MODULE_DIR "${CMAKE_CURRENT_BINARY_DIR}" CACHE INTERNAL "Path to paraview module.")
  MAKE_DIRECTORY(${PV_PYTHON_MODULE_BINARY_DIR})
  # Handle out-of-source builds correctly.
  #
  #  1. Create a list of Python files to be installed/copied.
  #  2. Copy them to the binary directory. Since paraview cannot be built
  #     in place, we must always copy the files to the binary directory.
  #  3. Use Python's compileall to compile the copied files.
  #
  # *** Step 1 has to be done carefully to avoid missing out files ***


  # List all python source files.
  # All files paraview/*.py included in the paraview module.
  SET(PV_PYTHON_FILES
    demos/demo1
    demos/show_grid_as_background
    vtk/__init__
    vtk/genericfiltering
    vtk/geovis
    vtk/hybrid
    vtk/imaging
    vtk/infovis
    vtk/io
    vtk/parallel
    vtk/views
    vtk/volumerendering
    vtk/widgets
    vtk/algorithms
    vtk/dataset_adapter
    servermanager
    __init__
    numeric
    util
    variant
    benchmark
    simple
    smtrace
    smstate
    pvfilters
    pvvtkextensions
    )

  # Now copy these files if necessary.
  SET(PV_PYTHON_SOURCE_FILES)
  SET(PV_PYTHON_OUTPUT_FILES)
  FOREACH(file ${PV_PYTHON_FILES})
    SET(src "${PV_PYTHON_MODULE_SOURCE_DIR}/${file}.py")
    SET(PV_PYTHON_SOURCE_FILES ${PV_PYTHON_SOURCE_FILES} ${src})
  ENDFOREACH(file)

  # All VTK util py included in the paraview module.
  SET(PV_VTK_PYTHON_FILES
    numpy_support
    vtkConstants
    )
  FOREACH(file ${PV_VTK_PYTHON_FILES})
    SET(src "${VTK_SOURCE_DIR}/Wrapping/Python/vtk/util/${file}.py")
    SET(PV_PYTHON_SOURCE_FILES ${PV_PYTHON_SOURCE_FILES} ${src})
  ENDFOREACH(file)

  IF ("${VTK_BINARY_DIR}" MATCHES "^${VTK_SOURCE_DIR}$")
    #MESSAGE("In source build -- no need to copy Python files.")
  ELSE ("${VTK_BINARY_DIR}" MATCHES "^${VTK_SOURCE_DIR}$")
    FOREACH(file ${PV_PYTHON_FILES})
      SET(src "${PV_PYTHON_MODULE_SOURCE_DIR}/${file}.py")
      SET(tgt "${PV_PYTHON_MODULE_BINARY_DIR}/${file}.py")
      ADD_CUSTOM_COMMAND(DEPENDS ${src}
        COMMAND ${CMAKE_COMMAND}
        ARGS -E copy ${src} ${tgt}
        OUTPUT ${tgt}
        COMMENT "source copy")
      SET(PV_PYTHON_OUTPUT_FILES ${PV_PYTHON_OUTPUT_FILES} ${tgt})
    ENDFOREACH(file)
    FOREACH(file ${PV_VTK_PYTHON_FILES})
      SET(src "${VTK_SOURCE_DIR}/Wrapping/Python/vtk/util/${file}.py")
      SET(tgt "${PV_PYTHON_MODULE_BINARY_DIR}/${file}.py")
      ADD_CUSTOM_COMMAND(DEPENDS ${src}
        COMMAND ${CMAKE_COMMAND}
        ARGS -E copy ${src} ${tgt}
        OUTPUT ${tgt}
        COMMENT "source copy")
      SET(PV_PYTHON_OUTPUT_FILES ${PV_PYTHON_OUTPUT_FILES} ${tgt})
    ENDFOREACH(file)
  ENDIF ("${VTK_BINARY_DIR}" MATCHES "^${VTK_SOURCE_DIR}$")

  # Byte compile the Python files.
  CONFIGURE_FILE(${PV_PYTHON_MODULE_SOURCE_DIR}/compile_all_pv.py.in
    ${PV_PYTHON_MODULE_BINARY_DIR}/compile_all_pv.py
    @ONLY IMMEDIATE)

  ADD_CUSTOM_COMMAND(
    WORKING_DIRECTORY ${PV_PYTHON_MODULE_BINARY_DIR}
    COMMAND ${PYTHON_EXECUTABLE}
    ARGS compile_all_pv.py
    DEPENDS ${PV_PYTHON_SOURCE_FILES} ${PV_PYTHON_MODULE_BINARY_DIR}/compile_all_pv.py
    ${PV_PYTHON_OUTPUT_FILES}
    OUTPUT "${PV_PYTHON_MODULE_BINARY_DIR}/pv_compile_complete"
    )

  ADD_CUSTOM_TARGET(paraview_pyc ALL 
    DEPENDS "${PV_PYTHON_MODULE_BINARY_DIR}/pv_compile_complete")

  # Install the paraview module files.
  IF (NOT PV_INSTALL_NO_LIBRARIES)
    INSTALL(DIRECTORY ${PV_PYTHON_MODULE_BINARY_DIR} 
        DESTINATION ${PV_INSTALL_LIB_DIR}/site-packages
        COMPONENT Runtime
        )
  ENDIF (NOT PV_INSTALL_NO_LIBRARIES)
  
  # Create the setup.py file.
  IF(CMAKE_CONFIGURATION_TYPES)
    # The build has multiple configuration types.  If CMAKE_BUILD_TYPE
    # is set use it as the default BUILD_TYPE for setup.py to install.
    SET(VTK_PYTHON_HAS_CONFIG_TYPES 1)
    IF(CMAKE_BUILD_TYPE)
      SET(VTK_PYTHON_BUILD_TYPE "\"${CMAKE_BUILD_TYPE}\"")
    ELSE(CMAKE_BUILD_TYPE)
      SET(VTK_PYTHON_BUILD_TYPE "[]")
    ENDIF(CMAKE_BUILD_TYPE)
  ELSE(CMAKE_CONFIGURATION_TYPES)
    # The build has one configuration type.  The build type does not
    # affect installation.
    SET(VTK_PYTHON_HAS_CONFIG_TYPES 0)
    SET(VTK_PYTHON_BUILD_TYPE "[]")
  ENDIF(CMAKE_CONFIGURATION_TYPES)
  IF(VTK_USE_RENDERING)
    SET(VTK_PYTHON_USE_RENDERING 1)
  ELSE(VTK_USE_RENDERING)
    SET(VTK_PYTHON_USE_RENDERING 0)
  ENDIF(VTK_USE_RENDERING)
  IF(VTK_USE_PARALLEL)
    SET(VTK_PYTHON_USE_PARALLEL 1)
  ELSE(VTK_USE_PARALLEL)
    SET(VTK_PYTHON_USE_PARALLEL 0)
  ENDIF(VTK_USE_PARALLEL)
  IF(VTK_USE_CHARTS)
    SET(VTK_PYTHON_USE_CHARTS 1)
  ELSE(VTK_USE_CHARTS)
    SET(VTK_PYTHON_USE_CHARTS 0)
  ENDIF(VTK_USE_CHARTS)
  IF(VTK_USE_GEOVIS)
    SET(VTK_PYTHON_USE_GEOVIS 1)
  ELSE(VTK_USE_GEOVIS)
    SET(VTK_PYTHON_USE_GEOVIS 0)
  ENDIF(VTK_USE_GEOVIS)
  IF(VTK_USE_INFOVIS)
    SET(VTK_PYTHON_USE_INFOVIS 1)
  ELSE(VTK_USE_INFOVIS)
    SET(VTK_PYTHON_USE_INFOVIS 0)
  ENDIF(VTK_USE_INFOVIS)
  IF(VTK_USE_TEXT_ANALYSIS)
    SET(VTK_PYTHON_USE_TEXT_ANALYSIS 1)
  ELSE(VTK_USE_TEXT_ANALYSIS)
    SET(VTK_PYTHON_USE_TEXT_ANALYSIS 0)
  ENDIF(VTK_USE_TEXT_ANALYSIS)
  IF(VTK_USE_VIEWS)
    SET(VTK_PYTHON_USE_VIEWS 1)
  ELSE(VTK_USE_VIEWS)
    SET(VTK_PYTHON_USE_VIEWS 0)
  ENDIF(VTK_USE_VIEWS)
  IF(VTK_USE_MPI)
    SET(VTK_PYTHON_USE_MPI 1)
  ELSE(VTK_USE_MPI)
    SET(VTK_PYTHON_USE_MPI 0)
  ENDIF(VTK_USE_MPI)
  IF(VTK_USE_QT AND VTK_WRAP_PYTHON_SIP)
    SET(VTK_PYTHON_USE_QT 1)
  ELSE(VTK_USE_QT AND VTK_WRAP_PYTHON_SIP)
    SET(VTK_PYTHON_USE_QT 0)
  ENDIF(VTK_USE_QT AND VTK_WRAP_PYTHON_SIP)
  IF(VTK_WRAP_PYTHON_SIP)
    SET(VTK_PYTHON_USE_SIP 1)
  ELSE(VTK_WRAP_PYTHON_SIP)
    SET(VTK_PYTHON_USE_SIP 0)
  ENDIF(VTK_WRAP_PYTHON_SIP)
  
  CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/setup.py.in
               ${CMAKE_CURRENT_BINARY_DIR}/setup.py @ONLY IMMEDIATE)
ENDIF (PARAVIEW_ENABLE_PYTHON)
