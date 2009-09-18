#!/bin/sh

source martin.sh

get "/" root
function root () {
    header "Content-Type" "text/html"
    cat <<EOT
<html>
    <head>
        <title>hello world from $PATH_INFO<title>
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

get "/ps" ps_handler
function ps_handler () {
    header "Content-Type" "text/plain"
    ps aux
}

get "/DeanMartin.jpg" dean_handler
function dean_handler () {
    header "Content-Type" "image/jpeg"
    cat "DeanMartin.jpg"
}

get "/redirect" redirect_handler
function redirect_handler () {
    status 302
    header "Location" "http://jackjs.org/"
}

# standalone using the wwwoosh server
wwwoosh_run martin_dispatch 8081

# as a CGI script
#martin_dispatch