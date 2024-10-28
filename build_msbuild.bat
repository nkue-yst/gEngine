setlocal

mkdir Build
mkdir Binaries

cmake -B Build -S . -G "Visual Studio 17 2022"
cd Build

msbuild /p:Configuration=Release /p:Platform=x64 /t:Build ALL_BUILD.vcxproj

D:\Qt\5.15.2\msvc2019_64\bin\windeployqt.exe ..\Binaries\Release\gEngineEditor.exe

pause
