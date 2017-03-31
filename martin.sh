
get () {
    route "GET" $@
}

post () {
    route "POST" $@
}

delete () {
    route "DELETE" $@
}

status () {
    martin_response_status="$1"
}

header () {
    martin_response_headers="$martin_response_headers$1: $2$LF"
}

not_found () {
    status "404"
    header "Content-type" "text/plain"
    if [ $# -gt 0 ]; then
        echo "$@"
    else
        echo "Not Found: $PATH_INFO"
    fi
}

LF=$'\n'

# route: method, path, action
martin_routes=""

route () {
    martin_routes="$martin_routes$1,$2,$3$LF"
}

martin_find_route () {
  echo "$martin_routes" | while IFS="," read -r method path action; do
    if [ "$1" = "$method" ] && [ "$2" = "$path" ]; then
        echo $action
        return
    fi
  done
}

martin_response_headers=""
martin_response_status=""
martin_response_file="$TMPDIR/martin_response$$"

martin_reset_response () {
    martin_response_status="200 OK"
    martin_response_headers=""
}

martin_dispatch () {
    local action="$(martin_find_route "$REQUEST_METHOD" "$PATH_INFO")"

    [ ! "$action" ] && action="not_found"

    martin_reset_response

    # execute the action, storing output in a temporary file
    "$action" > "$martin_response_file"

    # set status header and content-length header
    header "Status" "$martin_response_status"
    header "Content-Length" "$(wc -c "$martin_response_file" | awk '{ print $1 }')"

    # echo headers, blank line, then body
    echo "$martin_response_headers"
    cat "$martin_response_file"
}

martin () {
  if [ $REQUEST_METHOD ]; then
    # as a CGI script
    martin_dispatch
  else
    # standalone using the wwwoosh server
    . ./wwwoosh.sh
    wwwoosh martin_dispatch $PORT
  fi
}
