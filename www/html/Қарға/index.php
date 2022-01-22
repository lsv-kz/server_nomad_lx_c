<?php
date_default_timezone_set ( "Asia/Almaty");
$doc_root = $_SERVER["DOCUMENT_ROOT"];
if(empty($doc_root))
{
    error("500 Internal Server Error","doc_root ?");
}

$log_file = "/tmp/".basename(__FILE__).'_'.posix_getgid().'.txt';
$fh = fopen($log_file, 'w');
if ($fh == false)
{
    error("500 Internal Server Error","Error: open tmp file");
}
fputs($fh, '------- '.$log_file." --------\r\n");

$uri = url_decode($_SERVER["REQUEST_URI"]);
fputs($fh, __LINE__."> REQUEST_URI".$uri."\r\n");
if(empty($uri))
{
        error("500 Internal Server Error","uri ?");
}

if(preg_match("/([^\/]$)/", $uri))
{
        if(substr_count($uri, '/') > 1)
        {
                $uri = pathinfo($uri,PATHINFO_DIRNAME).'/';
        }
        else if(strlen($uri) > 1) {
                $uri = pathinfo($uri,PATHINFO_DIRNAME);
        }
}
 $cwdd=getcwd();
$dir = $doc_root.$uri;
fputs($fh, "<".__LINE__."> cwdd=".$cwdd."\r\n");
$media = "y";

fputs($fh, "<".__LINE__."> dir=".$dir."\r\n");

$time = localtime();
$list_dir=array();
$list_files=array();
$st;
//   
printf("<!DOCTYPE HTML>
<html>
 <head>
  <meta charset=\"utf-8\">
  <title>Index of %s(h)</title>
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

   </style>
 </head>
 <body id=\"top\">
  <style>
   .semi {
     opacity: 0.5;
   }
  </style>
  <!--  table {
      border-collapse: collapse;
    }
    table, td, th {
      border: 1px solid green;
    }-->
  <h3>Index of %s</h3>
  <table cols=\"2\" width=\"100%%\">
   <tr><td><h3>Directories</h3></td><td></td></tr>\n", $uri, $uri);

if($uri !='/' and $uri !='\\')
{
        echo "   <tr><td><a href=\"../\">Parent Directory/</a></td><td></td></tr>\n";
}
else
{
        echo "   <tr><td></td><td></td></tr>\n";
}

if ($handle = opendir($dir))
{
        if($dir == '.')
        {
                $dir = "";
        }
        
    while (false !== ($entry = readdir($handle)))
    {
        //      if($entry != '.' && $entry != '..' && preg_match("/^\./", $entry))
                if(!preg_match("/^\./", $entry))
                {
                        if(($st=stat($entry)) === false)
                        {
                                fputs($fh, "<".__LINE__.">  Error stat($entry)\r\n");
                                continue;
                        }

                        if(decoct($st['mode'] & octdec('040000')))
                        {
                                array_push($list_dir, $entry);
                        }
                        else array_push($list_files, $entry);
                }
    }
    closedir($handle);
}
sort($list_dir);
sort($list_files);

foreach ($list_dir as $item)
{
        print("   <tr><td><a href=\"".url_encode($item)."/\">".$item."/</a></td><td></td></tr>\n");
}
echo "   <tr><td><hr></td><td><hr></td></tr>\n";
echo "   <tr><td><h3>Files</h3></td><td></td></tr>\n";

foreach ($list_files as $item)
{
        $size=filesize($dir.$item);
        if(preg_match("/(\.jpg)|(\.ico)|(\.png)|(\.svg)|(\.gif)/i", $item) && ($media == 'y'))
        {
                if($size < 80000)
                {
                        print("   <tr><td><a href=\"".url_encode($item)."\"><img src=\"".url_encode($item)."\"></a>".
                        "<br>".$item."</td><td align=\"right\">".$size." bytes</td></tr>\n".
                        "   <tr><td></td><td></td></tr>\n");
                }
                else
                {
                        print("   <tr><td><a href=\"".url_encode($item)."\"><img src=\"".url_encode($item)."\" width=\"320\"></a>".
                                "<br>".$item."</td><td align=\"right\">".$size." bytes</td></tr>\n".
                                "   <tr><td></td><td></td></tr>\n");
                }
        }
        else if(preg_match("/(\.mp3)|(\.wav)/i", $item) && ($media == 'y'))
        {
                print("   <tr><td><audio preload=\"none\" controls src=\"".url_encode($item)."\"></audio>".
                                "<a href=\"".url_encode($item)."\" target=\"_blank\">".$item."</a></td><td align=\"right\">".$size." bytes</td></tr>\n");
        }
        else
                print("   <tr><td><a href=\"".url_encode($item)."\">".$item."</a></td><td align=\"right\">".$size." bytes</td></tr>\n");
}
print("  </table>
  <hr>
  ".date('r')."           
  <a href=\"#top\" style=\"display:block;
        position:fixed;bottom:30px;left:10px;
        width:32px;height:32px;padding:12px 10px 2px;font-size:40px;
        background:gray;
        -webkit-border-radius:15px;
        border-radius:15px;
        color:black;
        opacity: 0.7\">^</a>\n
 </body>
</html>");
fclose($fh);
exit;

function error($stat, $msg)
{
header("Status: 500 Internal Server Error");

print("<!DOCTYPE HTML>
<html>
 <head>
  <title>Error</title>
 </head>
 <body leftmargin=100 rightmargin=100>
  <h3>$stat</h3>
  <p>$msg</p>
  <hr>
  ".date('r')."
 </body>
</html>");
fclose($fh);
exit;
}

function url_encode($arg)
{
        $str=array();
        $cnt=0;
        $len=strlen($arg);
        while($len--) {
                if(preg_match("/([^a-z0-9_.!~*'()\-:\/])/i", $arg[$cnt]))
                {
                        $val=sprintf ("%02X", ord($arg[$cnt]));
                        $str[$cnt]='%'.$val;
                }
                else
                {
                        $str[$cnt]=$arg[$cnt];
                }
                $cnt++;
        }
        return implode($str);
}

function url_decode($arg)
{
        $str=[];
        $cnt1=0;
        $cnt2=0;
        $len=strlen($arg);

        while($len > $cnt1)
        {
                if(preg_match("/\%/i", $arg[$cnt1]))
                {
                        $val=hexdec($arg[++$cnt1])*16+hexdec($arg[++$cnt1]);
                        $str[$cnt2++]=chr($val);
                        $cnt1++;
                }
                else $str[$cnt2++]=$arg[$cnt1++];
        }
        return implode($str);
}
?>
