using namespace System.IO;

# See also: VPC_Keyword_Shaders in utils\vpc\vpc\projectscript.cpp


$ShaderFolder = "$PSScriptRoot\materialsystem\stdshaders"

$ShaderModelMapping = @{
    ps2x = @("ps20", "ps20b");
    vsxx = "vs20"
}

$ShaderModelMapping_Force3_0 = @{
    ps2x = "ps30"; ps20 = "ps30"; ps20b = "ps30";
    vsxx = "vs30"; vs20 = "vs30"
}

function Get-ShaderType([string]$ShaderPath) {
    if($ShaderPath -match "\.(fxc|vsh|psh)") {
        $Matches.1
    } else {
        throw "Passed not a shader"
    }
}

class ShaderCompilation {
    [string]$File
    [string]$File_ShaderModel
    [string]$CompileAs
    [string]$CompileAs_ShaderModel
}


# Based on LoadShaderListFile from valve_perl_helpers.pl
function Parse-ShaderFileLine([string]$Line, [hashtable]$ShaderModelMapping) {
    $InShader = $Line
    $ShaderModel = [regex]::Match($InShader, "_([a-zA-Z0-9]+?)\.(?:fxc|vsh|psh)").Groups[1].Value
    #if($null -eq $ShaderModel -or $ShaderModel -eq "") { 
    #    throw ("No shader model in shader name $Line") # Original code actually allows this.
    #}

    $Replacement = $ShaderModelMapping[$ShaderModel]

    if($null -eq $Replacement) {
        [ShaderCompilation]@{
            File = $InShader; File_ShaderModel = $ShaderModel;
            CompileAs = $InShader; CompileAs_ShaderModel = $ShaderModel; 
        }
    } else {
        $Replacement | % {
            [ShaderCompilation]@{
                File = $InShader; File_ShaderModel = $ShaderModel;
                CompileAs = $InShader -replace "$ShaderModel\.","$_.";
                    CompileAs_ShaderModel = $_
            }
        }
    }
}

#Parse-ShaderFileLine "ShaderModel.psh" $ShaderModelMapping

#return;

# Based on LoadShaderListFile from valve_perl_helpers.pl
function Parse-ShaderFile([FileInfo]$File, [switch]$ForceModel3_0) {
    $InShaders = Get-Content -Path $File.FullName `
        | % { $_ -ireplace "\/\/.*$","" -replace "\s*",""} `
        | ? { $_ -ne "" }

    $ShaderModelMapping = If ($ForceModel3_0) { $ShaderModelMapping_Force3_0 } else { $ShaderModelMapping }

    $InShaders | % { Parse-ShaderFileLine $_ $ShaderModelMapping }
}

[hashtable]$ShaderContents = @{}

# Returns lines of the shader
function Flatten-ShaderIncludes([FileInfo]$Shader, [string]$RootDir) {
    if($ShaderContents -contains $Shader) { return $ShaderContents[$Shader]; }

    $Data = @(
        Get-Content -Path $Shader.FullName | % {
            if($_ -match '#include\s+"(.*?)"') {
                $Include = [FileInfo]::new("$RootDir\$($Matches[1])")

                Flatten-ShaderIncludes $Include $RootDir
            } else {
                $_
            }
        }
    )

    $ShaderContents[$Shader] = $Data
    $Data
}

