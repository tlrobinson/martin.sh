#!/bin/sh

default_port="8080"
http_version="HTTP/1.1"
wwwoosh_fifo="/tmp/wwwoosh_fifo"

CR=$'\r'
LF=$'\n'
CRLF="$CR$LF"

function handle_request () {
    app="$1"

    # read the request line
    read request_line

    # read the header lines until we reach a blank line
    while read header && [ ! "$header" = $'\r' ]; do
        # FIXME: multiline headers
        header_name="HTTP_$(echo $header | cut -d ':' -f 1 | tr 'a-z-' 'A-Z_')"
        export $header_name="$(echo $header | cut -d ':' -f 2 | sed 's/^ //')"
    done

    # extract HTTP method and HTTP version
    export REQUEST_METHOD=$(echo $request_line | cut -d ' ' -f 1)
    export HTTP_VERSION=$(echo $request_line | cut -d ' ' -f 3 | tr -d $'\r')

    # extract the request_path, then PATH_INFO and QUERY_STRING components
    request_path=$(echo $request_line | cut -d ' ' -f 2)
    export PATH_INFO=$(echo $request_path | cut -d '?' -f 1)
    export QUERY_STRING=$(echo $request_path | cut -d '?' -f 2)

    export SCRIPT_NAME=""
    export SERVER_NAME="localhost"
    export SERVER_PORT="$port"

    "$app"
}

function handle_response () {
    response_status="200 OK"
    response_headers=""

    while read header && [ ! "$header" = "" ]; do
        header_name="$(echo $header | cut -d ':' -f 1 | tr 'A-Z' 'a-z')"
        if [ "$header_name" = "status" ]; then
            response_status="$(echo $header | cut -d ':' -f 2 | sed 's/^ //')"
        #elif [ "$header_name" = "content-type" ]; then
        #elif [ "$header_name" = "location" ]; then
        else
            if [ "$response_headers" ]; then
                response_headers="$response_headers$CRLF$header"
            else
                response_headers="$header"
            fi
        fi
    done

    # Date
    header="Date: $(date -u '+%a, %d %b %Y %R:%S GMT')"
    response_headers="$response_headers$CRLF$header"

    # echo status line, headers, blank line, body
    echo "$http_version $response_status$CRLF$response_headers$CRLF$CR"
    cat
}

function wwwoosh_run () {
    app="$1"

    # TODO: is there a better way than a named pipe?
    rm -f "$wwwoosh_fifo"
    mkfifo "$wwwoosh_fifo"

    port="$default_port"
    [ $# -gt 1 ] && port="$2"

    debug=""
    [ $# -gt 2 ] && debug="$3"

    echo "Starting Wwwoosh on port $port..."

    while true; do

        if [ "$debug" ]; then
            nc -l $port < "$wwwoosh_fifo" | tee /dev/stderr | handle_request "$app" | handle_response | tee /dev/stderr > "$wwwoosh_fifo"
        else
            nc -l $port < "$wwwoosh_fifo" | handle_request "$app" | handle_response > "$wwwoosh_fifo"
        fi

    done
}
