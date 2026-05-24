cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(/std:c++latest)

# Advance options go before the project
if(MSVC)

    # MSVC specific options
    add_compile_options(/W4 /WX)  # High warning level, treat warnings as errors
    add_compile_options(/sdl)     # Security development lifecycle checks
    add_compile_options(/Oy-)     # Disable frame pointer omission for better debugging
    #add_compile_definitions(/O2) # Optimization Flag. Should be set when this project is released
    add_compile_options(/fsanitize=address,undefined) # More info Asan/UBsan: https://learn.microsoft.com/en-us/cpp/sanitizers/asan?view=msvc-170 
    # TODO: We need to create code that if we release the build, the following flags should be enabled/disabled
        # We want absolute max performance, and will sacrifice hardening
        # Disable:
            # /W4 /WX
            # /Oy-
            # Asan/UbSan
        # Enable:
            #/O2
            

endif()


project(../Windows-To-Go
        VERSION 1.0
        LANGUAGES CXX
        DESCRIPTION "Create bootable Windows USB drives that work on any computer. Features smart system migration, automatic repair tools, and persistent Windows environment with full driver compatibility across different hardware."
        HOMEPAGE_URL "https://www.github.com/alphastigma101/Windows-To-Go"
)

if(MSVC)

    add_compile_definitions(UNICODE _UNICODE)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

else()

    add_compile_definitions(ANSI _ANSI)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

endif()

add_library(togo)

if(MSVC)

    target_compile_definitions(togo PRIVATE UNICODE _UNICODE IS_WINDOWS=1)
    
    # Enable module support for togo library 
    target_compile_options(togo PRIVATE 
        /std:c++latest
    )

else()

    target_compile_definitions(togo PRIVATE ANSI _ANSI IS_WINDOWS=0)
    target_compile_options(togo PRIVATE 
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -fsanitize=address,undefined
        -fno-sanitize-recover=all
        -fmodules
        -fbuiltin-module-map
    )

endif()

target_sources(togo
    PRIVATE
        windows/togo.cc
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS
                windows editor interface tracker
            FILES
                windows/togo.h 
                editor/editor.h
                interface/interface.h
                tracker/tracker.h
        FILE_SET CXX_MODULES
            BASE_DIRS
                modules
            FILES
                modules/platform.ixx
           
)

add_library(editor)

if(MSVC)

    target_compile_definitions(editor PRIVATE UNICODE _UNICODE IS_WINDOWS=1)

    # Enable module support for editor library 
    target_compile_options(editor PRIVATE 
        /std:c++latest
    )

else() 

    target_compile_definitions(editor PRIVATE ANSI _ANSI IS_WINDOWS=0)
    target_compile_options(togo PRIVATE 
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -fsanitize=address,undefined
        -fno-sanitize-recover=all
        -fmodules
        -fbuiltin-module-map
    )

endif()

target_sources(editor
    PRIVATE
        # Compile editor.cc as a library, so it can be later on linked with the executable 
        editor/editor.cc
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS
                # Functions just like the -I compiler flag, blocking circular dependencies
                editor interface tracker
            FILES 
                editor/editor.h
                interface/interface.h
                tracker/tracker.h

        FILE_SET CXX_MODULES
            BASE_DIRS
                modules
            FILES
                modules/platform.ixx
)

add_executable(WindowsToGo main.cc)

if(MSVC)

    target_compile_definitions(WindowsToGo PRIVATE UNICODE _UNICODE)
    set_target_properties(WindowsToGo PROPERTIES
        VS_GLOBAL_CharacterSet "Unicode"
    )

else()

    target_compile_definitions(WindowsToGo PRIVATE ANSI _ANSI)
    set_target_properties(WindowsToGo PROPERTIES
        VS_GLOBAL_CharacterSet "ANSI"
    )
    # Linux host - offer MinGW-w64 cross-compilation
    find_program(MINGW64_CXX x86_64-w64-mingw32-g++ 
        PATHS /usr/bin /usr/local/bin /usr/x86_64-w64-mingw32/bin
        DOC "MinGW-w64 C++ compiler"
    )
    
    find_program(MINGW64_CC x86_64-w64-mingw32-gcc
        PATHS /usr/bin /usr/local/bin /usr/x86_64-w64-mingw32/bin
        DOC "MinGW-w64 C compiler"
    )

endif()


if (MSVC)

    target_link_libraries(WindowsToGo togo editor setupapi VirtDisk sfc version)

else()

    target_link_libraries(WindowsToGo togo editor pthread setupapi VirtDisk winioctl static sfc version)

endif()