cmake_minimum_required(VERSION 3.24)

project(SUS_DLLHACK VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(QT_MSVC_ROOT "D:/Qt/6.11.0/msvc2022_64" CACHE PATH "Qt MSVC 2022 根目录")
set(CMAKE_PREFIX_PATH "${QT_MSVC_ROOT}" CACHE PATH "Qt CMake 前缀" FORCE)
set(DLLHACK_MINGW_ROOT "" CACHE PATH "MinGW64 source directory used to package the runtime proxy compiler")
set(_dllhack_mingw_candidates
    "${DLLHACK_MINGW_ROOT}"
    "$ENV{DLLHACK_MINGW_ROOT}"
    "${CMAKE_SOURCE_DIR}/MinGW64"
    "${CMAKE_SOURCE_DIR}/../MinGW64"
    "${CMAKE_SOURCE_DIR}/../sus-Dllhack/MinGW64"
    "C:/Users/Yzy12/Desktop/sus-Dllhack/MinGW64"
    "C:/Users/Yzy12/Desktop/test/MinGW64"
    "C:/Users/Yzy12/Desktop/MinGW64"
)
foreach(_dllhack_mingw_candidate IN LISTS _dllhack_mingw_candidates)
    if(NOT _dllhack_mingw_candidate STREQUAL "")
        file(TO_CMAKE_PATH "${_dllhack_mingw_candidate}" _dllhack_mingw_candidate_norm)
        if(EXISTS "${_dllhack_mingw_candidate_norm}/bin/gcc.exe")
            set(DLLHACK_MINGW_ROOT "${_dllhack_mingw_candidate_norm}" CACHE PATH "MinGW64 source directory used to package the runtime proxy compiler" FORCE)
            break()
        endif()
    endif()
endforeach()
if(EXISTS "${DLLHACK_MINGW_ROOT}/bin/gcc.exe")
    message(STATUS "SUS-DLLHACK MinGW root: ${DLLHACK_MINGW_ROOT}")
else()
    message(WARNING "SUS-DLLHACK MinGW root was not found. build/tools/mingw64 will be missing unless DLLHACK_MINGW_ROOT points to a MinGW64 directory containing bin/gcc.exe.")
endif()

set(THREADS_PREFER_PTHREAD_FLAG OFF CACHE BOOL "" FORCE)
set(CMAKE_USE_WIN32_THREADS_INIT ON CACHE BOOL "" FORCE)
set(CMAKE_USE_PTHREADS_INIT OFF CACHE BOOL "" FORCE)
set(CMAKE_HAVE_LIBC_PTHREAD OFF CACHE BOOL "" FORCE)
set(CMAKE_HAVE_PTHREADS_CREATE OFF CACHE BOOL "" FORCE)
set(CMAKE_HAVE_PTHREAD_CREATE OFF CACHE BOOL "" FORCE)
set(CMAKE_THREAD_LIBS_INIT "" CACHE STRING "" FORCE)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

add_executable(zeroeye_cli
    third_party/ZeroEye/zeroeye_main.cpp
)
target_include_directories(zeroeye_cli PRIVATE third_party/ZeroEye)
target_compile_definitions(zeroeye_cli PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)
target_link_libraries(zeroeye_cli PRIVATE
    dbghelp
    wintrust
    crypt32
    shell32
    user32
    advapi32
)
if(MSVC)
    target_compile_options(zeroeye_cli PRIVATE /utf-8 /EHsc)
endif()

qt_add_executable(SUS-DLLHACK WIN32
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.hpp
    src/ZeroEyeTask.cpp
    src/ZeroEyeTask.hpp
    src/ExtractionManager.cpp
    src/ExtractionManager.hpp
    src/PeExports.cpp
    src/PeExports.hpp
    src/ProxyGenerator.cpp
    src/ProxyGenerator.hpp
    src/ProxyValidator.cpp
    src/ProxyValidator.hpp
    third_party/sgn/SgnLite.cpp
    third_party/sgn/SgnLite.hpp
    resources/resources.qrc
)

target_include_directories(SUS-DLLHACK PRIVATE
    src
    third_party/ZeroEye
    third_party/sgn
)
target_compile_definitions(SUS-DLLHACK PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    DLLHACK_MINGW_ROOT="${DLLHACK_MINGW_ROOT}"
)
target_link_libraries(SUS-DLLHACK PRIVATE
    Qt6::Core
    Qt6::Widgets
    dbghelp
    shell32
    user32
)
if(MSVC)
    target_compile_options(SUS-DLLHACK PRIVATE /utf-8 /EHsc)
endif()

add_custom_command(TARGET SUS-DLLHACK POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:zeroeye_cli>
        $<TARGET_FILE_DIR:SUS-DLLHACK>
)

if(EXISTS "${DLLHACK_MINGW_ROOT}/bin/gcc.exe")
    set(_mini_mingw "$<TARGET_FILE_DIR:SUS-DLLHACK>/tools/mingw64")
    set(_gcc_runtime "${DLLHACK_MINGW_ROOT}/libexec/gcc/x86_64-w64-mingw32/4.9.2")

    add_custom_command(TARGET SUS-DLLHACK POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:SUS-DLLHACK>/tools"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_mini_mingw}/bin"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_mini_mingw}/libexec/gcc/x86_64-w64-mingw32/4.9.2"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_mini_mingw}/x86_64-w64-mingw32/bin"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_mini_mingw}/x86_64-w64-mingw32/lib"
    )

    foreach(file_name
            gcc.exe as.exe ld.exe
            libgcc_s_seh_64-1.dll libwinpthread_64-1.dll libiconv-2.dll libintl-8.dll)
        if(EXISTS "${DLLHACK_MINGW_ROOT}/bin/${file_name}")
            add_custom_command(TARGET SUS-DLLHACK POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DLLHACK_MINGW_ROOT}/bin/${file_name}"
                    "${_mini_mingw}/bin/${file_name}"
            )
        endif()
    endforeach()

    foreach(file_name
            cc1.exe libcloog-isl-3.dll libgmp-3.dll libisl-10.dll
            libmpc-2.dll libmpfr-1.dll libiconv-2.dll)
        if(EXISTS "${_gcc_runtime}/${file_name}")
            add_custom_command(TARGET SUS-DLLHACK POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_gcc_runtime}/${file_name}"
                    "${_mini_mingw}/libexec/gcc/x86_64-w64-mingw32/4.9.2/${file_name}"
            )
        endif()
    endforeach()

    foreach(file_name as.exe ld.exe)
        if(EXISTS "${DLLHACK_MINGW_ROOT}/x86_64-w64-mingw32/bin/${file_name}")
            add_custom_command(TARGET SUS-DLLHACK POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DLLHACK_MINGW_ROOT}/x86_64-w64-mingw32/bin/${file_name}"
                    "${_mini_mingw}/x86_64-w64-mingw32/bin/${file_name}"
            )
        endif()
    endforeach()

    foreach(file_name libkernel32.a libuser32.a)
        if(EXISTS "${DLLHACK_MINGW_ROOT}/x86_64-w64-mingw32/lib/${file_name}")
            add_custom_command(TARGET SUS-DLLHACK POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DLLHACK_MINGW_ROOT}/x86_64-w64-mingw32/lib/${file_name}"
                    "${_mini_mingw}/x86_64-w64-mingw32/lib/${file_name}"
            )
        endif()
    endforeach()
endif()

