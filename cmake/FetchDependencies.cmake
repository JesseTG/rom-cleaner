macro(define_git_dependency_vars name default_url default_tag)
    string(TOUPPER ${name} VAR_NAME)
    string(MAKE_C_IDENTIFIER ${VAR_NAME} VAR_NAME)

    if (NOT ${VAR_NAME}_REPOSITORY_URL)
        set(
                "${VAR_NAME}_REPOSITORY_URL"
                "${default_url}"
                CACHE STRING
                "${name} repository URL. Set this to use a fork."
                FORCE
        )
    endif ()

    if (NOT ${VAR_NAME}_REPOSITORY_TAG)
        set(
                "${VAR_NAME}_REPOSITORY_TAG"
                "${default_tag}"
                CACHE STRING
                "${name} repository commit hash or tag. Set this when using a new version or a custom branch."
                FORCE
        )
    endif ()
endmacro()

function(fetch_dependency name default_url default_tag)
    define_git_dependency_vars(${name} ${default_url} ${default_tag})

    message(STATUS "Using ${name}: ${${VAR_NAME}_REPOSITORY_URL} (ref ${${VAR_NAME}_REPOSITORY_TAG})")

    FetchContent_Declare(
        ${name}
        GIT_REPOSITORY "${${VAR_NAME}_REPOSITORY_URL}"
        GIT_TAG "${${VAR_NAME}_REPOSITORY_TAG}"
    )

    FetchContent_GetProperties(${name})
endfunction()

set(CMAKE_MODULE_PATH "${FETCHCONTENT_BASE_DIR}/embed-binaries-src/cmake" "${CMAKE_MODULE_PATH}")
fetch_dependency(libretro-common "https://github.com/libretro/libretro-common" "fce57fd")
fetch_dependency(embed-binaries "https://github.com/andoalon/embed-binaries.git" "21f28ca")
fetch_dependency(pntr "https://github.com/RobLoach/pntr" "8855a90")
fetch_dependency(kissfft "https://github.com/mborgerding/kissfft" "febd4ca")
fetch_dependency(span-lite "https://github.com/martinmoene/span-lite" "00afc28")

FetchContent_MakeAvailable(libretro-common pntr embed-binaries kissfft span-lite)
