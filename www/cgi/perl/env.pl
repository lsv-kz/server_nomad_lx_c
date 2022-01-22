#!/usr/bin/perl

use strict;
use warnings;
use POSIX;
use File::Basename;
use URI::Escape;

my $time = localtime;

my $meth = $ENV{'REQUEST_METHOD'};
if(!defined($meth))
{
    $meth = '------';
}

my $data = '';
if(($meth =~ /POST/))
{
 #   $data=uri_unescape(<STDIN>);
    $data=(<STDIN>);
}

print "Content-type: text/html; charset=utf-8\n\n";
print "<!DOCTYPE html>
<html>
 <head>
  <title>Environment Dumper </title>
  <style>
    body {
        margin-left:50px;
		margin-right:50px;
		background: rgb(60,40,40);
		color: gold;
    }
  </style>
 </head>
 <body>\n";
#  <center>
#   <table border=1>\n";
foreach (sort keys %ENV)
{
    print "   $_ = $ENV{$_}<br>\n";
}
#   </table>
print "
  <p>$data</p>
  <form action=\"env.pl\" method=\"$meth\">
   <input type=\"hidden\" name=\"name\" value=\".-./. .+.!.?.,.~.#.&.>.<.^.\">
   <input type=\"submit\" value=\"Get ENV\">
  </form>
  <hr>
  $time
 </body>
</html>";

