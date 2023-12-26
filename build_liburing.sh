cd liburing
./configure --cc=gcc --cxx=g++ --use-libc
make -j$(nproc)
sudo make install