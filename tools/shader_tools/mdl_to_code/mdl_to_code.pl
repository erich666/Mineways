#!/usr/bin/perl
# perl mdl_to_code.pl > mdlFiles.h
use strict;
use warnings;
use English;

my $dir = '.';

my @fname;
my $fnum = 0;
my @root;
my @lineCount;
my $line;
my $fh;
my $fp;
my $i;

print "#include \"stdafx.h\"\n";

foreach $fp (glob("$dir/*.mdl")) {
    #printf "%s\n", $fp;
	if ( $fp =~ m/(\w+)/) {
		$root[$fnum] = $1;
	} else {
		print STDERR "Couldn't parse MDL file name $fp\n";
		exit 1;
	}
	$fname[$fnum] = $fp;

    open $fh, "<", $fp or die "can't read open '$fp': $OS_ERROR";
	print "\nconst char* file_$root[$fnum]\[\] = {\n";
	$lineCount[$fnum] = 0;
    while ($line = <$fh>) {
		chomp $line;
		# my @fields = split "," , $line;
		$line =~ s/"/\\"/g;
		print "	\"$line\\n\",\n";
		$lineCount[$fnum]++;
    }
	print "};\n";
    close $fh or die "can't read close '$fp': $OS_ERROR";
	$fnum++;
}

print "\nconst int mdlFileLines[] = {\n";
for ($i = 0; $i < $fnum; $i++ ) {
	print "	$lineCount[$i],\n";
}
print "};\n";


print "\nconst char** mdlFileContents[] = {\n";
for ($i = 0; $i < $fnum; $i++ ) {
	print "	file_$root[$i],\n";
}
print "	NULL\n";
print "};\n";


print "\nconst wchar_t* mdlFileNames[] = {\n";
for ($i = 0; $i < $fnum; $i++ ) {
	print "	L\"$root[$i].mdl\",\n";
}
print "};\n";


