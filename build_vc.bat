@echo off
@setlocal enableextensions
rem @cd /d "%~dp0\..\"

rem VS 2026

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VCVARS="

if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -version "[18.0,19.0)" -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvarsall.bat"

if not defined VCVARS for %%e in (Community Professional Enterprise BuildTools) do if exist "%ProgramFiles%\Microsoft Visual Studio\18\%%e\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\18\%%e\VC\Auxiliary\Build\vcvarsall.bat"

if not exist "%VCVARS%" goto notfound

rem every platform needs its own environment: the target include and lib paths are taken from vcvarsall
call "%VCVARS%" amd64

rem without OutDir both builds land in bin\64 and the second one overwrites the first
msbuild memreduct.sln -property:Configuration=Release -property:Platform=x64 -property:OutDir=bin\64\ -property:IntDir=obj\64\ -verbosity:normal
if %ERRORLEVEL% neq 0 goto failed

call "%VCVARS%" amd64_x86

msbuild memreduct.sln -property:Configuration=Release -property:Platform=Win32 -property:OutDir=bin\32\ -property:IntDir=obj\32\ -verbosity:normal
if %ERRORLEVEL% neq 0 goto failed

set "EXITCODE=0"
goto end

:notfound

echo Visual Studio 2026 (18.x) was not found...
set "EXITCODE=1"
goto end

:failed

echo build failed...
set "EXITCODE=1"
goto end

:end

echo done...

pause

exit /b %EXITCODE%
