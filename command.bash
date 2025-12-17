cd /c/Users/Peter_Wang/Desktop/Temp/CPlusPlusScripts/ControlHub/build
rm -rf *
cmake .. -G "MinGW Makefiles"
cmake --build .
cp ../DLLs/libstdc++-6.dll ./
cp ../DLLs/libwinpthread-1.dll ./
