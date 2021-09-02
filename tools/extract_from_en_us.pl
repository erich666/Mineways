#!usr/bin/perl

use strict;
use warnings;

while (@ARGV) {
    my $arg = shift(@ARGV);
    unless (open(INFILE,$arg)) {
		print "can't open $arg: $!\n";
    }
    &PROCESS() ;
    close(INFILE) ;
}
exit 0 ;

sub PROCESS {
    while (<INFILE>) {
		chop;
		if ( $_ = /"block\.minecraft\.([\w|_]+)"/ ) {
			print "$1\n";
		}
	}
}
