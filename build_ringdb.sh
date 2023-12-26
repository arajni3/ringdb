#!/bin/bash

# first build liburing
chmod +x build_liburing.sh
./build_liburing.sh

# can alternatively set to Release, RelWithDebInfo, or MinSizeRel
cmake_config=Debug
build_dir=${PWD}/${cmake_config}
# put build files and executable RingDB in ${build_dir}, where PWD should be the directory for the ringdb project
cmake --no-warn-unused-cli -DCMAKE_BUILD_TYPE:STRING=${cmake_config} -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE -DCMAKE_C_COMPILER:FILEPATH=/usr/bin/gcc -DCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/g++ -S"${PWD}" -B"${build_dir}" -G "Unix Makefiles"
cmake --build ${build_dir} --config ${cmake_config} --target all -j 10 --
sudo chown -R root ${build_dir}/RingDB
sudo chmod -R 4755 ${build_dir}/RingDB