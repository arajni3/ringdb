# first build liburing
chmod +x build_liburing.sh
./build_liburing.sh

# can alternatively set to RELEASE
cmake_config=Debug
# put build files and executable RingDB in ${PWD}/${cmake_config}, where PWD should be the directory for the ringdb project
/usr/bin/cmake --no-warn-unused-cli -DCMAKE_BUILD_TYPE:STRING=${cmake_config} -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE -DCMAKE_C_COMPILER:FILEPATH=/usr/bin/gcc -DCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/g++ -S"${PWD}" -B"${PWD}/${cmake_config}" -G "Unix Makefiles"
/usr/bin/cmake --build ${PWD}/${cmake_config}--config ${cmake_config} --target all -j 10 --
sudo chown -R root ${PWD}
sudo chmod -R 4755 ${PWD}