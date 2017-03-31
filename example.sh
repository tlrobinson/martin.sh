#!/bin/sh

. ./martin.sh

get "/" root; root () {
    header "Content-Type" "text/html; charset=utf-8"
    cat <<EOT
<!DOCTYPE html>
<html>
  <head>
    <meta charset="utf-8">
    <title>hello world from $PATH_INFO</title>
  </head>
  <body>
    <img src="/DeanMartin.jpg">
    <h1>hello world from $PATH_INFO</h1>
    <a href="/ps">processes</a>
    <a href="/redirect">redirect</a>
  </body>
</html>
EOT
}

get "/ps" ps_handler; ps_handler () {
    header "Content-Type" "text/plain"
    ps
}

get "/DeanMartin.jpg" dean_handler; dean_handler () {
    header "Content-Type" "image/jpeg"
    cat "DeanMartin.jpg"
}

get "/redirect" redirect_handler; redirect_handler () {
    status 302
    header "Location" "http://jackjs.org/"
}

martin $PORT
