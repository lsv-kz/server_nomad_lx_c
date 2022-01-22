#!/usr/bin/perl

use warnings;
use strict;
use POSIX;
use URI::Escape;
use Encode;
use CGI qw(:standard);
use File::Basename;

my $doc_root = $ENV{'DOCUMENT_ROOT'};
my $success = open LOG,'>', "/tmp/".basename(__FILE__).'_'.getgid().'.txt';
if (!$success) {
	exit 1;
}

print LOG __LINE__,"==\n";

my $server_addr=$ENV{'SERVER_ADDR'}; 
my $server_port=$ENV{'SERVER_PORT'}; 
my $http_referer=$ENV{'HTTP_REFERER'};
my $method=$ENV{'REQUEST_METHOD'};

if(!($method =~ /POST/)) {
	printf LOG "GET\n";
#	print "Content-type: text/html\r\n";
#	print "\r\n";
#	print "error method";
#	exit;
}
else
{
	printf LOG "POST\n";
}

#if(!$server_addr){
#	$server_addr = "127.0.0.1";
#}

my $firstname=param('firstname');
if(not defined $firstname) {
	$firstname="?";
}

my $lastname=param('lastname');
if(not defined $lastname) {
	$lastname="?";
}

my $time = localtime;

printf LOG "firstname=%s\nlastname=%s\n",$firstname,$lastname;
# 
print "Content-type: text/html; charset=UTF-8\n\n";
print <<END_OF_PAGE;
<!DOCTYPE html>
<html>
 <head>
  <title>Test Perl</title>
 </head>
 <body leftmargin="100" rightmargin="100" bgcolor="#00bf0f">
  <h3>Здравствуйте, $firstname $lastname!</h3>
  <form action="submit.pl" method="$method">
    <p>Just type in your name (and click Submit) to enter the contest:<br>
       First name: <input type="text" name="firstname" value=""><br>
       Last name:  <input type="text" name="lastname" value=""><br>
       <input type="submit" value="Submit">
    </p>
  </form>
  <hr>
  $time
 </body>
</html>
END_OF_PAGE
