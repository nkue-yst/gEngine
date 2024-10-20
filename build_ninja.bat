setlocal

mkdir Build
mkdir Binaries

cmake -B Build -S . -G "Ninja"
ninja Build
