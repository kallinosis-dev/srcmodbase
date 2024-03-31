@echo off

set TTEXE=..\..\devtools\bin\timeprecise.exe
if not exist %TTEXE% goto no_ttexe
goto no_ttexe_end

:no_ttexe
set TTEXE=time /t
:no_ttexe_end

echo.
rem echo ==================== buildshaders %* ==================
%TTEXE% -cur-Q
set tt_start=%ERRORLEVEL%
set tt_chkpt=%tt_start%


REM ****************
REM usage: buildshaders <shaderProjectName> [-x360|-ps3]
REM ****************

setlocal
set arg_filename=%1
rem set shadercompilecommand=echo shadercompile.exe -mpi_graphics -mpi_TrackEvents
set shadercompilecommand=shadercompile.exe
set shadercompileworkers=140
set targetdir=..\..\..\game\platform\shaders
set SrcDirBase=..\..
set ChangeToDir=../../../game/bin
set shaderDir=shaders
set SDKArgs=
set SHADERINCPATH=vshtmp9/... fxctmp9/...

if "%1" == "" goto usage
set inputbase=%1

if /i "%3" == "-force30" goto set_force30_arg
goto set_force_end
:set_force30_arg
			set DIRECTX_FORCE_MODEL=30
			goto set_force_end
:set_force_end

if /i "%2" == "-game" goto set_mod_args
if /i "%2" == "-nompi" set SDKArgs=-nompi
goto build_shaders

REM ****************
REM USAGE
REM ****************
:usage
echo.
echo "usage: buildshaders <shaderProjectName> [-ps3 or -x360 or -dx10 or -game] [gameDir if -game was specified] [-nompi if -ps3 or -x360 was specified] [-source sourceDir]"
echo "       gameDir is where gameinfo.txt is (where it will store the compiled shaders)."
echo "       sourceDir is where the source code is (where it will find scripts and compilers)."
echo "ex   : buildshaders myshaders"
echo "ex   : buildshaders myshaders -game c:\steam\steamapps\sourcemods\mymod -source c:\mymod\src"
echo "ex   : buildshaders myshaders -x360 -nompi"
echo.
echo "PS3 specific parameters (mutually exclusive, and must directly follow the -ps3 parameter):"
echo "-ps3debug - Generate PS3 debug information (only do this if you have an SSD - see the wiki)"
echo "-ps3scheduleoptimization - Find optimal fragment shader compiler scheduler settings (this takes a LONG time!)"
goto :end

REM ****************
REM MOD ARGS - look for -game or the vproject environment variable
REM ****************
:set_mod_args

if not exist %sourcesdk%\bin\shadercompile.exe goto NoShaderCompile
set ChangeToDir=%sourcesdk%\bin

if /i "%4" NEQ "-source" goto NoSourceDirSpecified
set SrcDirBase=%~5

REM ** use the -game parameter to tell us where to put the files
set targetdir=%~3\shaders
set SDKArgs=-nompi -game "%~3"

if not exist "%~3\gameinfo.txt" goto InvalidGameDirectory
goto build_shaders

REM ****************
REM ERRORS
REM ****************
:InvalidGameDirectory
echo -
echo Error: "%~3" is not a valid game directory.
echo (The -game directory must have a gameinfo.txt file)
echo -
goto end

:NoSourceDirSpecified
echo ERROR: If you specify -game on the command line, you must specify -source.
goto usage
goto end

:NoShaderCompile
echo -
echo - ERROR: shadercompile.exe doesn't exist in %sourcesdk%\bin
echo -
goto end

REM ****************
REM BUILD SHADERS
REM ****************
:build_shaders

rem echo --------------------------------
rem echo %inputbase%
rem echo --------------------------------
REM make sure that target dirs exist
REM files will be built in these targets and copied to their final destination
if not exist %shaderDir% mkdir %shaderDir%
if not exist %shaderDir%\fxc mkdir %shaderDir%\fxc
if not exist %shaderDir%\vsh mkdir %shaderDir%\vsh
if not exist %shaderDir%\psh mkdir %shaderDir%\psh
REM Nuke some files that we will add to later.

if exist inclist.txt del /f /q inclist.txt

REM ****************
REM Generate a makefile for the shader project
REM ****************
perl "%SrcDirBase%\devtools\bin\updateshaders.pl" -source "%SrcDirBase%" %inputbase%


REM ****************
REM Run the makefile, generating minimal work/build list for fxc files, go ahead and compile vsh and psh files.
REM ****************
rem nmake /S /C -f makefile.%inputbase% clean > clean.txt 2>&1
echo Building inc files, asm vcs files, and VMPI worklist for %inputbase%...
nmake /S /C -f makefile.%inputbase%

REM ****************
REM Copy the inc files to their target
REM ****************
if exist "inclist.txt" (
	echo Publishing shader inc files to target...
	perl %SrcDirBase%\devtools\bin\copyshaderincfiles.pl inclist.txt
)

REM ****************
REM END
REM ****************
:end


%TTEXE% -diff %tt_start%
echo.

