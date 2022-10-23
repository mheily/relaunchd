configure_file(launchd.8 launchd.8 @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/launchd.8
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man8)

install(FILES launchctl.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
install(FILES launchd.plist.5 DESTINATION ${CMAKE_INSTALL_MANDIR}/man5)

