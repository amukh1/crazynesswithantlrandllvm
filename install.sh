wget https://www.antlr.org/download/antlr4-cpp-runtime-4.13.1-source.zip
unzip antlr4-cpp-runtime-4.13.1-source.zip
cd antlr4-cpp-runtime-4.13.1
mkdir build && cd build
cmake ..
make

sudo cp -r ../runtime/src /usr/local/include/antlr4-runtime
sudo cp libantlr4-runtime.a /usr/local/lib
cd ../..

# g++ -std=c++17 *.cpp -o index -I/usr/local/include/antlr4-runtime -L. -lantlr4-runtime
./compile.sh
cp lucy /usr/local/bin