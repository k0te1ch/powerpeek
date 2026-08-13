# Warning configuration for MSVC.
#
# The Windows SDK headers themselves are not /W4 clean, so third-party headers are
# demoted to /external:W0 rather than the project being lowered to match them.

function(pp_set_warnings target as_errors)
    target_compile_options(${target} PRIVATE
        /W4
        /w14242 # conversion, possible loss of data
        /w14254 # bitfield conversion, possible loss of data
        /w14263 # member function does not override any base class virtual
        /w14265 # class has virtual functions but destructor is not virtual
        /w14287 # unsigned/negative constant mismatch
        /we4289 # loop control variable used outside the loop
        /w14296 # expression is always true/false
        /w14311 # pointer truncation
        /w14545 # expression before comma evaluates to a function missing an argument list
        /w14546 # function call before comma missing argument list
        /w14547 # operator before comma has no effect
        /w14549 # operator before comma has no effect
        /w14555 # expression has no effect
        /w14619 # pragma warning: there is no warning number
        /w14640 # thread-unsafe static member initialisation
        /w14826 # sign-extending conversion, possible behaviour change
        /w14905 # wide string literal cast to LPSTR
        /w14906 # string literal cast to LPWSTR
        /w14928 # illegal copy-initialisation

        # Window procedures and COM callbacks legitimately ignore parameters that the
        # signature forces on them; silencing this project-wide is cheaper than
        # decorating hundreds of call sites.
        /wd4100

        # Treat the Windows SDK / C++/WinRT headers as external so their own warnings
        # do not drown ours.
        /external:anglebrackets
        /external:W0
    )

    if(as_errors)
        target_compile_options(${target} PRIVATE /WX)
    endif()
endfunction()
