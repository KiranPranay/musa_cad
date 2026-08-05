# End-to-end check of the shipped binary's command line (issue #11).
#
# The Catch2 cases in test_cli.cpp cover the parser; this asserts what a SCRIPT sees:
# the real executable's exit codes and output, with no display available. Run via
#   cmake -DAPP=<path to musacad_app> -DWORK=<scratch dir> -P cli_check.cmake
# Mirrors header_scan.cmake -- cross-platform, no shell required.

if(NOT DEFINED APP)
    message(FATAL_ERROR "APP not set")
endif()
if(NOT DEFINED WORK)
    message(FATAL_ERROR "WORK not set")
endif()
file(MAKE_DIRECTORY "${WORK}")

# No display: --help/--version/--check must not need one. QT_QPA_PLATFORM is deliberately
# NOT set -- these paths must return before any Qt application object is constructed.
set(ENV{DISPLAY} "")
set(ENV{WAYLAND_DISPLAY} "")

set(failures "")

# run(<expected exit code> <output variable> <args...>)
function(run expect out_var)
    execute_process(COMMAND "${APP}" ${ARGN}
                    RESULT_VARIABLE code
                    OUTPUT_VARIABLE stdout_text
                    ERROR_VARIABLE stderr_text
                    TIMEOUT 60)
    set(${out_var} "${stdout_text}${stderr_text}" PARENT_SCOPE)
    if(NOT code EQUAL expect)
        set(failures "${failures};`musacad ${ARGN}` exited ${code}, expected ${expect}" PARENT_SCOPE)
    endif()
endfunction()

# --- 0: --help and --version succeed with no display -------------------------------
run(0 help_out --help)
if(NOT help_out MATCHES "Usage:")
    list(APPEND failures "--help did not print a Usage: section")
endif()
if(NOT help_out MATCHES "--check")
    list(APPEND failures "--help did not document --check")
endif()

run(0 ver_out --version)
if(NOT ver_out MATCHES "Musa CAD")
    list(APPEND failures "--version did not name the application")
endif()

# --- 1: usage errors exit 1 --------------------------------------------------------
run(1 e1 --nonsense)
run(1 e2 --check)

# --- 2: --check on a valid drawing exits 0 -----------------------------------------
# A minimal but genuine v14 document. Written at version 14 on purpose: as the format
# advances this doubles as an older-version-still-loads check from the CLI's side.
set(good "${WORK}/cli_good.musa")
file(WRITE "${good}"
"MUSACAD 14
UNITS mm
CURRENT 0
LTSCALE 1
LAYER 255 255 255 0 25 1 0 0 0
LINE 0 0 100 0 0 7 255 255 255 0 25
LINE 100 0 100 50 0 7 255 255 255 0 25
END
")
run(0 good_out --check "${good}")

# --- 3: --check on a malformed drawing exits 2 and says why on stderr ---------------
set(bad "${WORK}/cli_bad.musa")
file(WRITE "${bad}"
"MUSACAD 14
LINE this is not a coordinate
END
")
run(2 bad_out --check "${bad}")
if(NOT bad_out MATCHES "LINE")
    list(APPEND failures "--check on a malformed file did not report the offending record")
endif()

# --- 4: --check on a missing file exits 2 ------------------------------------------
run(2 miss_out --check "${WORK}/definitely_absent.musa")

if(failures)
    string(REPLACE ";" "\n  " pretty "${failures}")
    message(FATAL_ERROR "CLI end-to-end check failed:\n  ${pretty}")
endif()
message(STATUS "cli_check: help/version/check exit codes correct with no display")
