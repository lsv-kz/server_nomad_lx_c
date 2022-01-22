#!/usr/bin/perl

use strict;
use warnings;
use POSIX;
use File::Basename;
use FCGI;

my $host = ":9001";
print  "-------- index.pl - $host ------------\n";

my $socket = FCGI::OpenSocket($host, 10);

my $request = FCGI::Request(\*STDIN, \*STDOUT, \*STDERR, \%ENV, $socket);

my $count = 1;

while($request->Accept() >= 0)
{
   index_f();
}

sub index_f
{
    my $time = localtime;
    my $doc_root = url__decode($ENV{'DOCUMENT_ROOT'});

    my $play = 'y';
    my $show_pictures = 'y';

    my $host = $ENV{"HTTP_HOST"};
    my $uri = url__decode($ENV{"REQUEST_URI"});

    my $dir=$doc_root.$uri;

    if(!(opendir DIR, $dir))
    {
        error("403 Forbidden<br> path: ".$dir);
        return;
    }

    my @allfiles = readdir DIR;
    closedir DIR;
    my @sorted=sort @allfiles;
    my @folders;
    my @files;

    my $item;
    my $size;
    while(($item=pop @sorted))
    {
        if(-f ($dir.$item))
        {
            $size = -s $dir.$item;
            if(($item =~ /(\.mp3)|(\.wav)/i) && ($play =~ /^y$/i) && (!($item =~ /(^\.)/)))
            {
                    unshift @files, "   <tr><td><audio preload=\"none\" controls src=\"".url__encode($item)."\"></audio>".
                    "<a href=\"".url__encode($item)."\">".$item."</a></td><td align=\"right\">".$size." bytes</td></tr>\n";
            }
            elsif(($item =~ /(\.jpg)|(\.ico)|(\.png)|(\.svg)|(\.gif)/i) && (!($item =~ /(^\.)/)) && ($show_pictures =~ /^y$/i))
            {
                if($size < 8000)
                {
                    unshift @files, "   <tr><td><a href=\"".url__encode($item)."\"><img src=\"".url__encode($item)."\"></a>".
                                "<br>".$item."</td><td align=\"right\">".$size." bytes</td></tr>\n".
                                "   <tr><td></td><td></td></tr>\n";
                }
                else
                {
                    if(!($item =~ /(^\.)/))
                    {
                        unshift @files, "   <tr><td><a href=\"".url__encode($item)."\"><img src=\"".url__encode($item)."\" width=\"320\"></a>".
                                "<br>".$item."</td><td align=\"right\">".$size." bytes</td></tr>\n".
                                "   <tr><td></td><td></td></tr>\n";
                    }
                }
            }
            else
            {
                if(!($item =~ /(^\.)/))
                {
                    unshift @files, "   <tr><td><a href=\"".url__encode($item)."\">".$item."</a></td><td align=\"right\">".$size." bytes</td></tr>\n";
                }
            }
        }
        if (-d ($dir.$item))
        {
            if(!($item =~ /(^\.{2}$)|(^\.$)|(^\.)/))
            {
                unshift  @folders, "   <tr><td><a href=\"".url__encode($item)."/\">".$item."/</a></td><td></td></tr>\n";
            }
        }
    }

    print "Content-type: text/html; charset=UTF-8\r\n";
    print "\r\n";

print <<HTML;
<!DOCTYPE HTML>
<html>
 <head>
  <meta charset=\"utf-8\">
  <title>Index of $uri</title>
  <style>
   body {
    margin-left:100px;
    margin-right:50px;
    background: rgb(60,40,40);
    color: rgb(200,240,120);
   }
   a {
    text-decoration: none;
    color:gold;
   }
   h3 {
    color: rgb(200,240,120);
   }
#   table {
#    border-collapse: collapse;
#   }
#   table, td, th {
#    border: 1px solid green;
#   }
  </style>
 </head>
 <body id=\"top\">
  <style>
   .semi {
     opacity: 0.5;
   }
  </style>
  <h3>Index of $uri (fcgi perl)</h3>
  <table cols=\"2\" width=\"100%\">
   <tr><td><h3>Directories</h3></td><td></td></tr>
HTML

    if(($uri =~ /^\/$/))
    {
        print "   <tr><td></td><td></td></tr>\n";
    }
    else
    {
        print "   <tr><td><a href=\"../\">Parent Directory/</a></td><td></td></tr>\n";
    }
    print @folders;
    print "   <tr><td><hr></td><td><hr></td></tr>\n";
    print "   <tr><td><h3>Files</h3></td><td></td></tr>\n";
    print @files;
    printf("  </table>
  <hr>
  %s
  <a href=\"#top\" style=\"display:block;position:fixed;bottom:30px;left:10px;
                    width:32px;height:32px;padding:12px 10px 2px;font-size:40px;
                    background:#ddd;
                    -webkit-border-radius:15px;
                    border-radius:15px;
                    color:green;
                    opacity: 0.7\">^</a>
 </body>
</html>", $time);
}

sub url__encode
{
    my $text = shift;
    $text =~ s/([^a-z0-9_.!~*()\-\/?])/sprintf "%%%02X", ord($1)/egi; 
    return $text;
}

sub url__decode
{
    my $text = shift;
    $text =~ s/%([a-f0-9][a-f0-9])/chr(hex($1))/egi;
    return $text;
}

sub error
{
    my $msg = shift;
    print "Status: 500 Internal Server Error\r\n";
    print "Content-type: text/html; charset=UTF-8\r\n";
    print "\r\n";
    my $time=localtime;
print <<HTML;
<!DOCTYPE HTML>
<html>
 <head>
  <meta charset=\"utf-8\">
  <title>Error</title>
   <style>
    body {
      margin-left:100px; margin-right:50px;
    }
   </style>
 </head>
 <body>
  <h3>$msg</h3>
  <hr>
  $time
 </body>
</html>
HTML

}



