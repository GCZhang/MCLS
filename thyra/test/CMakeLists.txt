INCLUDE(AddSubdirectories)

#ADD_SUBDIRECTORIES(LOWSFactoryTpetra)

ASSERT_DEFINED(${PACKAGE_NAME}_ENABLE_EpetraExt)
ASSERT_DEFINED(${PACKAGE_NAME}_ENABLE_Epetra)
IF (${PACKAGE_NAME}_ENABLE_Epetra)
  IF (${PACKAGE_NAME}_ENABLE_EpetraExt)
    ADD_SUBDIRECTORIES(LOWSFactoryEpetra)
  ENDIF()
ENDIF()
