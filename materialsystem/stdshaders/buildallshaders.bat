@echo off
setlocal

set sourcedir="shaders"
set targetdir="..\..\..\game\platform\shaders"

set BUILD_SHADER=call buildshaders.bat

set ARG_EXTRA=

if /i "%2" == "-nompi" set ARG_EXTRA=-nompi

%BUILD_SHADER% stdshader_dx9_20b %ARG_EXTRA%
%BUILD_SHADER% stdshader_dx9_20b_new	-dx9_30 %ARG_EXTRA%
%BUILD_SHADER% stdshader_dx9_30		-dx9_30	-force30 %ARG_EXTRA%
rem %BUILD_SHADER% stdshader_dx10     -dx10 %ARG_EXTRA%