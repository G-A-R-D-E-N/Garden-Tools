@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM Builds and deploys the native C++ FO4RecordEditor host.
REM Requires Visual Studio 2022 C++ tools, CMake, Python 3, Node/npm, and internet
REM access for pinned CMake FetchContent dependencies. No .NET SDK/runtime is used.
REM Usage: publish.bat [deploy-folder]. The folder can also come from FO4IDE_DEPLOY_DIR; with neither, the build is made but not copied anywhere.

set "ROOT=%~dp0"
set "NATIVE_BUILD=%ROOT%native\build\publish-win-x64"
set "DEPLOY=%~1"
if not defined DEPLOY set "DEPLOY=%FO4IDE_DEPLOY_DIR%"

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

if not defined DEPLOY (
  echo No deploy folder given; skipping the deploy step. Pass one as the first argument or set FO4IDE_DEPLOY_DIR.
) else if exist "%DEPLOY%\" (
  echo Deploying to MO2 Tools folder: !DEPLOY!
  copy /Y "!NATIVE_EXE!" "%DEPLOY%\FO4RecordEditor.exe" >nul
  if errorlevel 1 (
    echo Failed to copy FO4RecordEditor.exe to !DEPLOY!.
    popd
    exit /b 1
  )

  REM The editor statically links Radicle, so radicle.dll is not a dependency of
  REM the exe. It is the SDK surface other consumers load by name, so ship it too.
  echo Building the Radicle C ABI shared library...
  cmake --build "%NATIVE_BUILD%" --config Release --target radicle_c
  if errorlevel 1 (
    echo RADICLE C ABI BUILD FAILED.
    popd
    exit /b 1
  )

  set "RADICLE_DLL="
  for /r "%NATIVE_BUILD%" %%F in (radicle.dll) do set "RADICLE_DLL=%%~fF"
  if not defined RADICLE_DLL (
    echo radicle_c built but radicle.dll was not found.
    popd
    exit /b 1
  )

  copy /Y "!RADICLE_DLL!" "%DEPLOY%\radicle.dll" >nul
  if errorlevel 1 (
    echo Failed to copy radicle.dll to !DEPLOY!.
    popd
    exit /b 1
  )

  if exist "%DEPLOY%\sdk\include\fo4\" rmdir /S /Q "%DEPLOY%\sdk\include\fo4"
  mkdir "%DEPLOY%\sdk\include\fo4" >nul 2>nul
  copy /Y "%ROOT%..\Radicle\include\fo4\c_api*.h" "%DEPLOY%\sdk\include\fo4\" >nul
  if errorlevel 1 (
    echo Failed to copy the Radicle C ABI headers to !DEPLOY!.
    popd
    exit /b 1
  )
  mkdir "%DEPLOY%\sdk\lib" >nul 2>nul
  copy /Y "%NATIVE_BUILD%\lib\Release\radicle.lib" "%DEPLOY%\sdk\lib\radicle.lib" >nul
  if errorlevel 1 (
    echo WARNING: radicle.lib was not found; C++ consumers will need it from a local build.
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
  echo Deploy folder does not exist: !DEPLOY!
  popd
  exit /b 1
)

echo.
echo ============================================================
echo Native FO4RecordEditor published successfully.
echo Executable: !NATIVE_EXE!
echo No .NET runtime is required.
if defined DEPLOY if exist "%DEPLOY%\" echo Launch from MO2: "%DEPLOY%\FO4RecordEditor.exe"
echo ============================================================

popd
endlocal