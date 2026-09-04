set(TPSTUB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../tp_stub CACHE PATH "Path to tp_stub directory")
include(${TPSTUB_DIR}/krkrz.cmake)

find_package(ZLIB REQUIRED)

krkrz_plugin(layerExSave
    NCBIND
    SOURCES
        LodePNG/lodepng.cpp
        savepng.cpp
        savetlg5.cpp
        utils.cpp
        Main.cpp
        tlg5/slide.cpp
        LicensesGen.cpp
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
    LIBRARIES
        ZLIB::ZLIB
)
