BEGIN {use File::Basename; push @INC, dirname($0); }
require "valve_perl_helpers.pl";

sub ReadInputFile
{
	local( $filename ) = shift;
	local( *INPUT );
	local( @output );
	open INPUT, "<$filename" || die;

	local( $line );
	local( $linenum ) = 1;
	while( $line = <INPUT> )
	{
#		print "LINE: $line";
#		$line =~ s/\n//g;
#		local( $postfix ) = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";
#		$postfix .= "; LINEINFO($filename)($linenum)\n";
		if( $line =~ m/\#include\s+\"(.*)\"/i )
		{
			push @output, &ReadInputFile( $1 );
		}
		else
		{
#			push @output, $line . $postfix;
			push @output, $line;
		}
		$linenum++;
	}

	close INPUT;
#	print "-----------------\n";
#	print @output;
#	print "-----------------\n";
	return @output;
}

$dynamic_compile = 1;
$generateListingFile = 0;
$spewCombos = 0;

@startTimes = times;
$startTime = time;

$g_produceCppClasses = 1;

$g_sfm = 1;

while( 1 )
{
	$fxc_filename = shift;
	if( $fxc_filename =~ m/-source/ )
	{
		shift;
	}
	elsif( $fxc_filename =~ m/-nv3x/i )
	{
		$nvidia = 1;
	}
	elsif( $fxc_filename =~ m/-ps20a/i )
	{
		$ps2a = 1;
	}
	elsif( $fxc_filename =~ m/-nocpp/i )
	{
		$g_produceCppClasses = 0;
	}
	else
	{
		last;
	}
}

$argstring = $fxc_filename;
$fxc_basename = $fxc_filename;
$fxc_basename =~ s/^.*-----//;
$fxc_filename =~ s/-----.*$//;

$debug = 0;
$forcehalf = 0;

sub ToUpper
{
	local( $in ) = shift;
	$in =~ tr/a-z/A-Z/;
	return $in;
}

sub CreateCCodeToSpewDynamicCombo
{
	local( $out ) = "";

	$out .= "\t\tOutputDebugString( \"src:$fxc_filename vcs:$fxc_basename dynamic index\" );\n";
	$out .= "\t\tchar tmp[128];\n";
	$out .= "\t\tint shaderID = ";
	local( $scale ) = 1;
	for( $i = 0; $i < scalar( @dynamicDefineNames ); $i++ )
	{
		local( $name ) = @dynamicDefineNames[$i];
		local( $varname ) = "m_n" . $name;
		$out .= "( $scale * $varname ) + ";
		$scale *= $dynamicDefineMax[$i] - $dynamicDefineMin[$i] + 1;
	}
	$out .= "0;\n";
	if( scalar( @dynamicDefineNames ) + scalar( @staticDefineNames ) > 0 )
	{
		$out .= "\t\tint nCombo = shaderID;\n";
	}
	
	my $type = GetShaderType( $fxc_filename );
	for( $i = 0; $i < scalar( @dynamicDefineNames ); $i++ )
	{
		$out .= "\t\tint n$dynamicDefineNames[$i] = nCombo % ";
		$out .= ( $dynamicDefineMax[$i] - $dynamicDefineMin[$i] + 1 ) + $dynamicDefineMin[$i];
		$out .= ";\n";

		$out .= "\t\tsprintf( tmp, \"\%d\", n$dynamicDefineNames[$i] );\n";
		$out .= "\t\tOutputDebugString( \" $dynamicDefineNames[$i]";
		$out .= "=\" );\n";
		$out .= "\t\tOutputDebugString( tmp );\n";

		$out .= "\t\tnCombo = nCombo / " . ( $dynamicDefineMax[$i] - $dynamicDefineMin[$i] + 1 ) . ";\n";
		$out .= "\n";
	}
	$out .= "\t\tOutputDebugString( \"\\n\" );\n";
	return $out;
}

sub CreateCCodeToSpewStaticCombo
{
	local( $out ) = "";

	$out .= "\t\tOutputDebugString( \"src:$fxc_filename vcs:$fxc_basename static index\" );\n";
	$out .= "\t\tchar tmp[128];\n";
	$out .= "\t\tint shaderID = ";

	local( $scale ) = 1;
	for( $i = 0; $i < scalar( @dynamicDefineNames ); $i++ )
	{
		$scale *= $dynamicDefineMax[$i] - $dynamicDefineMin[$i] + 1;
	}
	for( $i = 0; $i < scalar( @staticDefineNames ); $i++ )
	{
		local( $name ) = @staticDefineNames[$i];
		local( $varname ) = "m_n" . $name;
		$out .= "( $scale * $varname ) + ";
		$scale *= $staticDefineMax[$i] - $staticDefineMin[$i] + 1;
	}
	$out .= "0;\n";

#	$out .= "\t\tsprintf( tmp, \"\%d\\n\", shaderID );\n";
#	$out .= "\t\tOutputDebugString( tmp );\n\n";
	if( scalar( @staticDefineNames ) + scalar( @staticDefineNames ) > 0 )
	{
		$out .= "\t\tint nCombo = shaderID;\n";
	}
	
	my $type = GetShaderType( $fxc_filename );
	for( $i = 0; $i < scalar( @dynamicDefineNames ); $i++ )
	{
		$out .= "\t\tnCombo = nCombo / " . ( $dynamicDefineMax[$i] - $dynamicDefineMin[$i] + 1 ) . ";\n";
	}
	for( $i = 0; $i < scalar( @staticDefineNames ); $i++ )
	{
		$out .= "\t\tint n$staticDefineNames[$i] = nCombo % ";
		$out .= ( $staticDefineMax[$i] - $staticDefineMin[$i] + 1 ) + $staticDefineMin[$i];
		$out .= ";\n";

		$out .= "\t\tsprintf( tmp, \"\%d\", n$staticDefineNames[$i] );\n";
		$out .= "\t\tOutputDebugString( \" $staticDefineNames[$i]";
		$out .= "=\" );\n";
		$out .= "\t\tOutputDebugString( tmp );\n";

		$out .= "\t\tnCombo = nCombo / " . ( $staticDefineMax[$i] - $staticDefineMin[$i] + 1 ) . ";\n";
		$out .= "\n";
	}
	$out .= "\t\tOutputDebugString( \"\\n\" );\n";
	return $out;
}

# This code is used to inject information about combo names, ranges, etc, into the inc file so that we can know what combo is skipped when we run into a skipped combo that is trying to be used.
sub CreateCFuntionToSpewSkippedCombo
{
	local( $out ) = "";

	if ( scalar( @dynamicDefineNames ) )
	{
		$out .= "\nstatic const ShaderComboInformation_t s_DynamicComboArray_" . $fxc_basename . "[" . scalar( @dynamicDefineNames ) . "] = \n{\n";
		for( $i = 0; $i < scalar( @dynamicDefineNames ); $i++ )
		{
			$out .= "\t{ \"$dynamicDefineNames[$i]\", $dynamicDefineMin[$i], $dynamicDefineMax[$i] },\n";
		}
		$out .= "};\n";
	}

	if ( scalar( @staticDefineNames ) )
	{
		$out .= "\nstatic const ShaderComboInformation_t s_StaticComboArray_" . $fxc_basename . "[" . scalar( @staticDefineNames ) . "] = \n{\n";
		for( $i = 0; $i < scalar( @staticDefineNames ); $i++ )
		{
			$out .= "\t{ \"$staticDefineNames[$i]\", $staticDefineMin[$i], $staticDefineMax[$i] },\n";
		}
		$out .= "};\n";
	}
	
	$out .= "static const ShaderComboSemantics_t $fxc_basename" . "_combos =\n";
	$out .= "{\n";
	$out .= "\t\"$fxc_basename\", ";
	
	if ( scalar( @dynamicDefineNames ) )
	{
		$out .= "s_DynamicComboArray_" . $fxc_basename . ", " . scalar( @dynamicDefineNames ) . ", ";
	}
	else
	{
		$out .= "NULL, 0, ";
	}
	
	if ( scalar( @staticDefineNames ) )
	{
		$out .= "s_StaticComboArray_" . $fxc_basename . ", " . scalar( @staticDefineNames ) . " ";
	}
	else
	{
		$out .= "NULL, 0 ";
	}

	$out .= "\n};\n";
	
	$out .= "\nclass ConstructMe_$fxc_basename\n";
	$out .= "{\n";
	$out .= "public:\n";
	$out .= "\tConstructMe_$fxc_basename" . "()\n";
	$out .= "\t{\n";

	$out .= "\t\tGetShaderDLL()->AddShaderComboInformation( &$fxc_basename" . "_combos );\n";
	$out .= "\t}\n";
	$out .= "};\n";
	
	$out .= "\nstatic ConstructMe_$fxc_basename s_ConstructMe_$fxc_basename;\n";

	return $out;
};

sub WriteHelperVar
{
	local( $name ) = shift;
	local( $min ) = shift;
	local( $max ) = shift;
	local( $varname ) = "m_n" . $name;
	local( $boolname ) = "m_b" . $name;
	push @outputHeader, "private:\n";
	push @outputHeader, "\tint $varname;\n";
	push @outputHeader, "#ifdef _DEBUG\n";
	push @outputHeader, "\tbool $boolname;\n";
	push @outputHeader, "#endif\n";
	push @outputHeader, "public:\n";
	# int version of set function
	push @outputHeader, "\tvoid Set" . $name . "( int i )\n";
	push @outputHeader, "\t{\n";
	push @outputHeader, "\t\tAssert( i >= $min && i <= $max );\n";
	push @outputHeader, "\t\t$varname = i;\n";
	push @outputHeader, "#ifdef _DEBUG\n";
	push @outputHeader, "\t\t$boolname = true;\n";
	push @outputHeader, "#endif\n";
	push @outputHeader, "\t}\n";
	# bool version of set function
	push @outputHeader, "\tvoid Set" . $name . "( bool i )\n";
	push @outputHeader, "\t{\n";
	push @outputHeader, "\t\tAssert( ( i ? 1 : 0 ) >= $min && ( i ? 1 : 0 ) <= $max );\n";
	push @outputHeader, "\t\t$varname = i ? 1 : 0;\n";
	push @outputHeader, "#ifdef _DEBUG\n";
	push @outputHeader, "\t\t$boolname = true;\n";
	push @outputHeader, "#endif\n";
	push @outputHeader, "\t}\n";
}

sub WriteStaticBoolExpression
{
	local( $prefix ) = shift;
	local( $operator ) = shift;
	for( $i = 0; $i < scalar( @staticDefineNames ); $i++ )
	{
		if( $i )
		{
			push @outputHeader, " $operator ";
		}
		local( $name ) = @staticDefineNames[$i];
		local( $boolname ) = "m_b" . $name;
		push @outputHeader, "$prefix$boolname";
	}
	push @outputHeader, ";\n";
}

sub WriteDynamicBoolExpression
{
	local( $prefix ) = shift;
	local( $operator ) = shift;
	for( $i = 0; $i < scalar( @dynamicDefineNames ); $i++ )
	{
		if( $i )
		{
			push @outputHeader, " $operator ";
		}
		local( $name ) = @dynamicDefineNames[$i];
		local( $boolname ) = "m_b" . $name;
		push @outputHeader, "$prefix$boolname";
	}
	push @outputHeader, ";\n";
}

sub WriteDynamicHelperClasses
{
	local( $basename ) = $fxc_basename;
	$basename =~ tr/A-Z/a-z/;
	local( $classname ) = $basename . "_Dynamic_Index";
	push @outputHeader, "class $classname\n";
	push @outputHeader, "{\n";
	for( $i = 0; $i < scalar( @dynamicDefineNames ); $i++ )
	{
		$name = $dynamicDefineNames[$i];
		$min = $dynamicDefineMin[$i];
		$max = $dynamicDefineMax[$i];
		&WriteHelperVar( $name, $min, $max );
	}
	push @outputHeader, "public:\n";
#	push @outputHeader, "void SetPixelShaderIndex( IShaderAPI *pShaderAPI ) { pShaderAPI->SetPixelShaderIndex( GetIndex() ); }\n";
	# CONSTRUCTOR
	push @outputHeader, "\t// CONSTRUCTOR\n\t$classname( IShaderDynamicAPI *pShaderAPI )\n";
	push @outputHeader, "\t{\n";
	for( $i = 0; $i < scalar( @dynamicDefineNames ); $i++ )
	{
		local( $name ) = @dynamicDefineNames[$i];
		local( $boolname ) = "m_b" . $name;
		local( $varname ) = "m_n" . $name;
		if ( length( $dynamicDefineInit{$name} ) )
		{
			push @outputHeader, "#ifdef _DEBUG\n";
			push @outputHeader, "\t\t$boolname = true;\n";
			push @outputHeader, "#endif // _DEBUG\n";
			push @outputHeader, "\t\t$varname = $dynamicDefineInit{$name};\n";
		}
		else
		{
			push @outputHeader, "#ifdef _DEBUG\n";
			push @outputHeader, "\t\t$boolname = false;\n";
			push @outputHeader, "#endif // _DEBUG\n";
			push @outputHeader, "\t\t$varname = 0;\n";
		}
	}
	push @outputHeader, "\t}\n";
	push @outputHeader, "\tint GetIndex()\n";
	push @outputHeader, "\t{\n";
	push @outputHeader, "\t\t// Asserts to make sure that we aren't using any skipped combinations.\n";
	foreach $skip (@perlskipcodeindividual)
	{
		# can't do this static and dynamic can see each other.
#		$skip =~ s/\$/m_n/g;
#		$skip =~ s/defined//g;
#		push @outputHeader, "\t\tAssert( !( $skip ) );\n";
#		print "\t\tAssert( !( $skip ) );\n";
	}
	push @outputHeader, "\t\t// Asserts to make sure that we are setting all of the combination vars.\n";

	push @outputHeader, "#ifdef _DEBUG\n";
	if( scalar( @dynamicDefineNames ) > 0 )
	{
		push @outputHeader, "\t\tbool bAllDynamicVarsDefined = ";
		WriteDynamicBoolExpression( "", "&&" );
	}
	if( scalar( @dynamicDefineNames ) > 0 )
	{
		push @outputHeader, "\t\tAssert( bAllDynamicVarsDefined );\n";
	}
	push @outputHeader, "#endif // _DEBUG\n";

	if( $spewCombos && scalar( @dynamicDefineNames ) )
	{
		push @outputHeader, &CreateCCodeToSpewDynamicCombo();
	}
	push @outputHeader, "\t\treturn ";
	local( $scale ) = 1;
	for( $i = 0; $i < scalar( @dynamicDefineNames ); $i++ )
	{
		local( $name ) = @dynamicDefineNames[$i];
		local( $varname ) = "m_n" . $name;
		push @outputHeader, "( $scale * $varname ) + ";
		$scale *= $dynamicDefineMax[$i] - $dynamicDefineMin[$i] + 1;
	}
	push @outputHeader, "0;\n";
	push @outputHeader, "\t}\n";
	push @outputHeader, "};\n";
	push @outputHeader, "\#define shaderDynamicTest_" . $basename . " ";
	my $prefix;
	my $shaderType = &GetShaderType( $fxc_filename );
	
	if( $g_ps3 )
	{
		if( $shaderType =~ m/vp/i )
		{
			$prefix = "vsh_";
		}
		else
		{
			$prefix = "psh_";
		}
	}
	else
	{
		if( $shaderType =~ m/^vs/i )
		{
			$prefix = "vsh_";
		}
		else
		{
			$prefix = "psh_";
		}
	}
	for( $i = 0; $i < scalar( @dynamicDefineNames ); $i++ )
	{
		local( $name ) = @dynamicDefineNames[$i];
		if ( !length( $dynamicDefineInit{$name} ) )
		{
			push @outputHeader, $prefix . "forgot_to_set_dynamic_" . $name . " + ";
		}
	}
	push @outputHeader, "0\n";
}

sub WriteSkips
{
	my $skip;

	push @outputHeader, "// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!\n";
	foreach $skip (@perlskipcodeindividual)
	{
#		$skip =~ s/\$/m_n/g;
		push @outputHeader, "// $skip\n";
	}
}

sub WriteStaticHelperClasses
{
	local( $basename ) = $fxc_basename;
	$basename =~ tr/A-Z/a-z/;
	local( $classname ) = $basename . "_Static_Index";
	push @outputHeader, "#include \"shaderlib/cshader.h\"\n";
	push @outputHeader, "class $classname\n";
	push @outputHeader, "{\n";
	for( $i = 0; $i < scalar( @staticDefineNames ); $i++ )
	{
		$name = $staticDefineNames[$i];
		$min = $staticDefineMin[$i];
		$max = $staticDefineMax[$i];
		&WriteHelperVar( $name, $min, $max );
	}
	push @outputHeader, "public:\n";
#	push @outputHeader, "void SetShaderIndex( IShaderShadow *pShaderShadow ) { pShaderShadow->SetPixelShaderIndex( GetIndex() ); }\n";
	# WRITE THE CONSTRUCTOR
	push @outputHeader, "\t// CONSTRUCTOR\n\t$classname( IShaderShadow *pShaderShadow, IMaterialVar **params )\n";
	push @outputHeader, "\t{\n";
	for( $i = 0; $i < scalar( @staticDefineNames ); $i++ )
	{
		local( $name ) = @staticDefineNames[$i];
		local( $boolname ) = "m_b" . $name;
		local( $varname ) = "m_n" . $name;
		if ( length( $staticDefineInit{$name} ) )
		{
			push @outputHeader, "#ifdef _DEBUG\n";
			push @outputHeader, "\t\t$boolname = true;\n";
			push @outputHeader, "#endif // _DEBUG\n";
			push @outputHeader, "\t\t$varname = $staticDefineInit{$name};\n";
		}
		else
		{
			push @outputHeader, "#ifdef _DEBUG\n";
			push @outputHeader, "\t\t$boolname = false;\n";
			push @outputHeader, "#endif // _DEBUG\n";
			push @outputHeader, "\t\t$varname = 0;\n";
		}
	}
	push @outputHeader, "\t}\n";
	push @outputHeader, "\tint GetIndex()\n";
	push @outputHeader, "\t{\n";
	push @outputHeader, "\t\t// Asserts to make sure that we aren't using any skipped combinations.\n";
	foreach $skip (@perlskipcodeindividual)
	{
		$skip =~ s/\$/m_n/g;
#		push @outputHeader, "\t\tAssert( !( $skip ) );\n";
	}
	push @outputHeader, "\t\t// Asserts to make sure that we are setting all of the combination vars.\n";

	push @outputHeader, "#ifdef _DEBUG\n";
	if( scalar( @staticDefineNames ) > 0 )
	{
		push @outputHeader, "\t\tbool bAllStaticVarsDefined = ";
		WriteStaticBoolExpression( "", "&&" );

	}
	if( scalar( @staticDefineNames ) > 0 )
	{
		push @outputHeader, "\t\tAssert( bAllStaticVarsDefined );\n";
	}
	push @outputHeader, "#endif // _DEBUG\n";

	if( $spewCombos && scalar( @staticDefineNames ) )
	{
		push @outputHeader, &CreateCCodeToSpewStaticCombo();
	}
	push @outputHeader, "\t\treturn ";
	local( $scale ) = 1;
	for( $i = 0; $i < scalar( @dynamicDefineNames ); $i++ )
	{
		$scale *= $dynamicDefineMax[$i] - $dynamicDefineMin[$i] + 1;
	}
	for( $i = 0; $i < scalar( @staticDefineNames ); $i++ )
	{
		local( $name ) = @staticDefineNames[$i];
		local( $varname ) = "m_n" . $name;
		push @outputHeader, "( $scale * $varname ) + ";
		$scale *= $staticDefineMax[$i] - $staticDefineMin[$i] + 1;
	}
	push @outputHeader, "0;\n";
	if( $scale > 0x7fffffff )
	{
		$g_toomanycombos = 1;
		$g_totalcombos = $scale;
	}
	push @outputHeader, "\t}\n";
	push @outputHeader, "};\n";
	push @outputHeader, "\#define shaderStaticTest_" . $basename . " ";
	my $prefix;
	my $shaderType = &GetShaderType( $fxc_filename );
	if( $g_ps3 )
	{
		if( $shaderType =~ m/vp/i )
		{
			$prefix = "vsh_";
		}
		else
		{
			$prefix = "psh_";
		}
	}
	else
	{
		if( $shaderType =~ m/^vs/i )
		{
			$prefix = "vsh_";
		}
		else
		{
			$prefix = "psh_";
		}
	}
	for( $i = 0; $i < scalar( @staticDefineNames ); $i++ )
	{
		local( $name ) = @staticDefineNames[$i];
		if ( !length( $staticDefineInit{$name} ) )
		{
			push @outputHeader, $prefix . "forgot_to_set_static_" . $name . " + ";
		}
	}
	push @outputHeader, "0\n";
}

sub GetNewMainName
{
	local( $shadername ) = shift;
	local( $combo ) = shift;
	local( $i );
	$shadername =~ s/\./_/g;
	local( $name ) = $shadername;
	for( $i = 0; $i < scalar( @defineNames ); $i++ )
	{
		local( $val ) = ( $combo % ( $defineMax[$i] - $defineMin[$i] + 1 ) ) + $defineMin[$i];
		$name .= "_" . $defineNames[$i] . "_" . $val;
		$combo = $combo / ( $defineMax[$i] - $defineMin[$i] + 1 );
	}
#	return $name;
	return "main";
}

sub RenameMain
{
	local( $shadername ) = shift;
	local( $combo ) = shift;
	local( $name ) = &GetNewMainName( $shadername, $combo );
	if ( $g_ps3)
	{
		return "-e $name "
	}
	else
	{
		return "/Dmain=$name /E$name ";
	}
}

sub GetShaderType
{
	local( $shadername ) = shift; # hack - use global variables
	$shadername = $fxc_basename;
	
	if( $g_ps3 )
	{
		if( $shadername =~ m/_vs/i )
		{
			return "sce_vp_rsx"
		}
		elsif( $shadername =~ m/_ps/i )
		{
			return "sce_fp_rsx"
		}
		else
		{
			die "\n\nPS3 SHADERNAME = $shadername\n\n";
		}
	}
	elsif( $shadername =~ m/ps30/i )
	{
		if( $debug )
		{
			return "ps_3_sw";
		}
		else
		{
			return "ps_3_0";
		}
	}
	elsif( $shadername =~ m/ps20b/i )
	{
		return "ps_2_b";
	}
	elsif( $shadername =~ m/ps20/i )
	{
		if( $debug )
		{
			return "ps_2_sw";
		}
		else
		{
			if( $ps2a )
			{
				return "ps_2_a";
			}
			else
			{
				return "ps_2_0";
			}
		}
	}
	elsif( $shadername =~ m/ps14/i )
	{
		return "ps_1_4";
	}
	elsif( $shadername =~ m/ps11/i )
	{
		return "ps_1_1";
	}
	elsif( $shadername =~ m/vs30/i )
	{
		if( $debug )
		{
			return "vs_3_sw";
		}
		else
		{
			return "vs_3_0";
		}
	}
	elsif( $shadername =~ m/vs20/i )
	{
		if( $debug )
		{
			return "vs_2_sw";
		}
		else
		{
			return "vs_2_0";
		}
	}
	elsif( $shadername =~ m/vs14/i )
	{
		return "vs_1_1";
	}
	elsif( $shadername =~ m/vs11/i )
	{
		return "vs_1_1";
	}
	else
	{
		die "\n\nSHADERNAME = $shadername\n\n";
	}
}

sub CalcNumCombos
{
	local( $i, $numCombos );
	$numCombos = 1;
	for( $i = 0; $i < scalar( @dynamicDefineNames ); $i++ )
	{
		$numCombos *= $dynamicDefineMax[$i] - $dynamicDefineMin[$i] + 1;
	}
	for( $i = 0; $i < scalar( @staticDefineNames ); $i++ )
	{
		$numCombos *= $staticDefineMax[$i] - $staticDefineMin[$i] + 1;
	}
	return $numCombos;
}

sub CalcNumDynamicCombos
{
	local( $i, $numCombos );
	$numCombos = 1;
	for( $i = 0; $i < scalar( @dynamicDefineNames ); $i++ )
	{
		$numCombos *= $dynamicDefineMax[$i] - $dynamicDefineMin[$i] + 1;
	}
	return $numCombos;
}

sub CreateCFuncToCreateCompileCommandLine
{
	local( $out ) = "";

	$out .= "\t\tOutputDebugString( \"compiling src:$fxc_filename vcs:$fxc_basename \" );\n";
	$out .= "\t\tchar tmp[128];\n";
	$out .= "\t\tsprintf( tmp, \"\%d\\n\", shaderID );\n";
	$out .= "\t\tOutputDebugString( tmp );\n";
	$out .= "\t\tstatic PrecompiledShaderByteCode_t byteCode;\n";
	if( scalar( @dynamicDefineNames ) + scalar( @staticDefineNames ) > 0 )
	{
		$out .= "\t\tint nCombo = shaderID;\n";
	}
	
#	$out .= "\tvoid BuildCompileCommandLine( int nCombo, char *pResult, int maxLength )\n";
#	$out .= "\t{\n";
	$out .= "\t\tD3DXMACRO ";
	$out .= "defineMacros";
	$out .= "[";
	$out .= scalar( @dynamicDefineNames ) + scalar( @staticDefineNames ) + 1; # add 1 for null termination
	$out .= "];\n";
	if( scalar( @dynamicDefineNames ) + scalar( @staticDefineNames ) > 0 )
	{
		$out .= "\t\tchar tmpStringBuf[1024];\n";
		$out .= "\t\tchar *pTmpString = tmpStringBuf;\n\n";
	}

	local( $i );
	my $type = GetShaderType( $fxc_filename );
	for( $i = 0; $i < scalar( @dynamicDefineNames ); $i++ )
	{
		$out .= "\t\tsprintf( pTmpString, \"%d\", nCombo % ";
		$out .= ( $dynamicDefineMax[$i] - $dynamicDefineMin[$i] + 1 ) + $dynamicDefineMin[$i];
		$out .= " );\n";
		$out .= "\t\tdefineMacros";
		$out .= "[";
		$out .= $i;
		$out .= "]";
		$out .= "\.Name = ";
		$out .= "\"$dynamicDefineNames[$i]\";\n";

		$out .= "\t\tint n$dynamicDefineNames[$i] = nCombo % ";
		$out .= ( $dynamicDefineMax[$i] - $dynamicDefineMin[$i] + 1 ) + $dynamicDefineMin[$i];
		$out .= ";\n";
		$out .= "\t\tUNUSED( n$dynamicDefineNames[$i] );\n";

		$out .= "\t\tdefineMacros";
		$out .= "[";
		$out .= $i;
		$out .= "]";
		$out .= "\.Definition = ";
		$out .= "pTmpString;\n";
		$out .= "\t\tpTmpString += strlen( pTmpString ) + 1;\n";

		$out .= "\t\tsprintf( tmp, \"\%d\", n$dynamicDefineNames[$i] );\n";
		$out .= "\t\tOutputDebugString( \" $dynamicDefineNames[$i]";
		$out .= "=\" );\n";
		$out .= "\t\tOutputDebugString( tmp );\n";

		$out .= "\t\tnCombo = nCombo / " . ( $dynamicDefineMax[$i] - $dynamicDefineMin[$i] + 1 ) . ";\n";
		$out .= "\n";
	}
	for( $i = 0; $i < scalar( @staticDefineNames ); $i++ )
	{
		$out .= "\t\tsprintf( pTmpString, \"%d\", nCombo % ";
		$out .= ( $staticDefineMax[$i] - $staticDefineMin[$i] + 1 ) + $staticDefineMin[$i];
		$out .= " );\n";
		$out .= "\t\tdefineMacros";
		$out .= "[";
		$out .= $i + scalar( @dynamicDefineNames );
		$out .= "]";
		$out .= "\.Name = ";
		$out .= "\"$staticDefineNames[$i]\";\n";

		$out .= "\t\tint n$staticDefineNames[$i] = nCombo % ";
		$out .= ( $staticDefineMax[$i] - $staticDefineMin[$i] + 1 ) + $staticDefineMin[$i];
		$out .= ";\n";
		$out .= "\t\tUNUSED( n$staticDefineNames[$i] );\n";

		$out .= "\t\tdefineMacros";
		$out .= "[";
		$out .= $i + scalar( @dynamicDefineNames );
		$out .= "]";
		$out .= "\.Definition = ";
		$out .= "pTmpString;\n";
		$out .= "\t\tpTmpString += strlen( pTmpString ) + 1;\n";

		$out .= "\t\tsprintf( tmp, \"\%d\", n$staticDefineNames[$i] );\n";
		$out .= "\t\tOutputDebugString( \" $staticDefineNames[$i]";
		$out .= "=\" );\n";
		$out .= "\t\tOutputDebugString( tmp );\n";

		$out .= "\t\tnCombo = nCombo / " . ( $staticDefineMax[$i] - $staticDefineMin[$i] + 1 ) . ";\n";
		$out .= "\n";
	}

	$out .= "\t\tOutputDebugString( \"\\n\" );\n";

	$cskipcode = $perlskipcode;
	$cskipcode =~ s/\$/n/g;
	$out .= "\t\tif( $cskipcode )\n\t\t{\n";
	$out .= "\t\t\tstatic char blah[4] = { 0, 0, 0, 0 };\n";
	$out .= "\t\t\tbyteCode.m_pRawData = blah;\n";
	$out .= "\t\t\tbyteCode.m_nSizeInBytes = 4;\n";
	$out .= "\t\t\treturn byteCode;\n";
	$out .= "\t\t}\n";

	

	$out .= "\t\t// Must null terminate macros.\n";
	$out .= "\t\tdefineMacros[";
	$out .= scalar( @dynamicDefineNames ) + scalar( @staticDefineNames );
	$out .= "]";
	$out .= ".Name = NULL;\n";
	$out .= "\t\tdefineMacros[";
	$out .= scalar( @dynamicDefineNames ) + scalar( @staticDefineNames );
	$out .= "]";
	$out .= ".Definition = NULL;\n\n";


	$out .= "\t\tLPD3DXBUFFER pShader; // NOTE: THESE LEAK!!!\n";
	$out .= "\t\tLPD3DXBUFFER pErrorMessages; // NOTE: THESE LEAK!!!\n";
	$out .= "\t\tHRESULT hr = D3DXCompileShaderFromFile( \"u:\\\\hl2_e3_2004\\\\src_e3_2004\\\\materialsystem\\\\stdshaders\\\\$fxc_filename\",\n\t\t\tdefineMacros,\n\t\t\tNULL, // LPD3DXINCLUDE \n\t\t\t\"main\",\n\t\t\t\"$type\",\n\t\t\t0, // DWORD Flags\n\t\t\t&pShader,\n\t\t\t&pErrorMessages,\n\t\t\tNULL // LPD3DXCONSTANTTABLE *ppConstantTable\n\t\t\t );\n";
	$out .= "\t\tif( hr != D3D_OK )\n";
	$out .= "\t\t{\n";
	$out .= "\t\t\tconst char *pErrorMessageString = ( const char * )pErrorMessages->GetBufferPointer();\n";
	$out .= "\t\t\tOutputDebugString( pErrorMessageString );\n";
	$out .= "\t\t\tOutputDebugString( \"\\n\" );\n";
	$out .= "\t\t\tAssert( 0 );\n";
	$out .= "\t\t\tstatic char blah[4] = { 0, 0, 0, 0 };\n";
	$out .= "\t\t\tbyteCode.m_pRawData = blah;\n";
	$out .= "\t\t\tbyteCode.m_nSizeInBytes = 4;\n";
	$out .= "\t\t}\n";
	$out .= "\t\telse\n";
	$out .= "\t\t{\n";
	$out .= "\t\t\tbyteCode.m_pRawData = pShader->GetBufferPointer();\n";
	$out .= "\t\t\tbyteCode.m_nSizeInBytes = pShader->GetBufferSize();\n";
	$out .= "\t\t}\n";
	$out .= "\t\treturn byteCode;\n";
	return $out;
}

#print "--------\n";

$fxctmp = "fxctmp9_tmp";

if( !stat $fxctmp )
{
	mkdir $fxctmp, 0777 || die $!;
}

# suck in an input file (using includes)
#print "$fxc_filename...";
@fxc = ReadInputFile( $fxc_filename );

# READ THE TOP OF THE FILE TO FIND SHADER COMBOS
foreach $line ( @fxc )
{
	$line="" if ( ( $g_x360 || $g_ps3 ) && ($line=~/\[PC\]/));						# line marked as [PC] when building for x360 or PS3
	$line="" if ( ( $g_x360 == 0 ) && ( $g_ps3 == 0) && ($line=~/\[CONSOLE\]/));	# line marked as [CONSOLE] when building for pc 
	$line="" if ( ( $g_x360 == 0 )  && ($line=~/\[XBOX\]/));						# line marked as [XBOX] when building for pc / ps3
	$line="" if ( ( $g_ps3 == 0 )  && ($line=~/\[SONYPS3\]/));						# line marked as [SONYPS3] when building for pc / xbox
	$line="" if ( ( $g_ps3 == 1 )  && ($line=~/\[!SONYPS3\]/));						# line marked as [!SONYPS3] when building for ps3
	
	$line="" if ( ( $g_sfm == 0 ) && ( $line =~ /\[SFM\]/ ) );						# line marked as [SFM] when not building for sfm
	$line="" if ( ( $g_sfm ) && ( $line =~ /\[\!SFM\]/ ) );							# line marked as [!SFM] when building for sfm

	if ( $fxc_basename =~ m/_ps(\d+\w?)$/i )
	{
		my $psver = $1;
		$line="" if (($line =~/\[ps\d+\w?\]/i) && ($line!~/\[ps$psver\]/i));	# line marked for a version of compiler and not what we build
	}
	if ( $fxc_basename =~ m/_vs(\d+\w?)$/i )
	{
		my $vsver = $1;
		$line="" if (($line =~/\[vs\d+\w?\]/i) && ($line!~/\[vs$vsver\]/i));	# line marked for a version of compiler and not what we build
	}
	
	my $init_expr;

	# looks for something like [=0] after the base part of the combo definition.
	if ( $line =~ /\[\s*\=\s*([^\]]+)\]/ )
	{
		$init_expr = $1;	# parse default init expression for combos
	}

	$line=~s/\[[^\[\]]*\]//;		# cut out all occurrences of
                                    # square brackets and whatever is
                                    # inside all these modifications
                                    # to the line are seen later when
                                    # processing skips and centroids
	
	next if( $line =~ m/^\s*$/ );
	
	if( $line =~ m/^\s*\/\/\s*STATIC\s*\:\s*\"(.*)\"\s+\"(\d+)\.\.(\d+)\"/ )
	{
		local( $name, $min, $max );
		$name = $1;
		$min = $2;
		$max = $3;
		# print STDERR "STATIC: \"$name\" \"$min..$max\"\n";
		push @staticDefineNames, $name;
		push @staticDefineMin, $min;
		push @staticDefineMax, $max;
		$staticDefineInit{$name} = $init_expr;
	}
	elsif( $line =~ m/^\s*\/\/\s*DYNAMIC\s*\:\s*\"(.*)\"\s+\"(\d+)\.\.(\d+)\"/ )
	{
		local( $name, $min, $max );
		$name = $1;
		$min = $2;
		$max = $3;
		# print STDERR "DYNAMIC: \"$name\" \"$min..$max\"\n";
		push @dynamicDefineNames, $name;
		push @dynamicDefineMin, $min;
		push @dynamicDefineMax, $max;
		$dynamicDefineInit{$name} = $init_expr;
	}
}
# READ THE WHOLE FILE AND FIND SKIP STATEMENTS
foreach $line ( @fxc )
{
	if( $line =~ m/^\s*\/\/\s*SKIP\s*\s*\:\s*(.*)$/ )
	{
		#		print $1 . "\n";
		$perlskipcode .= "(" . $1 . ")||";
		push @perlskipcodeindividual, $1;
	}
}

if( defined $perlskipcode )
{
	$perlskipcode .= "0";
	$perlskipcode =~ s/\n//g;
}
else
{
	$perlskipcode = "0";
}

# READ THE WHOLE FILE AND FIND CENTROID STATEMENTS
foreach $line ( @fxc )
{
	if( $line =~ m/^\s*\/\/\s*CENTROID\s*\:\s*TEXCOORD(\d+)\s*$/ )
	{
		$centroidEnable{$1} = 1;
#		print "CENTROID: $1\n";
	}
}

if( $spewCombos )
{
	push @outputHeader, "#include \"windows.h\"\n";
}

#push @outputHeader, "\#include \"shaderlib\\baseshader.h\"\n";
#push @outputHeader, "IShaderDynamicAPI *CBaseShader::s_pShaderAPI;\n";

# Go ahead an compute the mask of samplers that need to be centroid sampled
$centroidMask = 0;
foreach $centroidRegNum ( keys( %centroidEnable ) )
{
#	print "THING: $samplerName $centroidRegNum\n";
	$centroidMask += 1 << $centroidRegNum;
}

#printf "0x%x\n", $centroidMask;

$numCombos = &CalcNumCombos();
#print "$numCombos combos\n";

if ( $g_produceCppClasses )
{
	# Write out the C++ helper class for picking shader combos
	&WriteSkips();
	&WriteStaticHelperClasses();
	&WriteDynamicHelperClasses();
	
	push @outputHeader, &CreateCFuntionToSpewSkippedCombo();
	my $incfilename = "$fxctmp\\$fxc_basename" . ".inc";
	if( $g_toomanycombos )
	{
		unlink $incfilename;
 		print STDERR "ERROR: too many combos in $fxc_filename ($g_totalcombos > 4294967295)!\n";
	}
	else
	{
		&WriteFile( $incfilename, join( "", @outputHeader ) );
	}
}



if( $generateListingFile )
{
	my $listFileName = "$fxctmp/$fxc_basename" . ".lst";
	print "writing $listFileName\n";
	if( !open FILE, ">$listFileName" )
	{
		die;
	}
	print FILE @listingOutput;
	close FILE;
}


@endTimes = times;

$endTime = time;

#printf "Elapsed user time: %.2f seconds!\n", $endTimes[0] - $startTimes[0];
#printf "Elapsed system time: %.2f seconds!\n", $endTimes[1] - $startTimes[1];
#printf "Elapsed child user time: %.2f seconds!\n", $endTimes[2] - $startTimes[2];
#printf "Elapsed child system time: %.2f seconds!\n", $endTimes[3] - $startTimes[3];

#printf "Elapsed total time: %.2f seconds!\n", $endTime - $startTime;


