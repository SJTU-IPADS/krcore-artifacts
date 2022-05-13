install(FILES "rdma-persistent-naming.rules"
  RENAME "60-rdma-persistent-naming.rules"
  DESTINATION "${CMAKE_INSTALL_UDEV_RULESDIR}")

install(FILES "rdma-description.rules"
  RENAME "75-rdma-description.rules"
  DESTINATION "${CMAKE_INSTALL_UDEV_RULESDIR}")

install(FILES "rdma-umad.rules"
  RENAME "90-rdma-umad.rules"
  DESTINATION "${CMAKE_INSTALL_UDEV_RULESDIR}")

# This file is intended to be customized by the user, so it is installed in
# /etc/
install(FILES "persistent-ipoib.rules"
  RENAME "70-persistent-ipoib.rules"
  DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/udev/rules.d")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

# Create an installed executable (under /usr/lib/udev)
function(rdma_udev_executable EXEC)
  add_executable(${EXEC} ${ARGN})
  target_link_libraries(${EXEC} LINK_PRIVATE ${COMMON_LIBS})
  set_target_properties(${EXEC} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${BUILD_BIN}")
  install(TARGETS ${EXEC} DESTINATION "${CMAKE_INSTALL_UDEV_RULESDIR}/../")
endfunction()

if (NOT NL_KIND EQUAL 0)
  rdma_udev_executable(rdma_rename
    rdma_rename.c
  )

  target_link_libraries(rdma_rename LINK_PRIVATE
    ${NL_LIBRARIES}
  )
endif()
