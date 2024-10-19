setlocal

mkdir Build
mkdir Binaries

cmake -B Build -S . -G "Visual Studio 17 2022"
