#!/bin/sh

wwwoosh_port="8080"
wwwoosh_http_version="HTTP/1.1"

wwwoosh_fifo="/tmp/wwwoosh_fifo"
wwwoosh_debug_enabled=""

CR=$'\r'
LF=$'\n'
CRLF="$CR$LF"

wwwoosh () {
    local app="$1"

    # TODO: is there a better way than a named pipe?
    rm -f "$wwwoosh_fifo"
    mkfifo "$wwwoosh_fifo"

    [ $# -gt 1 ] && wwwoosh_port="$2"
    [ $# -gt 2 ] && wwwoosh_debug_enabled="$3"

    echo "Starting Wwwoosh on port $wwwoosh_port..."

    while true; do
        wwwoosh_listen $wwwoosh_port < "$wwwoosh_fifo" |
        wwwoosh_debug |
        wwwoosh_handle_request "$app" |
        wwwoosh_debug |
        wwwoosh_handle_response > "$wwwoosh_fifo"
    done
}

wwwoosh_debug () {
    if [ $wwwoosh_debug_enabled ]; then
        tee /dev/stderr
    else
        cat
    fi
}

wwwoosh_handle_request () {
    local app="$1"

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
    export wwwoosh_http_version=$(echo $request_line | cut -d ' ' -f 3 | tr -d $'\r')

    # extract the request_path, then PATH_INFO and QUERY_STRING components
    request_path=$(echo $request_line | cut -d ' ' -f 2)
    export PATH_INFO=$(echo $request_path | cut -d '?' -f 1)
    export QUERY_STRING=$(echo $request_path | cut -d '?' -f 2)

    export SCRIPT_NAME=""
    export SERVER_NAME="localhost"
    export SERVER_PORT="$port"

    "$app"
}

wwwoosh_handle_response () {
    local response_status="200 OK"
    local response_headers=""

    local content_length="-"

    add_header () {
      local header="$1"
      if [ "$response_headers" ]; then
          response_headers="$response_headers$CRLF$header"
      else
          response_headers="$header"
      fi
    }

    while read header && [ ! "$header" = "" ]; do
        local header_name="$(echo $header | cut -d ':' -f 1 | tr 'A-Z' 'a-z')"
        local header_value="$(echo $header | cut -d ':' -f 2)"
        if [ "$header_name" = "status" ]; then
            response_status="$(echo $header | cut -d ':' -f 2 | sed 's/^ //')"
        elif [ "$header_name" = "content-length" ]; then
            content_length="$header_value"
            add_header "$header"
        else
            add_header "$header"
        fi
    done

    add_header "Connection: close"
    add_header "Date: $(date -u '+%a, %d %b %Y %R:%S GMT')"

    # echo status line, headers, blank line, body
    echo "$wwwoosh_http_version $response_status$CRLF$response_headers$CRLF$CR"
    cat

    log_remote_host="-"
    log_user="-"
    log_date="$(date -u '+%d/%b/%Y:%H:%M:%S')"
    log_header="$REQUEST_METHOD $PATH_INFO $wwwoosh_http_version" # doesn't work
    log_status="$(echo "$response_status" | cut -d " " -f1)"
    log_size="$content_length"

    echo "$log_remote_host - $log_user [$log_date] \"$log_header\" $log_status $log_size" 1>&2
}

wwwoosh_listen () {
  # first try the standard netcat, then the bsd netcat
  nc -l -p $1 2> /dev/null || nc -l $1
}

case "$0" in
  *wwwoosh.sh) wwwoosh $@ ;;
esac
