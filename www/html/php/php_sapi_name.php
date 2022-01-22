<?php
$sapi=php_sapi_name();
echo <<<_END
<!DOCTYPE HTML>
<html>
 <head>
  <meta charset=utf-8>
  <title>php_sapi_name</title>
 </head>
 <body>
  <p>$sapi</p>
 </body>
</html>
_END;

?>
