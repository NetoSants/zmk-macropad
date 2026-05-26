zephyr_library()
zephyr_library_sources(encoder_custom.c)
zephyr_library_sources(keymap_autosave.c)
zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
