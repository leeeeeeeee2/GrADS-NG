# nClimGrid-Daily external-data suite (goal §25, adapted to repo conventions).
# Fast unit tests live in ng/tests (label "unit"); everything here needs the
# external fixture and is labeled "nclimgrid;external-data" so ordinary
# development never downloads anything:  ctest -LE external-data
# Full suite explicitly:                   ctest -L nclimgrid
set(NCLIMGRID_SUITE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(NCLIMGRID_CACHE_DIR "$ENV{NCLIMGRID_CACHE}")
if(NOT NCLIMGRID_CACHE_DIR OR NCLIMGRID_CACHE_DIR STREQUAL "")
    set(NCLIMGRID_CACHE_DIR "$ENV{HOME}/.cache/grads-ng-test-data")
endif()
# NCLIMGRID_PYTHON: project venv python preferred (has netCDF4/numpy),
# else whatever python3 resolves to; empty when none exists.
find_program(NCLIMGRID_SYSTEM_PYTHON NAMES python3)
set(NCLIMGRID_VENV_PYTHON "${CMAKE_SOURCE_DIR}/../.venv/bin/python")
if(EXISTS "${NCLIMGRID_VENV_PYTHON}")
    set(NCLIMGRID_PYTHON "${NCLIMGRID_VENV_PYTHON}")
elseif(NCLIMGRID_SYSTEM_PYTHON)
    set(NCLIMGRID_PYTHON "${NCLIMGRID_SYSTEM_PYTHON}")
else()
    set(NCLIMGRID_PYTHON "")
endif()

set(NCLIMGRID_LABELS "nclimgrid;external-data")
set(NCLIMGRID_SKIP_RE "SKIP:")

# Machine-specific paths, settable at configure time (-DREF_GRADS_BIN=...).
# Empty REF_GRADS_BIN means the reference arm SKIPs (documented, not silent).
if(NOT DEFINED REF_GRADS_BIN)
    set(REF_GRADS_BIN "$ENV{REF_GRADS_BIN}" CACHE STRING
        "Reference OpenGrADS binary for nclimgrid reference tests")
endif()
if(NOT DEFINED REF_GADDIR)
    set(REF_GADDIR "$ENV{GADDIR}" CACHE STRING
        "Reference OpenGrADS SupportData dir (udunits.dat)")
endif()

# Setup fixture: download/verify/extract/convert once (idempotent script).
add_test(NAME nclimgrid_fixture
    COMMAND ${CMAKE_COMMAND} -E env
        "NCLIMGRID_CACHE=${NCLIMGRID_CACHE_DIR}"
        bash "${NCLIMGRID_SUITE_DIR}/download-nclimgrid-test-data.sh")
set_tests_properties(nclimgrid_fixture PROPERTIES
    LABELS "${NCLIMGRID_LABELS}"
    FIXTURES_SETUP nclimgrid_fixture
    SKIP_REGULAR_EXPRESSION "${NCLIMGRID_SKIP_RE}"
    TIMEOUT 1200)

function(nclimgrid_py_test name script)
    if(NOT NCLIMGRID_PYTHON)
        # No python at all: register a test that reports SKIP explicitly.
        add_test(NAME ${name}
            COMMAND bash -c "echo 'SKIP: no python3 available'; exit 0")
    else()
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND} -E env
                "NCLIMGRID_CACHE=${NCLIMGRID_CACHE_DIR}"
                "REF_GRADS_BIN=${REF_GRADS_BIN}"
                "GADDIR=${REF_GADDIR}"
                "${NCLIMGRID_PYTHON}" "${NCLIMGRID_SUITE_DIR}/tests/${script}"
                "--suite=${NCLIMGRID_SUITE_DIR}"
                "--cache=${NCLIMGRID_CACHE_DIR}"
                "--ngbin=$<TARGET_FILE:grads-ng>")
    endif()
    set_tests_properties(${name} PROPERTIES
        LABELS "${NCLIMGRID_LABELS}"
        FIXTURES_REQUIRED nclimgrid_fixture
        SKIP_REGULAR_EXPRESSION "${NCLIMGRID_SKIP_RE}"
        TIMEOUT 1200)
endfunction()

nclimgrid_py_test(nclimgrid_fixture_check test_fixture.py)
nclimgrid_py_test(nclimgrid_netcdf test_netcdf.py)
nclimgrid_py_test(nclimgrid_reference test_reference.py)
nclimgrid_py_test(nclimgrid_ng test_ng.py)
nclimgrid_py_test(nclimgrid_workflow test_workflow.py)
nclimgrid_py_test(nclimgrid_perf test_perf.py)

# Explicit entry point (goal §28):  cmake --build <bdir> --target test-nclimgrid
add_custom_target(test-nclimgrid
    COMMAND ${CMAKE_CTEST_COMMAND} -L nclimgrid --output-on-failure
    COMMENT "Running nClimGrid-Daily external-data suite (skips offline)"
    VERBATIM)
