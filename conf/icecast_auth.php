<?php
// This is a simple example script when using auth url. This sort of
// script would be on a web server that would be invoked by icecast when
// checking for a username/password supplied by a listener.

// This code is made available under the most current version of the GPL
// (c) S.Nixon

// copy to local variables
$user_name = $HTTP_GET_VARS['user'];
$passwd = $HTTP_GET_VARS['pass'];

if ($user_name && $passwd)
// trying to authenticate
{
    // try authenticating them in
    $login_result = login($user_name, $passwd);
    if ($login_result === true)
    {
      // okay, so they have a valid login
      header('icecast-auth-user: 1');
    }  
    else
    {
      // unsuccessful login, error message should be in $login_result
      header('icecast-auth-user: 0');
      echo 'You could not be logged in for the following reason: '.$login_result.'<br /> You must be authenticated to connect to this stream.<br />';
    }      
}

function login($username, $password)
// check username and password with db
// if yes, return true
// else return an error string...
{

  $username = trim($username);
  // connect to db
  $conn = db_connect();
  if (!$conn)
    return 'Could not connect to database server - please try later.';

  // check username & pass
  $result = mysql_query("select * from users where user_name='$username' and password = password('$password')");
  if (!$result)
     return 'Could not execute authenication query; please report this to the admin.';
  // if there's exactly one result, the user is validated. Otherwise, they're invalid
  if (mysql_num_rows($result) == 1)
  {   
     return true;
  }
}

function db_connect()
{
  // hostname for db
  $db_hostname = 'localhost';
  // Names of DB to connect to
  $db_name = 'user_auth_db';
  // User for accessing the db
  $db_username = 'db_user';
  // Password for DB user above
  $db_password = 'db_pass';

   $result = mysql_pconnect($db_hostname, $db_username, $db_password); 
   if (!$result)
      return false;
   if (!mysql_select_db($global_db_name))
      return false;

   return $result;
}
?>
