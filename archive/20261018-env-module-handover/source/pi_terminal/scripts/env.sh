#!/usr/bin/env bash
# Source this file before invoking cmake/ctest or a debugger.
SDK_HOME="${OPI_SDK_HOME:-$HOME/.local/share/orange-pi-terminal-sdk}"
if [[ -d "$SDK_HOME/tools/bin" ]]; then
    export PATH="$SDK_HOME/tools/bin:$PATH"
fi
QT_ROOT="${OPI_QT_ROOT:-$SDK_HOME/qt/6.5.3/gcc_64}"
if [[ -d "$QT_ROOT" ]]; then
    export CMAKE_PREFIX_PATH="$QT_ROOT${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
    export LD_LIBRARY_PATH="$QT_ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export PATH="$QT_ROOT/bin:$PATH"
fi
