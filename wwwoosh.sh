#!/bin/sh

port="8080"
application="$1"

function handler () {
    app=$1
    
    # read the request line
    read request_line
    
    # read the header lines until we reach a blank line
    while read header && [ ! "$header" == $'\r' ]; do
        # FIXME: multiline headers
        # FIXME: doesn't strip space between ":" and value
        header_name="HTTP_$(echo $header | cut -d ':' -f 1 | tr 'a-z-' 'A-Z_')"
        export $header_name="$(echo $header | cut -d ':' -f 2)"
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
    export SERVER_port="$port"

    # echo the status line
    # FIXME: don't output status line until the headers are complete, so we can respect "Status" header if present
    echo "$HTTP_VERSION 200 OK"
    "$app"
    
    # FIXME: this last newline causes the response to complete for some reason
    echo ""
}

# TODO: is there a better way than a named pipe?
pipe="nc-www-pipe"
rm -f "$pipe"
mkfifo "$pipe"

while true; do
    #nc -l $port < "$pipe" | handler $app > "$pipe"
    # debug edition:
    nc -l $port < "$pipe" | tee /dev/stderr | handler "$app" | tee /dev/stderr > "$pipe"
done