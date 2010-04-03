. wwwoosh.sh

martin_response="/tmp/martin_response"

routes_method=()
routes_path=()
routes_action=()

route () {
    routes_method=( ${routes_method[@]} "$1" )
    routes_path=( ${routes_path[@]} "$2" )
    routes_action=( ${routes_action[@]} "$3" )
}

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
    response_status="$1"
}

header () {
    head="$1: $2"
    if [ "$response_headers" ]; then
        response_headers="$response_headers\n$head"
    else
        response_headers="$head"
    fi
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

martin_dispatch () {
    action=""

    for (( i = 0 ; i < ${#routes_method[@]} ; i++ )); do
        method=${routes_method[$i]}
        path=${routes_path[$i]}
        act=${routes_action[$i]}
        if [ "$REQUEST_METHOD" = "$method" ]; then
            if [ "$PATH_INFO" = "$path" ]; then
                action="$act"
                break
            fi
        fi
    done

    [ ! "$action" ] && action="not_found"

    reset_response

    # execute the action, storing output in a temporary file
    "$action" > "$martin_response"

    # set status header, echo headers, blank line, then body
    header "Status" "$response_status"
    echo "$response_headers"
    echo ""
    cat "$martin_response"
}

reset_response () {
    response_status="200 OK"
    response_headers=""
}