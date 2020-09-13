#!/bin/bash

# find any available executable (linx binaries are not flat in bin/ but have subfolders per release config)
# ${BASH_SOURCE%/*} is the equivalent to a plain . but behaves correctly if the script is called from somewhere else

STANDALONE_EXE=${BASH_SOURCE%/*}/../../../bin/Release/dxc-wrapper-standalone
if ! test -f "$STANDALONE_EXE"; then
    STANDALONE_EXE=${BASH_SOURCE%/*}/../../../bin/RelWithDebInfo/dxc-wrapper-standalone
fi
if ! test -f "$STANDALONE_EXE"; then
    STANDALONE_EXE=${BASH_SOURCE%/*}/../../../bin/Debug/dxc-wrapper-standalone
fi
if ! test -f "$STANDALONE_EXE"; then
    STANDALONE_EXE=${BASH_SOURCE%/*}/../../../bin/Default/dxc-wrapper-standalone
fi

$STANDALONE_EXE -wj ${BASH_SOURCE%/*}/src/shaders.json
