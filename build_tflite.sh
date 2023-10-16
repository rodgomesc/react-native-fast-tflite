#!/bin/bash

# Variables
output_dir="output"

# Navigate to tensorflow directory
cd tensorflow || exit 1

# Cleanup and prepare output directories
rm -rf tmp output
mkdir -p tmp/include
mkdir -p "$output_dir/include"
mkdir -p "$output_dir/lib"

# Define ABIs and their corresponding subdirectories
abis="android_arm64=arm64-v8a android_arm=armeabi-v7a android_x86_64=x86_64"

# Loop through each ABI and compile
for abi in $abis; do
  rm -rf bazel-bin bazel-out
  IFS="=" read -r key value <<< "$abi"
  output_subdir="$value"

  bazel build \
          -c opt \
          --config="$key" \
          //tensorflow/lite/c:tensorflowlite_c

  # Copy compiled .so file to output directory
  mkdir -p "$output_dir/lib/$output_subdir"
  cp bazel-bin/tensorflow/lite/c/libtensorflowlite_c.so "$output_dir/lib/$output_subdir/" || exit 1
done

# Merge remaining tensorflow headers
rsync -avm --include='*.h' -f 'hide,! */' tensorflow tmp/include

# Copy headers to output directory
cp -r tmp/include "$output_dir/"
