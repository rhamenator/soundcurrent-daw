@echo off
rem SPDX-License-Identifier: GPL-3.0-only
rem Run in the actual native x64 MSVC environment prepared by the workflow.
setlocal
if exist native-dependencies\sndfile-build exit /b 1
if exist native-dependencies\sndfile-repeat exit /b 1
set "SC_MEDIA_FLAGS=/Brepro /PDBALTPATH:sndfile.pdb"
cmake -S native-dependencies/libsndfile-1.2.2 -B native-dependencies/sndfile-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DBUILD_SHARED_LIBS=ON -DENABLE_EXTERNAL_LIBS=OFF -DENABLE_MPEG=OFF -DBUILD_PROGRAMS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DBUILD_REGTEST=OFF -DENABLE_EXPERIMENTAL=OFF -DINSTALL_PKGCONFIG_MODULE=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5 "-DCMAKE_SHARED_LINKER_FLAGS=%SC_MEDIA_FLAGS%"
if errorlevel 1 exit /b 1
cmake --build native-dependencies/sndfile-build --parallel 2 --target sndfile
if errorlevel 1 exit /b 1
cmake -S native-dependencies/libsndfile-1.2.2 -B native-dependencies/sndfile-repeat -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DBUILD_SHARED_LIBS=ON -DENABLE_EXTERNAL_LIBS=OFF -DENABLE_MPEG=OFF -DBUILD_PROGRAMS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DBUILD_REGTEST=OFF -DENABLE_EXPERIMENTAL=OFF -DINSTALL_PKGCONFIG_MODULE=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5 "-DCMAKE_SHARED_LINKER_FLAGS=%SC_MEDIA_FLAGS%"
if errorlevel 1 exit /b 1
cmake --build native-dependencies/sndfile-repeat --parallel 2 --target sndfile
if errorlevel 1 exit /b 1
powershell -NoProfile -File .github/scripts/record-preview-media.ps1
exit /b %errorlevel%
