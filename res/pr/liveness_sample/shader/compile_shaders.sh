#!/bin/bash

# -P 0: Multithreading (no limit on spawned processes)
# -n 3: three arguments per line in shader_list.txt
# -r: don't run if the input is empty
cat shader_list.txt | xargs -P 0 -n 3 -r -- bash -c './dxc_bin/compile_single_shader.sh $0 $1 $2'
