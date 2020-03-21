#!/usr/contrib/bin/perl
# find comments in the file, show where comments with code in them are located

# put incomplete list first, then full list file second:
# perl name_compare.pl nbt_names.txt html_names.txt

while (@ARGV) {  # take the command line arguments and put in a string
    $arg = shift(@ARGV) ;	# get next argument
	$file_count++;
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
		if ( $file_count == 1 ) {
			$name{$_} = 1;
		} else {
			if ( !exists($name{$_}) ) {
				print "$_ not found\n";
			}
		}
	}
}
