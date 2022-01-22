<?php 
date_default_timezone_set("UTC");

$query = getenv("QUERY_STRING");
$method = getenv("REQUEST_METHOD");

if(preg_match('/GET/', $method)) {
	if(empty($_GET['firstname'])) $firstname = '?';
	else $firstname = $_GET['firstname'];

	if(empty($_GET['lastname'])) $lastname = '?'; 
	else $lastname = $_GET['lastname'];
}
else if(preg_match('/POST/', $method)) {
	if(empty($query)) $query = 'nil';
	else $firstname = $_POST['firstname'];
	
	if(empty($_POST['firstname'])) $firstname = '?';
	else $firstname = $_POST['firstname'];

	if(empty($_POST['lastname'])) $lastname = '?'; 
	else $lastname = $_POST['lastname'];
}
else {
	$firstname = 'firstname ?';
	$lastname = 'lastname ?';
}

printf("<!DOCTYPE html><html>
   <head>
     <meta charset=\"utf-8\">
     <title>Form Test</title>
   </head>
   <body>
     <p>Hello, %s %s!</p>
     <form method=\"%s\" action=\"%s\">
       What is your name?<br>
       First name:<input type=\"text\" name=\"firstname\"><br>
       Last name: <input type=\"text\" name=\"lastname\"><br>
       <input type=\"submit\">
     </form>
     <p>QUERY_STRING: %s</p>
     <hr>
     %s
   </body>
</html>", $firstname, $lastname, $method, basename(__FILE__), $query, date('r'));
?>
