INCLUDE(TribitsLibraryMacros)

#
# A) Package-specific configuration options
#

#
# B) Define the header and source files (and directories)
#

SET(HEADERS "")
SET(SOURCES "")

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR})

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})

APPEND_SET(HEADERS
  MCLS_TpetraAdapter.hpp
  MCLS_TpetraBlockJacobiPreconditioner.hpp
  MCLS_TpetraBlockJacobiPreconditioner_impl.hpp
  MCLS_TpetraCrsMatrixAdapter.hpp
  MCLS_TpetraHelpers.hpp
  MCLS_TpetraParaSailsPreconditioner.hpp
  MCLS_TpetraParaSailsPreconditioner_impl.hpp
  MCLS_TpetraPointJacobiPreconditioner.hpp
  MCLS_TpetraPointJacobiPreconditioner_impl.hpp
  MCLS_TpetraThyraVectorExtraction.hpp
  MCLS_TpetraVectorAdapter.hpp
  MCLS_TpetraVectorExport.hpp
  ) 

APPEND_SET(SOURCES
  MCLS_TpetraDummy.cpp
  )

#
# C) Define the targets for package's library(s)
#

TRIBITS_ADD_LIBRARY(
  mclstpetra
  HEADERS ${HEADERS}
  SOURCES ${SOURCES}
  )

INSTALL(FILES 
  ${HEADERS} 
  DESTINATION 
  ${CMAKE_INSTALL_PREFIX}/include/
)
