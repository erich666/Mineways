#!/usr/contrib/bin/perl
# find comments in the file, show where comments with code in them are located

# $| = 1 ;        # turn off output buffering, so we see results if piped, etc
# $[ = 1;         # set array base to 1

while (@ARGV) {  # take the command line arguments and put in a string
    $arg = shift(@ARGV) ;	# get next argument
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
		# print "$_\n";
		if ( /\|(\w+)\|/ ) {
			print "$1\n";
			if ( $1 eq "water" ) {
				exit;
			}
		}
	}
}


