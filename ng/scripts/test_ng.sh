#!/bin/bash
# GrADS-NG Test Script

echo "Testing GrADS-NG..."

# Build NG version
cd /workspaces/GrADS-NG/ng
mkdir -p build
cd build
cmake ..
make -j$(nproc)

if [ $? -ne 0 ]; then
    echo "Build failed!"
    exit 1
fi

echo "Build successful!"

# Test basic execution
./grads-ng -b
if [ $? -ne 0 ]; then
    echo "Basic execution failed!"
    exit 1
fi

echo "Basic execution successful!"

# Test with custom geometry
./grads-ng -b -g 1024x768
if [ $? -ne 0 ]; then
    echo "Custom geometry test failed!"
    exit 1
fi

echo "Custom geometry test successful!"

echo "All tests passed!"