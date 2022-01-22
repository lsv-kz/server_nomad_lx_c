<?php
header("Content-type: text/plain; charset=utf-8");
echo 'SCRIPT_NAME: ' .getenv('SCRIPT_NAME') . "\n";
echo 'Current Working Directory: '.getcwd();
?>