function Parse-ShaderParamModifiers([string]$ParamsAndConds, [ShaderCompilation]$Compilation) {
    $ShadermodelNotPassed = $null
    $Initializer = $null

    while($ParamsAndConds -match "(.+)\[(.+?)\]\s*$") { # Strip rightmost condition and previous text
        $ParamsAndConds = $Matches.1

        $Condition = $Matches.2

        switch -regex ($Condition) {
            # NOTE: I have no plans to support consoles, so conditions are stubs for now
            # However, conditions for Linux or OpenGL can be added in future.
            "PC" { continue }
            "CONSOLE" { return }
            "XBOX" { return  }
            "SONYPS3" { return }

            "((?:ps|vs)\d+\w?)" { # Shader type and shadermodel check
                $ShadermodelNotPassed = $false -or $ShadermodelNotPassed -or `
                    $Compilation.CompileAs_ShaderModel -ine $Matches.1

                continue
            }
            "\s*=\s*(.+?)\s*" {
                if($null -ne $Initializer) {
                    return $null, "Multiple initializers ([= any_cpp_expr]) are not supported"
                }
                $Initializer = $Matches.1
                continue
            }

            Default {
                return $null, "Unsupported condition $Condition"
            }
        }
    }

    # Neither $true nor $null
    if($ShadermodelNotPassed -eq $false) { return }

    return @{
        Params = $ParamsAndConds.Trim();
        Initializer = $Initializer;
    }
}



function Process-ShaderParams([ShaderCompilation]$Compilation) {
    process {
        $Line = $_

        if($_ -cnotmatch '^\s*\/\/\s*(STATIC|DYNAMIC|SKIP)\s*:(.*)$') { return; }

        $Mode = $Matches.1
        $ParamsAndConds = $Matches.2

        $Result, $ErrorInfo = Parse-ShaderParamModifiers $ParamsAndConds $Compilation
        if($null -eq $Result) {
            if($null -ne $ErrorInfo) {
               throw "Error parsing $Mode in $($Compilation.File) or its includes: $ErrorInfo\nLine: $Line"
            }    
            return
        }

        $Params = $Result.Params

        if($Mode -eq "STATIC" -or $Mode -eq "DYNAMIC") {
            if($Params -inotmatch '"(.*)"\s+"(\d+)\.\.(\d+)"') {
                throw "Error parsing $Mode in $($Compilation.File) or its includes: Syntax error\nLine: $Line"
            }

            @{
                Mode = $Mode;
                Name = $Matches[1];
                Min = [convert]::ToInt32($Matches[2]);
                Max = [convert]::ToInt32($Matches[3]);
                Initializer = $Result.Initializer;
            }

        } elseif($Mode -eq "SKIP") {
            @{
                Mode = $Mode;
                Expr = $Params
            }
        }
    }
}

function Generate-ComboVar([hashtable]$Combo) {
    $Name = $Combo.Name

    $Min = $Combo.Min
    $Max = $Combo.Max

    $ValueVar = "_$Name"
    $CheckVar = "_$($Name)_Set"

    @"

private: // Variable $Name
    int $ValueVar;
"@
    if($null -eq $Combo.Initializer) {
        @"
#ifdef _DEBUG
    bool $CheckVar = false;
#endif
"@
    }


@"

public:
    void Set$Name(int value) {
        Assert( value >= $Min && value <= $Max );
        $ValueVar = value;
"@ 
    if($null -eq $Combo.Initializer) {
@"
#ifdef _DEBUG
        bool $CheckVar = true;
#endif
    }
"@ 
    }
    @"
    void Set$Name(bool value) { Set$Name(value ? 1 : 0); }
"@
}

function Generate-DebugCheck([System.Collections.Specialized.OrderedDictionary]$Combos) {
    $Condition = [string]::Join(" && ", @($Combos.Values | ? {$null -eq $_.Initializer} | % {"_$($_.Name)_Set"}) )
    
    if($Condition -ne "") {
        "Assert($Condition);"
    } else {
        "// No Assert as there are no combos of this type"
    }
}


function Get-ShaderType-ComboInitChecker([string]$ShaderModel) {
    if($ShaderModel.StartsWith("vs")) {
        return "vsh"
    } elseif($ShaderModel.StartsWith("ps")) {
        return "psh"
    } else {
        throw ("Invalid shader model $ShaderModel")
    }
}

function Generate-ComboInitChecker([System.Collections.Specialized.OrderedDictionary]$Combos, [string]$ComboSet, [string]$ShaderType) {
    $Result = [string]::Join(" + ", @(
        $Combos.Values | % {"forgot_to_set_$($ShaderType)_$($_.Name)"}
    ))

    if($Result -eq "") { $Result = "0" }

    $Result
}

function Generate-ComboHelperCtor([string]$Class, [System.Collections.Specialized.OrderedDictionary]$Combos) {

    "    $Class( IShaderShadow *pShaderShadow, IMaterialVar **params ) {"
    
    $Combos.Values | ? { $null -ne $_.Initializer} | % {
        "        _$($_.Name) = $($_.Initializer);"
    }


    "    }"
}

function Generate-StaticComboHelper(
    [string]$ShaderName, 
    [System.Collections.Specialized.OrderedDictionary]$Statics, 
    [System.Collections.Specialized.OrderedDictionary]$Dynamics,
    [string]$ShaderType) {

    $Class = "$($ShaderName)_Static_Index"
    $Scale = 1

    @"
class $Class {
"@
    $Statics.Values | % { Generate-ComboVar $_ }
    "public:\n"
    Generate-ComboHelperCtor $Class $Statics
    @"

    int GetIndex() {
        #ifdef _DEBUG
            $(Generate-DebugCheck $Statics)
        #endif

        int value = 0;
"@
    foreach($Combo in $Dynamics.Values) {$Scale *= $Combo.Max - $Combo.Min + 1}
    
    foreach($Combo in $Statics.Values) {
    @"
        value += $Scale * _$($Combo.Name);
"@
        $Scale *= $Combo.Max - $Combo.Min + 1;
    }

    @"
        return value;
    }
}

#define shaderStaticTest_$ShaderName $(Generate-ComboInitChecker $Statics -ComboSet "static" $ShaderType)
"@
}

function Generate-DynamicComboHelper(
    [string]$ShaderName,
    [System.Collections.Specialized.OrderedDictionary]$Dynamics,
    [string]$ShaderType) {

    $Class = "$($ShaderName)_Dynamic_Index"
    $Scale = 1
    
    @"
class $Class {
"@
    $Dynamics.Values | % { Generate-ComboVar $_ }
    "public:\n"
    Generate-ComboHelperCtor $Class $Dynamics
    @"

    int GetIndex() {
        #ifdef _DEBUG
            $(Generate-DebugCheck $Dynamics)
        #endif

        int value = 0;
"@
    foreach($Combo in $Dynamics.Values) {
    @"
        value += $Scale * _$($Combo.Name);
"@
        $Scale *= $Combo.Max - $Combo.Min + 1;
    }

    @"
        return value;
    }
}

#define shaderDynamicTest_$ShaderName $(Generate-ComboInitChecker $Dynamics -ComboSet "static" $ShaderType)
"@
}

function Generate-ShaderCppHeader(
    [ShaderCompilation]$Comp, 
    [System.Collections.Specialized.OrderedDictionary]$Statics, 
    [System.Collections.Specialized.OrderedDictionary]$Dynamics, 
    [System.Collections.ArrayList]$Skips) {

    [int64]$NumCombos = 1
    foreach($St in $Statics.Values) { $NumCombos *= ($St.Max - $St.Min + 1) }
    foreach($Dyn in $Dynamics.Values) { $NumCombos *= ($St.Max - $St.Min + 1) }

    $ShaderName = [Path]::GetFileNameWithoutExtension($Comp.CompileAs)
    $ShaderType = Get-ShaderType-ComboInitChecker($Comp.CompileAs_ShaderModel)

    #Write-Host $NumCombos

    if($NumCombos -gt [int32]::MaxValue) {
        throw "Shader combo amount not fits into positive int32 (amount $NumCombos > $([int32]::MaxValue), shader $($Comp.CompileAs))"
    }

    
    @" 
#include "shaderlib/cshader.h"

// This shader has $NumCombos combos total, $($Statics.Count) statics and $($Dynamics.Count) dynamics.
// List of SKIPs that affect this shader:
"@
    $Skips | % {"//    $_"}
    ""
    ""
    Generate-StaticComboHelper $ShaderName $Statics $Dynamics $ShaderType
    ""
    Generate-DynamicComboHelper $ShaderName $Dynamics $ShaderType
}

function Build-ShadersFromFile([FileInfo]$File, [switch]$ForceModel3_0) {
    $ShaderCompilations = Parse-ShaderFile $File $ForceModel3_0

    New-Item -ItemType Directory -Path "$ShaderFolder\fxctmp9" -Force | Out-Null

    $ShaderCompilations | % {
        [ShaderCompilation]$ShComp = $_

        [FileInfo]$File = [FileInfo]::new("$ShaderFolder\$($ShComp.File)")

        if ($File.Extension -ine ".fxc") {
            Write-Host "Skipping ($($ShComp.File) as $($ShComp.CompileAs)) as non-FXC files are not supported"
            return;
        }

        $Statics = [ordered]@{}
        $Dynamics = [ordered]@{}
        $Skips = [System.Collections.ArrayList]@()

        foreach ($shparam in Flatten-ShaderIncludes $File $ShaderFolder | Process-ShaderParams $ShComp) {
            switch($shparam.Mode) {
                "STATIC" { $Statics[$shparam.Name] = $shparam; continue }
                "DYNAMIC" { $Dynamics[$shparam.Name] = $shparam; continue }
                "SKIP" { $Skips += $shparam.Expr -replace '\$',''; continue }
            }
        }

        $CppHeaderPath = "$ShaderFolder\fxctmp9\$([Path]::GetFileNameWithoutExtension($ShComp.CompileAs)).inc"

        Generate-ShaderCppHeader $ShComp $Statics $Dynamics $Skips | Out-File $CppHeaderPath

        Write-Host "Generated $CppHeaderPath; Statics: $($Statics.Count), Dynamics: $($Dynamics.Count), Skips: $($Skips.Count)"
        

        # For non-PSH shaders: add include file path to inclist
        # For non-PSH shaders: add its compilation to default dependency

        # For non-FXC shaders: check CRC, if not passes, add its compilation to default dependency and add result to copylist

        # Get list of #includes recursively
        # Add compilation entry:
        # - depends on compilation scripts, shader file itself and its #includes
        # - runs compilation script (fxc_prep.pl -novcs || vsh_prep.pl) -source $(Dir) $_
        # - add shader and its #includes to copylist
    }
}


Write-Host "Started building all shaders"

Build-ShadersFromFile(".\materialsystem\stdshaders\stdshader_dx9_20b.txt")
Build-ShadersFromFile(".\materialsystem\stdshaders\stdshader_dx9_20b_new.txt") # -dx9_30
Build-ShadersFromFile(".\materialsystem\stdshaders\stdshader_dx9_30.txt") -ForceModel3_0 # -dx9_30	-force30
#Build-ShadersFromFile(".\materialsystem\stdshaders\stdshader_dx10.txt") # -dx10

Write-Host "Finished building all shaders"