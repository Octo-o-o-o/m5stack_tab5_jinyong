# shellcheck shell=bash
# Locate a host cmake without baking a personal path into the repo.

if command -v cmake >/dev/null 2>&1; then
    command -v cmake
    return 0 2>/dev/null || exit 0
fi

for cand in \
    "${HOME}/.espressif/tools/cmake/3.30.2/CMake.app/Contents/bin/cmake" \
    "${HOME}/.espressif/tools/cmake/"*/CMake.app/Contents/bin/cmake \
    "${HOME}/.espressif/tools/cmake/"*/bin/cmake \
    /opt/homebrew/bin/cmake \
    /usr/local/bin/cmake
 do
    if [[ -x "${cand}" ]]; then
        echo "${cand}"
        return 0 2>/dev/null || exit 0
    fi
done

echo ""
return 1 2>/dev/null || exit 1
