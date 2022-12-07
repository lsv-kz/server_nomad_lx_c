<html>
 <head>
  <meta charset="utf-8">
  <title>Form Test</title>
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
<?php
$meth = "?";
foreach ($_SERVER as $item=> $description)
{
	if (is_array($description))
		continue;
    if($item === "REQUEST_METHOD")
        $meth = $description;
	echo "  $item=$description<br>\n";
}

foreach ($_POST as $param_name => $param_val)
{
    echo "  $param_name=$param_val<br>\n";
}

foreach ($_GET as $param_name => $param_val)
{
    echo "  $param_name=$param_val<br>\n";
}

?>
  <form method="<?php echo "$meth"?>" action="env.php">
   <input type="hidden" name="name" value=".-._./. .+.!.?.,.~.#.&.>.<.^.">
   <input type="submit" value="get env">
  </form>
  <hr>
<?php  echo date('r');?>
 </body>
</html>
