#!/bin/bash

arg_domain=$1
arg_shadername=$2
arg_output=$3

#while [[ "$#" -gt 0 ]]; do case $1 in
#    -d|--domain) arg_domain="$2"; shift;;
#    -i|--input) arg_shadername="$2"; shift;;
#    -o|--output) arg_output="$2"; shift;;
#    *) echo "Unknown parameter: $1"; exit 1;;
#esac; shift; done

if [ -z "$arg_output" ]; then
    arg_output="$arg_shadername";
fi

spirv_additional_flags=""
if [ "$arg_domain" = "vs" ] || [ "$arg_domain" = "gs" ] || [ "$arg_domain" = "ds" ]; then
    spirv_additional_flags="-fvk-invert-y "
fi

#echo "arg_domain= $arg_domain"
#echo "arg_shadername= $arg_shadername"
#echo "arg_output= $arg_output"

docker run --rm -v $(pwd):$(pwd) -w $(pwd) gwihlidal/dxc ./src/${arg_shadername}.hlsl -T ${arg_domain}_6_0 -E main_${arg_domain} -spirv ${spirv_additional_flags} -fspv-target-env=vulkan1.1 -fvk-b-shift 0 all -fvk-t-shift 1000 all -fvk-u-shift 2000 all -fvk-s-shift 3000 all -Fo ./bin/${arg_output}.spv
