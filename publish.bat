@echo off
REM Builds a self-contained single-file FO4RecordEditor.exe (no .NET install required).
REM Drop the resulting exe anywhere and add it to MO2 as an executable.

dotnet publish FO4RecordEditor\FO4RecordEditor.csproj ^
  -c Release -r win-x64 --self-contained true ^
  -p:PublishSingleFile=true ^
  -p:IncludeNativeLibrariesForSelfExtract=true ^
  -p:EnableCompressionInSingleFile=true ^
  -p:DebugType=none

echo.
echo ============================================================
echo Published exe:
echo   FO4RecordEditor\bin\Release\net9.0-windows\win-x64\publish\FO4RecordEditor.exe
echo ============================================================
