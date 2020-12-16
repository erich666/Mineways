#!/usr/bin/perl
# run with perl export_all_packs.pl ..\..\resource_packs\_resource_pack_summary.csv > use_all_textures.mwscript
use strict;
use warnings;

my $txrpath = "C:\\Users\\Eric\\Documents\\_documents\\MinecraftOmniverse\\resource_packs";
my $outpath = "C:\\Users\\Eric\\Documents\\_documents\\MinecraftOmniverse\\USD conversions\\resource_packs";
my $root = "venice_test";
 
my $file = $ARGV[0] or die "Need to get CSV file on the command line\n";
 
my $firstLine = 1;
open(my $data, '<', $file) or die "Could not open '$file' $!\n";
 
while (my $line = <$data>) {
  chomp $line;
 
  my @fields = split "," , $line;
  if ( !$firstLine ) {
    print "\nTerrain file name: $txrpath\\terrainExt$fields[2].png\n";
	print "Set render type: USD 1.0\n";
	print "File type: Export tiles for textures to directory textures_$fields[2]\n";
    print "Export for rendering: $outpath\\$root\_$fields[2].usda\n";
  } else {
    $firstLine = 0;
  }
}
