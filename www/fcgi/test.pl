#!/usr/bin/perl

use strict;
use warnings;

use FCGI;

my $port = "9004";
print "------ :".$port." ------\n";
my $socket = FCGI::OpenSocket(":".$port, 128);

my $request = FCGI::Request(\*STDIN, \*STDOUT, \*STDERR, \%ENV, $socket);

my $count = 1;

while($request->Accept() >= 0)
{
    print "Content-Type: text/plain; charset=utf-8\r\n\r\n";
    print "count: ".$count++."\n";
    foreach (sort keys %ENV)
    {
        print "$_: $ENV{$_}\n";
    }
};
