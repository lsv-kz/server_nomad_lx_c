<!DOCTYPE html>
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
	if($item === "REQUEST_METHOD")
		$meth = $description;
	echo "  $item=$description<br>\n";
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
