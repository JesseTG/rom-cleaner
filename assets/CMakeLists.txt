find_program(Inkscape_EXECUTABLE
    NAMES inkscape
    PATHS
    /usr/bin
    /usr/local/bin
    C:/Program Files/
)

add_custom_command(
    OUTPUT "${CMAKE_SOURCE_DIR}/assets/png/snes.png"
    DEPENDS "snes.svg"
    COMMAND "${Inkscape_EXECUTABLE}" --export-area-page --export-filename="${CMAKE_SOURCE_DIR}/assets/png/snes.png" "${CMAKE_SOURCE_DIR}/assets/snes.svg"
    COMMENT "Exporting snes.svg to snes.png"
)

b_embed_proxy_target(romcleaner_libretro assets)
b_embed(assets "png/snes.png")
b_embed(assets "dust00.png")
b_embed(assets "dust01.png")
b_embed(assets "dust02.png")
b_embed(assets "dust03.png")
b_embed(assets "dust04.png")
b_embed(assets "dust05.png")
b_embed(assets "sparkle00.png")
b_embed(assets "sparkle01.png")
b_embed(assets "sparkle02.png")