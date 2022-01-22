#!/usr/bin/perl

use strict;
use warnings;
use POSIX;
use URI::Escape;
use CGI qw(:standard);
use File::Basename;
use FCGI;

my $host = ":9002";
print  "-------- env.pl - $host ------------\n";
my $socket = FCGI::OpenSocket($host, 5);

my $request = FCGI::Request(\*STDIN, \*STDOUT, \*STDERR, \%ENV, $socket);

my $count = 1;

while($request->Accept() >= 0)
{
   env_f();
};

sub env_f
{
	my $time = localtime;

	my $doc_root = $ENV{'DOCUMENT_ROOT'};
	my $remote_addr = $ENV{'REMOTE_ADDR'};
	if (!defined($remote_addr))
	{
		$remote_addr = '------';
	}

	my $path = $ENV{'PATH'};
	if(!defined($path))
	{
		$path = '------';
	}

	my $meth = $ENV{'REQUEST_METHOD'};
	if(!defined($meth))
	{
		$meth = '------';
	}
	
	my $data = '';

	if(($meth =~ /POST/))
	{
		$data = <STDIN>;
#		$data = uri_unescape(<STDIN>);
	}

	my $val = ".-./. .+.!.?.,.~.#.&.>.<.^.";

	print "Content-type: text/html; charset=utf-8\n\n";
print "<!DOCTYPE html>
<html>
 <head>
  <title>Environment Dumper (fastcgi Perl)</title>
  <style>
    body {
        margin-left:50px;
		margin-right:50px;
		background: rgb(60,40,40);
		color: gold;
    }
  </style>
 </head>
 <body>
";
	foreach (sort keys %ENV)
	{
		print "  $_ = $ENV{$_}<br>\n";
	}

print "
  <p>$data</p>
  <form method=\"$meth\">
   <input type=\"hidden\" name=\"name\" value=\"$val\">
   <input type=\"submit\" value=\"Get \$ENV\">
  </form>
  <hr>
  $time
 </body>
</html>";
}
