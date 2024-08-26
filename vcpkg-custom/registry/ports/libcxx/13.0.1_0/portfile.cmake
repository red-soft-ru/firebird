vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO llvm/llvm-project
    REF 75e33f71c2dae584b13a7d1186ae0a038ba98838
    HEAD_REF llvmorg-13.0.1
    SHA512 8bd80efe88160f615a9dc6fbcdab693d1459ca483410cf990f25169079e46726429f24ad042d287bfcfef8fd54a8a7264a67a710edaa24ebbf1e815a62b1f812
)

file(REMOVE_RECURSE "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-dbg" "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel")

set(BUILD_DIR "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel")

set(INSTALL_PREFIX "${CURRENT_PACKAGES_DIR}")

configure_file("${CMAKE_CURRENT_LIST_DIR}/build.sh.in" "${BUILD_DIR}/build.sh" @ONLY)

vcpkg_execute_required_process(
    COMMAND "${SHELL}" ./build.sh
    WORKING_DIRECTORY "${BUILD_DIR}"
    LOGNAME "build-${TARGET_TRIPLET}-rel"
)
