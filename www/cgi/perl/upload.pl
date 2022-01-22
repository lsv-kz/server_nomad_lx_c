#!/usr/bin/perl -w

use strict;
use URI::Escape;
use Encode;
use CGI qw (:standard);
use CGI::Carp qw(fatalsToBrowser);

my $success = open LOG,'>',"/tmp/perl_upload_pl.txt";
if (!$success) {
	exit 1;
}
my $dir = $ENV{DOCUMENT_ROOT}."/..Downloads/";
use constant MAX_DIR_SIZE   => 100000000;

$CGI::POST_MAX = 5000000; #1024 * 250; # Предел загрузки 250 Кбайт

my $q = new CGI;
$q->cgi_error and error( "Error transferring file: ". $q->cgi_error );

my $File_Name = uri_unescape(param('filename')) || error( "No filename entered." );
#my $File_Name = param('filename') || error( "No filename entered." );

if ( dir_size( $dir ) + $ENV{'CONTENT_LENGTH'} > MAX_DIR_SIZE ) {
   error( "Upload directory is full." );
}

my $path = $dir.$File_Name;
print LOG $File_Name,"\n";

my $Mime = uploadInfo($File_Name)->{'Content-Type'};

$success = open FILE_OUT,'>',$path;
if (!$success) {
	exit 1;
}
my $data;
while((<$File_Name>))
{
#	print FILE_OUT $data;;
	syswrite FILE_OUT,$_;
}

print header(-TYPE =>"text/html; charset=\"utf-8\"");

print start_html('example');
printf "<p>Name file:%s; %d bytes</p>\n",$File_Name, -s "$path";
printf "<p>Type MIME:%s</p>\n", $Mime;
print "<form action=\"upload.pl\" enctype=\"multipart/form-data\" method=\"post\"><br>\n";
print "What files are you sending? <input type=\"file\" name=\"filename\"><br>\n";
print "<input type=\"submit\" value=\"Upload\"> <input type=\"reset\"><br>\n";
print "</form>\n";
print end_html;
print LOG "end\n";

sub dir_size {
    my $dir = shift;
    my $dir_size = 0;
    
    # Loop through files and sum the sizes; doesn't descend down subdirs
    opendir DIR, $dir or die "Unable to open $dir: $!";
    while ( readdir DIR ) {
        $dir_size += -s "$dir/$_";
    }
    return $dir_size;
}
sub error {
    my( $reason ) = @_;
    
    print header( "text/html" ),
		start_html( "Error" ),
		h1( "Error" ),
		p( "Your upload was not procesed because the following error ",
                 "occured: " ),
		p( i( $reason ) );
	print "<form action=\"upload.pl\" enctype=\"multipart/form-data\" method=\"post\"><br>\n";
	print "What files are you sending? <input type=\"file\" name=\"filename\"><br>\n";
	print "<input type=\"submit\" value=\"Upload\"> <input type=\"reset\"><br>\n";
	print "</form>\n";
	print end_html;
    exit;
}
