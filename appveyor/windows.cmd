SETLOCAL EnableDelayedExpansion
SET

REM Visual Studio Build
REM msbuild %MSBUILD_OPTIONS% -p:Platform=x86 %SOLUTION% || exit /b !ERRORLEVEL!
REM msbuild %MSBUILD_OPTIONS% -p:Platform=x64 %SOLUTION% || exit /b !ERRORLEVEL!

REM Qt Build
mkdir qt\32
cd qt\32
(call "%VCVARS32%" x86 && "%QT_BASE_DIR32%\bin\qmake.exe" ..\..\Windows\qtsupercanbus\supercan.pro -spec win32-msvc "CONFIG+=qtquickcompiler" && make qmake_all) || exit /b !ERRORLEVEL!
cd ..\..
mkdir qt\64
cd qt\64
(call "%VCVARS64%" x64 && "%QT_BASE_DIR64%\bin\qmake.exe" ..\..\Windows\qtsupercanbus\supercan.pro -spec win32-msvc "CONFIG+=qtquickcompiler" && make qmake_all) || exit /b !ERRORLEVEL!
cd ..\..

REM package
mkdir bin\x86
mkdir bin\x64
mkdir lib\x86
mkdir lib\x64
mkdir inc
xcopy /y /f Windows\Win32\Release\supercan32.dll bin\x86\
xcopy /y /f Windows\Win32\Release\supercan_app32.exe bin\x86\
xcopy /y /f Windows\Win32\Release\supercan32.lib lib\x86\
xcopy /y /f Windows\x64\Release\supercan64.dll bin\x64\
xcopy /y /f Windows\x64\Release\supercan_app64.exe bin\x64\
xcopy /y /f Windows\x64\Release\supercan64.lib lib\x64\
xcopy /y /f /s Windows\inc inc\
xcopy /y /f Windows\Win32\Release\supercan_srv32.exe bin\x64\
xcopy /y /f Windows\x64\Release\supercan_srv64.exe bin\x64\
7z a -t7z -m0=lzma -mx=9 -mfb=64 -md=32m -ms=on supercan-win.7z bin lib inc src  || exit /b !ERRORLEVEL!
REM installer
call "%VCVARS64%" x64 && makensis /DSC_VERSION_MAJOR=%PRODUCT_VERSION_MAJOR% /DSC_VERSION_MINOR=%PRODUCT_VERSION_MINOR% /DSC_VERSION_PATCH=%PRODUCT_VERSION_PATCH% /DSC_VERSION_BUILD=%PRODUCT_VERSION_BUILD% Windows\NSIS\supercan.nsi  || exit /b !ERRORLEVEL!
move Windows\NSIS\supercan_inst.exe .