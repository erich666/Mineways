#!/usr/bin/perl
# perl shader_to_code.pl uber_shader.txt > code.txt
use strict;
use warnings;
 
my $file = $ARGV[0] or die "Need to get USDA code file on the command line\n";
 
open(my $data, '<', $file) or die "Could not open '$file' $!\n";
 
while (my $line = <$data>) {
  chomp $line;
 
  # my @fields = split "," , $line;
  $line =~ s/"/\\"/g;
  print "        strcpy_s(outputString, 256, \"$line\\n\");\n";
  print "        WERROR_MODEL(PortaWrite(gModelFile, outputString, strlen(outputString)));\n";
}