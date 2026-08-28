# Warnings are on and treated as real. Third-party headers are included as
# SYSTEM (see Dependencies.cmake) and MSVC's /external switches keep their
# noise from failing our build.
function(daveshot_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /permissive- /WX
            /external:anglebrackets /external:W0
            /utf-8)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()
