@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo === 像素闯关 编译（必须在「本文件夹」里的 src\main.cpp）===
set "OUT_DIR=%~dp0build_msvc"
set "EXE=%OUT_DIR%\Release\pixel_escape.exe"
rem 生成器名含空格，必须整行加引号；且勿用 "路径\" 这种结尾，否则与引号冲突

where cmake >nul 2>&1
if errorlevel 1 (
  if exist "C:\Program Files\CMake\bin\cmake.exe" set "PATH=C:\Program Files\CMake\bin;%PATH%"
)

where cmake >nul 2>&1
if errorlevel 1 (
  echo [错误] 找不到 cmake.exe。请安装 CMake 或把 cmake 加入 PATH。
  pause
  exit /b 1
)

call :find_vcvars
if not defined VCVARS call :try_vs2022
if not defined VCVARS (
  echo [错误] 找不到 vcvars64.bat。请安装「Visual Studio 2022」并勾选「使用 C++ 的桌面开发」。
  pause
  exit /b 1
)

call "%VCVARS%"
if errorlevel 1 (
  echo [错误] vcvars 执行失败。
  pause
  exit /b 1
)

cmake -S "%CD%" -B "%OUT_DIR%" -G "Visual Studio 17 2022" -A x64
if errorlevel 1 ( pause & exit /b 1 )

cmake --build "%OUT_DIR%" --config Release
if errorlevel 1 ( pause & exit /b 1 )

if not exist "%EXE%" (
  echo [错误] 未找到 %EXE%
  pause
  exit /b 1
)

copy /Y "%EXE%" "%~dp0pixel_escape_奶龙版.exe" >nul
echo.
echo 已生成: %EXE%
echo 已复制到: %~dp0pixel_escape_奶龙版.exe
echo 请运行 **本目录下** 的 pixel_escape_奶龙版.exe
start "" "%~dp0pixel_escape_奶龙版.exe"
exit /b 0

:find_vcvars
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
goto :eof

:try_vs2022
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do (
  if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
)
goto :eof
