@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM Builds and deploys the native C++ FO4RecordEditor host.
REM Requires Visual Studio 2022 C++ tools, CMake, Python 3, Node/npm, and internet
REM access for pinned CMake FetchContent dependencies. No .NET SDK/runtime is used.

set "ROOT=%~dp0"
set "NATIVE_BUILD=%ROOT%native\build\publish-win-x64"
set "DEPLOY=E:\Modlists\Fallen World Alpha 2\Tools\FO4Editor"

pushd "%ROOT%"

REM Stop the launch-point binary so Windows does not keep the old exe locked.
taskkill /IM FO4RecordEditor.exe /F >nul 2>nul

if exist "web\package-lock.json" (
  pushd web
  call npm ci
  if errorlevel 1 ( popd & echo WEB DEPENDENCY INSTALL FAILED. & popd & exit /b 1 )
  call npm run build
  if errorlevel 1 ( popd & echo WEB BUILD FAILED. & popd & exit /b 1 )
  popd
) else (
  pushd web
  call npm install
  if errorlevel 1 ( popd & echo WEB DEPENDENCY INSTALL FAILED. & popd & exit /b 1 )
  call npm run build
  if errorlevel 1 ( popd & echo WEB BUILD FAILED. & popd & exit /b 1 )
  popd
)

if not exist "web\dist\" (
  echo web\dist was not produced.
  popd
  exit /b 1
)

echo Configuring native C++ host...
cmake -S native -B "%NATIVE_BUILD%" -G "Visual Studio 17 2022" -A x64 ^
  -DFO4IDE_BUILD_TESTS=OFF ^
  -DFO4IDE_BUILD_HTTP=ON ^
  -DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON
if errorlevel 1 (
  echo NATIVE CMAKE CONFIGURE FAILED.
  popd
  exit /b 1
)

echo Building native C++ host...
cmake --build "%NATIVE_BUILD%" --config Release --target fo4ide
if errorlevel 1 (
  echo.
  echo ============================================================
  echo NATIVE BUILD FAILED -- FO4RecordEditor.exe was NOT produced.
  echo ============================================================
  popd
  exit /b 1
)

set "NATIVE_EXE="
for /r "%NATIVE_BUILD%" %%F in (FO4RecordEditor.exe) do set "NATIVE_EXE=%%~fF"
if not defined NATIVE_EXE (
  echo Native build completed but FO4RecordEditor.exe was not found.
  popd
  exit /b 1
)

echo Native executable: !NATIVE_EXE!

if exist "%DEPLOY%\" (
  echo Deploying to MO2 Tools folder: %DEPLOY%
  copy /Y "!NATIVE_EXE!" "%DEPLOY%\FO4RecordEditor.exe" >nul
  if errorlevel 1 (
    echo Failed to copy FO4RecordEditor.exe to %DEPLOY%.
    popd
    exit /b 1
  )

  if exist "%DEPLOY%\web\dist\" rmdir /S /Q "%DEPLOY%\web\dist"
  mkdir "%DEPLOY%\web\dist" >nul 2>nul
  robocopy "web\dist" "%DEPLOY%\web\dist" /MIR /NJH /NJS /NDL /NC /NS /NP >nul
  if errorlevel 8 (
    echo Failed to deploy web\dist.
    popd
    exit /b 1
  )
) else (
  echo WARNING: deploy folder does not exist: %DEPLOY%
)

echo.
echo ============================================================
echo Native FO4RecordEditor published successfully.
echo Executable: !NATIVE_EXE!
echo No .NET runtime is required.
echo Launch from MO2: %DEPLOY%\FO4RecordEditor.exe
echo ============================================================

popd
endlocal