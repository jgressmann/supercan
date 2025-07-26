SETLOCAL EnableDelayedExpansion
SET

REM Try to set version string from AppVeyor, Github Action
if "%GITHUB_ACTION%" NEQ "" (
    if "%GITHUB_REF_TYPE%" EQU "tag" (
        set VERSION_STR=!GITHUB_REF_NAME:~1!.%GITHUB_RUN_NUMBER%
    ) else (
        set VERSION_STR=0.0.0.0
    )
) else if "%APPVEYOR_REPO_TAG%" EQU "true" (
    set VERSION_STR=!APPVEYOR_REPO_TAG_NAME:~1!.%APPVEYOR_BUILD_NUMBER%
) else (
    if "%APPVEYOR_BUILD_VERSION%" NEQ "" (
        set VERSION_STR=0.%APPVEYOR_BUILD_VERSION%
    ) else (
        set VERSION_STR=0.0.0.0
    )
)



for /F "tokens=1,2,3,4 delims=." %%a in ("!VERSION_STR!") do (
    set SC_VERSION_MAJOR=%%a
    set SC_VERSION_MIN=%%b
    set SC_VERSION_PATCH=%%c
    set SC_VERSION_BUILD=%%d
)

REM SC_VERSION_MINOR looses content for some reason
echo SC_VERSION_MAJOR=!SC_VERSION_MAJOR!
echo SC_VERSION_MIN=!SC_VERSION_MIN!
echo SC_VERSION_PATCH=!SC_VERSION_PATCH!
echo SC_VERSION_BUILD=!SC_VERSION_BUILD!

REM NSIS version args
set NSIS_SC_VERSION_ARGS=/DSC_VERSION_MAJOR=!SC_VERSION_MAJOR! /DSC_VERSION_MINOR=!SC_VERSION_MIN! /DSC_VERSION_PATCH=!SC_VERSION_PATCH! /DSC_VERSION_BUILD=!SC_VERSION_BUILD!
echo NSIS_SC_VERSION_ARGS=!NSIS_SC_VERSION_ARGS!



REM Store commit
git rev-parse HEAD >COMMIT

REM Visual Studio Build
msbuild %MSBUILD_OPTIONS% -p:Platform=x86 %SOLUTION% || exit /b !ERRORLEVEL!
msbuild %MSBUILD_OPTIONS% -p:Platform=x64 %SOLUTION% || exit /b !ERRORLEVEL!

if "%SC_BUILD_QT_PLUGIN%" NEQ "" (
    if "%SC_BUILD_QT_PLUGIN%" NEQ "0" (
        REM Qt Build
        mkdir qt\32
        cd qt\32
        (call "%VCVARS32%" x86 && "%QT_BASE_DIR32%\bin\qmake.exe" ..\..\Windows\qtsupercanbus\supercan.pro -spec win32-msvc "CONFIG+=qtquickcompiler" && nmake qmake_all && nmake) || exit /b !ERRORLEVEL!
        cd ..\..
        mkdir qt\64
        cd qt\64
        (call "%VCVARS64%" x64 && "%QT_BASE_DIR64%\bin\qmake.exe" ..\..\Windows\qtsupercanbus\supercan.pro -spec win32-msvc "CONFIG+=qtquickcompiler" && nmake qmake_all && nmake) || exit /b !ERRORLEVEL!
        cd ..\..
    )
)

REM package
mkdir bin\x86
mkdir bin\x64
mkdir lib\x86
mkdir lib\x64
mkdir pdb\x86
mkdir pdb\x64
mkdir inc
xcopy /y /f Windows\Win32\Release\supercan32.dll bin\x86\
xcopy /y /f Windows\Win32\Release\supercan_app32.exe bin\x86\
xcopy /y /f Windows\Win32\Release\supercan32.lib lib\x86\
xcopy /y /f Windows\x64\Release\supercan64.dll bin\x64\
xcopy /y /f Windows\x64\Release\supercan_app64.exe bin\x64\
xcopy /y /f Windows\x64\Release\supercan64.lib lib\x64\
xcopy /y /f /s Windows\inc inc\
xcopy /y /f Windows\Win32\Release\supercan_srv32.exe bin\x86\
xcopy /y /f Windows\x64\Release\supercan_srv64.exe bin\x64\
if "%SC_BUILD_QT_PLUGIN%" NEQ "0" (
    xcopy /y /f qt\32\plugins\canbus\*.dll bin\x86\
    xcopy /y /f qt\64\plugins\canbus\*.dll bin\x64\
)
xcopy /y /f Windows\Win32\Release\supercan32.pdb pdb\x86\
xcopy /y /f Windows\Win32\Release\supercan_app32.pdb pdb\x86\
xcopy /y /f Windows\Win32\Release\supercan32.pdb pdb\x86\
xcopy /y /f Windows\x64\Release\supercan64.pdb pdb\x64\
xcopy /y /f Windows\x64\Release\supercan_app64.pdb pdb\x64\
xcopy /y /f Windows\x64\Release\supercan64.pdb pdb\x64\
xcopy /y /f Windows\Win32\Release\supercan_srv32.pdb pdb\x86\
xcopy /y /f Windows\x64\Release\supercan_srv64.pdb pdb\x64\
FOR /F "delims=" %%i IN ('git rev-parse --short HEAD') DO echo #define SC_COMMIT "%%i" >Windows\python\commit.h
xcopy /y /f /s Windows\python python\
xcopy /y /f Windows\dll\supercan_dll.c python\

(7z a -t7z -m0=lzma -mx=9 -mfb=64 -md=32m -ms=on supercan-win.7z bin lib inc src pdb python LICENSE COMMIT) || exit /b !ERRORLEVEL!
REM installer
(call "%VCVARS64%" x64 && makensis !NSIS_SC_VERSION_ARGS! Windows\NSIS\supercan.nsi) || exit /b !ERRORLEVEL!
move Windows\NSIS\supercan_inst.exe .