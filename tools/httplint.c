/*
 * HTTP Header Lint
 * Licensed under the MIT License
 *                http://www.opensource.org/licenses/mit-license
 * Copyright 2004 James Bursa <bursa@users.sourceforge.net>
 */

/*
 * Compile using
 *   gcc -W -Wall `curl-config --cflags --libs` -o httplint httplint.c
 *
 * References of the form [6.1.1] are to RFC 2616 (HTTP/1.1).
 */

#define _GNU_SOURCE
#define __USE_XOPEN

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <regex.h>
#include <curl/curl.h>


#define NUMBER "0123456789"
#define UNUSED(x) x = x

char *strndup(const char *src, size_t len) {
    return strdup(src);
}

bool start;
bool html = false;
CURL *curl;
int status_code;
char error_buffer[CURL_ERROR_SIZE];
regex_t re_status_line, re_token, re_token_value, re_content_type, re_ugly,
    re_absolute_uri, re_etag, re_server, re_transfer_coding, re_upgrade,
    re_rfc1123, re_rfc1036, re_asctime, re_cookie_nameval, re_cookie_expires;


void init(void);
void regcomp_wrapper(regex_t *preg, const char *regex, int cflags);
void check_url(const char *url);
size_t header_callback(char *ptr, size_t msize, size_t nmemb, void *stream);
size_t data_callback(void *ptr, size_t size, size_t nmemb, void *stream);
void check_status_line(const char *s);
void check_header(const char *name, const char *value);
bool parse_date(const char *s, struct tm *tm);
int month(const char *s);
time_t mktime_from_utc(struct tm *t);
const char *skip_lws(const char *s);
bool parse_list(const char *s, regex_t *preg, unsigned int n, unsigned int m,
    void (*callback)(const char *s, regmatch_t pmatch[]));
void header_accept_ranges(const char *s);
void header_age(const char *s);
void header_allow(const char *s);
void header_cache_control(const char *s);
void header_cache_control_callback(const char *s, regmatch_t pmatch[]);
void header_connection(const char *s);
void header_content_encoding(const char *s);
void header_content_encoding_callback(const char *s, regmatch_t pmatch[]);
void header_content_language(const char *s);
void header_content_length(const char *s);
void header_content_location(const char *s);
void header_content_md5(const char *s);
void header_content_range(const char *s);
void header_content_type(const char *s);
void header_date(const char *s);
void header_etag(const char *s);
void header_expires(const char *s);
void header_last_modified(const char *s);
void header_location(const char *s);
void header_pragma(const char *s);
void header_retry_after(const char *s);
void header_server(const char *s);
void header_trailer(const char *s);
void header_transfer_encoding(const char *s);
void header_transfer_encoding_callback(const char *s, regmatch_t pmatch[]);
void header_upgrade(const char *s);
void header_vary(const char *s);
void header_via(const char *s);
void header_set_cookie(const char *s);
void die(const char *error);
void print(const char *s, size_t len);
void lookup(const char *key);


struct header_entry {
  char name[40];
  void (*handler)(const char *s);
  int count;
  char *missing;
} header_table[] = {
  { "Accept-Ranges", header_accept_ranges, 0, 0 },
  { "Age", header_age, 0, 0 },
  { "Allow", header_allow, 0, 0 },
  { "Cache-Control", header_cache_control, 0, 0 },
  { "Connection", header_connection, 0, 0 },
  { "Content-Encoding", header_content_encoding, 0, 0 },
  { "Content-Language", header_content_language, 0, "missingcontlang" },
  { "Content-Length", header_content_length, 0, 0 },
  { "Content-Location", header_content_location, 0, 0 },
  { "Content-MD5", header_content_md5, 0, 0 },
  { "Content-Range", header_content_range, 0, 0 },
  { "Content-Type", header_content_type, 0, "missingcontenttype" },
  { "Date", header_date, 0, "missingdate" },
  { "ETag", header_etag, 0, 0 },
  { "Expires", header_expires, 0, 0 },
  { "Last-Modified", header_last_modified, 0, "missinglastmod" },
  { "Location", header_location, 0, 0 },
  { "Pragma", header_pragma, 0, 0 },
  { "Retry-After", header_retry_after, 0, 0 },
  { "Server", header_server, 0, 0 },
  { "Set-Cookie", header_set_cookie, 0, 0 },
  { "Trailer", header_trailer, 0, 0 },
  { "Transfer-Encoding", header_transfer_encoding, 0, 0 },
  { "Upgrade", header_upgrade, 0, 0 },
  { "Vary", header_vary, 0, 0 },
  { "Via", header_via, 0, 0 }
};


/**
 * Main entry point.
 */
int main(int argc, char *argv[])
{
  int i = 1;

  if (argc < 2)
    die("Usage: httplint [--html] url [url ...]");

  init();

  if (1 < argc && strcmp(argv[1], "--html") == 0) {
    html = true;
    i++;
  }

  for (; i != argc; i++)
    check_url(argv[i]);

  curl_global_cleanup();

  return 0;
}


/**
 * Initialise the curl handle and compile regular expressions.
 */
void init(void)
{
  struct curl_slist *request_headers = 0;

  if (curl_global_init(CURL_GLOBAL_ALL))
    die("Failed to initialise libcurl");

  curl = curl_easy_init();
  if (!curl)
    die("Failed to create curl handle");

  if (curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback))
    die("Failed to set curl options");
  if (curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, data_callback))
    die("Failed to set curl options");
  if (curl_easy_setopt(curl, CURLOPT_USERAGENT, "httplint"))
    die("Failed to set curl options");
  if (curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer))
    die("Failed to set curl options");

  /* remove libcurl default headers */
  request_headers = curl_slist_append(request_headers, "Accept:");
  request_headers = curl_slist_append(request_headers, "Pragma:");
  if (curl_easy_setopt(curl, CURLOPT_HTTPHEADER, request_headers))
    die("Failed to set curl options");

  /* compile regular expressions */
  regcomp_wrapper(&re_status_line,
      "^HTTP/([0-9]+)[.]([0-9]+) ([0-9][0-9][0-9]) ([\t -~€-ÿ]*)$",
      REG_EXTENDED);
  regcomp_wrapper(&re_token,
      "^([-0-9a-zA-Z_.!]+)",
      REG_EXTENDED);
  regcomp_wrapper(&re_token_value,
      "^([-0-9a-zA-Z_.!]+)(=([-0-9a-zA-Z_.!]+|\"([^\"]|[\\].)*\"))?",
      REG_EXTENDED);
  regcomp_wrapper(&re_content_type,
      "^([-0-9a-zA-Z_.]+)/([-0-9a-zA-Z_.]+)[ \t]*"
      "(;[ \t]*([-0-9a-zA-Z_.]+)="
       "([-0-9a-zA-Z_.]+|\"([^\"]|[\\].)*\")[ \t]*)*$",
      REG_EXTENDED);
  regcomp_wrapper(&re_absolute_uri,
      "^[a-zA-Z0-9]+://[^ ]+$",
      REG_EXTENDED);
  regcomp_wrapper(&re_etag,
      "^(W/[ \t]*)?\"([^\"]|[\\].)*\"$",
      REG_EXTENDED);
  regcomp_wrapper(&re_server,
      "^((([-0-9a-zA-Z_.!]+(/[-0-9a-zA-Z_.]+)?)|(\\(.*\\)))[ \t]*)+$",
      REG_EXTENDED);
  regcomp_wrapper(&re_transfer_coding,
      "^([-0-9a-zA-Z_.]+)[ \t]*"
      "(;[ \t]*([-0-9a-zA-Z_.]+)="
       "([-0-9a-zA-Z_.]+|\"([^\"]|[\\].)*\")[ \t]*)*$",
      REG_EXTENDED);
  regcomp_wrapper(&re_upgrade,
      "^([-0-9a-zA-Z_.](/[-0-9a-zA-Z_.])?)+$",
      REG_EXTENDED);
  regcomp_wrapper(&re_ugly,
      "^[a-zA-Z0-9]+://[^/]+[-/a-zA-Z0-9_]*$",
      REG_EXTENDED);
  regcomp_wrapper(&re_rfc1123,
      "^(Mon|Tue|Wed|Thu|Fri|Sat|Sun), ([0123][0-9]) "
      "(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) ([0-9]{4}) "
      "([012][0-9]):([0-5][0-9]):([0-5][0-9]) GMT$",
      REG_EXTENDED);
  regcomp_wrapper(&re_rfc1036,
      "^(Monday|Tuesday|Wednesday|Thursday|Friday|Saturday|Sunday), "
      "([0123][0-9])-(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)-"
      "([0-9][0-9]) ([012][0-9]):([0-5][0-9]):([0-5][0-9]) GMT$",
      REG_EXTENDED);
  regcomp_wrapper(&re_asctime,
      "^(Mon|Tue|Wed|Thu|Fri|Sat|Sun) "
      "(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) ([ 12][0-9]) "
      "([012][0-9]):([0-5][0-9]):([0-5][0-9]) ([0-9]{4})$",
      REG_EXTENDED);
  regcomp_wrapper(&re_cookie_nameval,
      "^[^;, ]+=[^;, ]*$",
      REG_EXTENDED);
  regcomp_wrapper(&re_cookie_expires,
      "^(Mon|Tue|Wed|Thu|Fri|Sat|Sun), ([0123][0-9])-"
      "(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)-([0-9]{4}) "
      "([012][0-9]):([0-5][0-9]):([0-5][0-9]) GMT$",
      REG_EXTENDED);
}


/**
 * Compile a regular expression, handling errors.
 */
void regcomp_wrapper(regex_t *preg, const char *regex, int cflags)
{
  char errbuf[200];
  int r;
  r = regcomp(preg, regex, cflags);
  if (r) {
    regerror(r, preg, errbuf, sizeof errbuf);
    fprintf(stderr, "Failed to compile regexp '%s'\n", regex);
    die(errbuf);
  }
}


/**
 * Fetch and check the headers for the specified url.
 */
void check_url(const char *url)
{
  int i, r;
  CURLcode code;

  start = true;
  for (i = 0; i != sizeof header_table / sizeof header_table[0]; i++)
    header_table[i].count = 0;

  if (!html)
    printf("Checking URL %s\n", url);
  if (strncmp(url, "http", 4)) {
    if (html)
      printf("<p class='warning'>");
    printf("Warning: this is not an http or https url");
    if (html)
      printf("</p>");
    printf("\n");
  }

  if (curl_easy_setopt(curl, CURLOPT_URL, url))
    die("Failed to set curl options");

  if (html)
    printf("<ul>\n");
  code = curl_easy_perform(curl);
  if (html)
    printf("</ul>\n");
  if (code != CURLE_OK && code != CURLE_WRITE_ERROR) {
    if (html)
      printf("<p class='error'>");
    printf("Error: ");
    print(error_buffer, strlen(error_buffer));
    printf(".");
    if (html)
      printf("</p>");
    printf("\n");
    return;
  } else {
    printf("\n");
    if (html)
      printf("<ul>");
    for (i = 0; i != sizeof header_table / sizeof header_table[0]; i++) {
      if (header_table[i].count == 0 && header_table[i].missing)
        lookup(header_table[i].missing);
    }
  }

  r = regexec(&re_ugly, url, 0, 0, 0);
  if (r)
    lookup("ugly");

  if (html)
    printf("</ul>");
}


/**
 * Callback for received header data.
 */
size_t header_callback(char *ptr, size_t msize, size_t nmemb, void *stream)
{
  const size_t size = msize * nmemb;
  char s[400], *name, *value;

  UNUSED(stream);

  printf(html ? "<li><code>" : "* ");
  print(ptr, size);
  printf(html ? "</code><ul>" : "\n");

  if (size < 2 || ptr[size - 2] != 13 || ptr[size - 1] != 10) {
    lookup("notcrlf");
    if (html)
      printf("</ul></li>\n");
    return size;
  }
  if (sizeof s <= size) {
    lookup("headertoolong");
    if (html)
      printf("</ul></li>\n");
    return size;
  }
  strncpy(s, ptr, size);
  s[size - 2] = 0;

  name = s;
  value = strchr(s, ':');

  if (s[0] == 0) {
    /* empty header indicates end of headers */
    lookup("endofheaders");
    if (html)
      printf("</ul></li>\n");
    return 0;

  } else if (start) {
    /* Status-Line [6.1] */
    check_status_line(s);
    start = false;

  } else if (!value) {
    lookup("missingcolon");

  } else {
    *value = 0;
    value++;

    check_header(name, skip_lws(value));
  }

  if (html)
    printf("</ul></li>\n");
  return size;
}


/**
 * Callback for received body data.
 *
 * We are not interested in the body, so abort the fetch by returning 0.
 */
size_t data_callback(void *ptr, size_t size, size_t nmemb, void *stream)
{
  UNUSED(ptr);
  UNUSED(size);
  UNUSED(nmemb);
  UNUSED(stream);

  return 0;
}


/**
 * Check the syntax and content of the response Status-Line [6.1].
 */
void check_status_line(const char *s)
{
  const char *reason;
  unsigned int major = 0, minor = 0;
  int r;
  regmatch_t pmatch[5];

  r = regexec(&re_status_line, s, 5, pmatch, 0);
  if (r) {
    lookup("badstatusline");
    return;
  }

  major = atoi(s + pmatch[1].rm_so);
  minor = atoi(s + pmatch[2].rm_so);
  status_code = atoi(s + pmatch[3].rm_so);
  reason = s + pmatch[4].rm_so;

  if (major < 1 || (major == 1 && minor == 0)) {
    lookup("oldhttp");
  } else if ((major == 1 && 1 < minor) || 1 < major) {
    lookup("futurehttp");
  } else {
    if (status_code < 100 || 600 <= status_code) {
      lookup("badstatus");
    } else {
      char key[] = "xxx";
      key[0] = '0' + status_code / 100;
      lookup(key);
    }
  }
}


/**
 * Check the syntax and content of a header.
 */
void check_header(const char *name, const char *value)
{
  struct header_entry *header;

  header = bsearch(name, header_table,
      sizeof header_table / sizeof header_table[0],
      sizeof header_table[0],
      (int (*)(const void *, const void *)) strcasecmp);

  if (header) {
    header->count++;
    header->handler(value);
  } else if ((name[0] == 'X' || name[0] == 'x') && name[1] == '-') {
    lookup("xheader");
  } else {
    lookup("nonstandard");
  }
}


/**
 * Attempt to parse an HTTP Full Date (3.3.1), returning true on success.
 */
bool parse_date(const char *s, struct tm *tm)
{
  int r;
  int len = strlen(s);
  regmatch_t pmatch[20];

  tm->tm_isdst = 0;
  tm->tm_gmtoff = 0;
  tm->tm_zone = "GMT";

  if (len == 29) {
    /* RFC 1123 */
    r = regexec(&re_rfc1123, s, 20, pmatch, 0);
    if (r == 0) {
      tm->tm_mday = atoi(s + pmatch[2].rm_so);
      tm->tm_mon = month(s + pmatch[3].rm_so);
      tm->tm_year = atoi(s + pmatch[4].rm_so) - 1900;
      tm->tm_hour = atoi(s + pmatch[5].rm_so);
      tm->tm_min = atoi(s + pmatch[6].rm_so);
      tm->tm_sec = atoi(s + pmatch[7].rm_so);
      return true;
    }

  } else if (len == 24) {
    /* asctime() format */
    r = regexec(&re_asctime, s, 20, pmatch, 0);
    if (r == 0) {
      if (s[pmatch[3].rm_so] == ' ')
        tm->tm_mday = atoi(s + pmatch[3].rm_so + 1);
      else
        tm->tm_mday = atoi(s + pmatch[3].rm_so);
      tm->tm_mon = month(s + pmatch[2].rm_so);
      tm->tm_year = atoi(s + pmatch[7].rm_so) - 1900;
      tm->tm_hour = atoi(s + pmatch[4].rm_so);
      tm->tm_min = atoi(s + pmatch[5].rm_so);
      tm->tm_sec = atoi(s + pmatch[6].rm_so);
      lookup("asctime");
      return true;
    }

  } else {
    /* RFC 1036 */
    r = regexec(&re_rfc1036, s, 20, pmatch, 0);
    if (r == 0) {
      tm->tm_mday = atoi(s + pmatch[2].rm_so);
      tm->tm_mon = month(s + pmatch[3].rm_so);
      tm->tm_year = 100 + atoi(s + pmatch[4].rm_so);
      tm->tm_hour = atoi(s + pmatch[5].rm_so);
      tm->tm_min = atoi(s + pmatch[6].rm_so);
      tm->tm_sec = atoi(s + pmatch[7].rm_so);
      lookup("rfc1036");
      return true;
    }

  }

  lookup("baddate");
  return false;
}


/**
 * Convert a month name to the month number.
 */
int month(const char *s)
{
  switch (s[0]) {
    case 'J':
      switch (s[1]) {
        case 'a':
          return 0;
        case 'u':
          return s[2] == 'n' ? 5 : 6;
      }
    case 'F':
      return 1;
    case 'M':
      return s[2] == 'r' ? 2 : 4;
    case 'A':
      return s[1] == 'p' ? 3 : 7;
    case 'S':
      return 8;
    case 'O':
      return 9;
    case 'N':
      return 10;
    case 'D':
      return 11;
  }
  return 0;
}


/**
 * UTC version of mktime, from
 *   http://lists.debian.org/deity/2002/deity-200204/msg00082.html
 */
time_t mktime_from_utc(struct tm *t)
{
  time_t tl, tb;
  struct tm *tg;

  tl = mktime (t);
  if (tl == -1)
    {
      t->tm_hour--;
      tl = mktime (t);
      if (tl == -1)
        return -1; /* can't deal with output from strptime */
      tl += 3600;
    }
  tg = gmtime (&tl);
  tg->tm_isdst = 0;
  tb = mktime (tg);
  if (tb == -1)
    {
      tg->tm_hour--;
      tb = mktime (tg);
      if (tb == -1)
        return -1; /* can't deal with output from gmtime */
      tb += 3600;
    }
  return (tl - (tb - tl));
}


/**
 * Skip optional LWS (linear white space) [2.2]
 */
const char *skip_lws(const char *s)
{
  if (s[0] == 13 && s[1] == 10 && (s[2] == ' ' || s[2] == '\t'))
    s += 2;
  while (*s == ' ' || *s == '\t')
    s++;
  return s;
}


/**
 * Parse a list of elements (#rule in [2.1]).
 */
bool parse_list(const char *s, regex_t *preg, unsigned int n, unsigned int m,
    void (*callback)(const char *s, regmatch_t pmatch[]))
{
  int r;
  unsigned int items = 0;
  regmatch_t pmatch[20];

  do {
    r = regexec(preg, s, 20, pmatch, 0);
    if (r) {
      if (html)
        printf("<li class='error'>");
      printf("    Failed to match list item %i\n", items + 1);
      if (html)
        printf("</li>\n");
      return false;
    }

    if (callback)
      callback(s, pmatch);
    items++;

    s += pmatch[0].rm_eo;
    s = skip_lws(s);
    if (*s == 0)
      break;
    if (*s != ',') {
      if (html)
        printf("<li class='error'>");
      printf("    Expecting , after list item %i\n", items);
      if (html)
        printf("</li>\n");
      return false;
    }
    while (*s == ',')
      s = skip_lws(s + 1);
  } while (*s != 0);

  if (items < n || m < items) {
    if (html)
      printf("<li class='error'>");
    printf("    %i items in list, but there should be ", items);
    if (m == UINT_MAX)
      printf("at least %i\n", n);
    else
      printf("between %i and %i\n", n, m);
    if (html)
      printf("</li>\n");
    return false;
  }

  return true;
}


/* Header-specific validation. */
void header_accept_ranges(const char *s)
{
  if (strcmp(s, "bytes") == 0)
    lookup("ok");
  else if (strcmp(s, "none") == 0)
    lookup("ok");
  else
    lookup("unknownrange");
}

void header_age(const char *s)
{
  if (s[0] == 0 || strspn(s, NUMBER) != strlen(s))
    lookup("badage");
  else
    lookup("ok");
}

void header_allow(const char *s)
{
  if (parse_list(s, &re_token, 0, UINT_MAX, 0))
    lookup("ok");
  else
    lookup("badallow");
}

void header_cache_control(const char *s)
{
  if (parse_list(s, &re_token_value, 1, UINT_MAX,
      header_cache_control_callback))
    lookup("ok");
  else
    lookup("badcachecont");
}

char cache_control_list[][20] = {
  "max-age", "max-stale", "min-fresh", "must-revalidate",
  "no-cache", "no-store", "no-transform", "only-if-cached",
  "private", "proxy-revalidate", "public", "s-maxage"
};

void header_cache_control_callback(const char *s, regmatch_t pmatch[])
{
  size_t len = pmatch[1].rm_eo - pmatch[1].rm_so;
  char name[20];
  char *dir;

  if (19 < len) {
    lookup("unknowncachecont");
    return;
  }

  strncpy(name, s + pmatch[1].rm_so, len);
  name[len] = 0;

  dir = bsearch(name, cache_control_list,
      sizeof cache_control_list / sizeof cache_control_list[0],
      sizeof cache_control_list[0],
      (int (*)(const void *, const void *)) strcasecmp);

  if (!dir) {
    if (html)
      printf("<li class='warning'>");
    printf("    Cache-Control directive '");
    print(name, strlen(name));
    printf("':\n");
    if (html)
      printf("</li>\n");
    lookup("unknowncachecont");
  }
}

void header_connection(const char *s)
{
  if (strcmp(s, "close") == 0)
    lookup("ok");
  else
    lookup("badconnection");
}

void header_content_encoding(const char *s)
{
  if (parse_list(s, &re_token, 1, UINT_MAX,
      header_content_encoding_callback))
    lookup("ok");
  else
    lookup("badcontenc");
}

char content_coding_list[][20] = {
  "compress", "deflate", "gzip", "identity"
};

void header_content_encoding_callback(const char *s, regmatch_t pmatch[])
{
  size_t len = pmatch[1].rm_eo - pmatch[1].rm_so;
  char name[20];
  char *dir;

  if (19 < len) {
    lookup("unknowncontenc");
    return;
  }

  strncpy(name, s + pmatch[1].rm_so, len);
  name[len] = 0;

  dir = bsearch(name, content_coding_list,
      sizeof content_coding_list / sizeof content_coding_list[0],
      sizeof content_coding_list[0],
      (int (*)(const void *, const void *)) strcasecmp);
  if (!dir) {
    if (html)
      printf("<li class='warning'>");
    printf("    Content-Encoding '%s':\n", name);
    if (html)
      printf("</li>\n");
    lookup("unknowncontenc");
  }
}

void header_content_language(const char *s)
{
  if (parse_list(s, &re_token, 1, UINT_MAX, 0))
    lookup("ok");
  else
    lookup("badcontlang");
}

void header_content_length(const char *s)
{
  if (s[0] == 0 || strspn(s, NUMBER) != strlen(s))
    lookup("badcontlen");
  else
    lookup("ok");
}

void header_content_location(const char *s)
{
  if (strchr(s, ' '))
    lookup("badcontloc");
  else
    lookup("ok");
}

void header_content_md5(const char *s)
{
  if (strlen(s) != 24)
    lookup("badcontmd5");
  else
    lookup("ok");
}

void header_content_range(const char *s)
{
  UNUSED(s);
  lookup("contentrange");
}

void header_content_type(const char *s)
{
  bool charset = false;
  char *type, *subtype;
  unsigned int i;
  int r;
  regmatch_t pmatch[30];

  r = regexec(&re_content_type, s, 30, pmatch, 0);
  if (r) {
    lookup("badcontenttype");
    return;
  }

  type = strndup(s + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so);
  subtype = strndup(s + pmatch[2].rm_so, pmatch[2].rm_eo - pmatch[2].rm_so);

  /* parameters */
  for (i = 3; i != 30 && pmatch[i].rm_so != -1; i += 3) {
    char *attrib, *value;

    attrib = strndup(s + pmatch[i + 1].rm_so,
        pmatch[i + 1].rm_eo - pmatch[i + 1].rm_so);
    value = strndup(s + pmatch[i + 2].rm_so,
        pmatch[i + 2].rm_eo - pmatch[i + 2].rm_so);

    if (strcasecmp(attrib, "charset") == 0)
      charset = true;
  }

  if (strcasecmp(type, "text") == 0 && !charset)
    lookup("nocharset");
  else
    lookup("ok");
}

void header_date(const char *s)
{
  double diff;
  time_t time0, time1;
  struct tm tm;

  time0 = time(0);
  if (!parse_date(s, &tm))
    return;
  time1 = mktime_from_utc(&tm);

  diff = difftime(time0, time1);
  if (10 < fabs(diff))
    lookup("wrongdate");
  else
    lookup("ok");
}

void header_etag(const char *s)
{
  int r;
  r = regexec(&re_etag, s, 0, 0, 0);
  if (r)
    lookup("badetag");
  else
    lookup("ok");
}

void header_expires(const char *s)
{
  struct tm tm;
  if (parse_date(s, &tm))
    lookup("ok");
}

void header_last_modified(const char *s)
{
  double diff;
  time_t time0, time1;
  struct tm tm;

  time0 = time(0);
  if (!parse_date(s, &tm))
    return;
  time1 = mktime_from_utc(&tm);

  diff = difftime(time1, time0);
  if (10 < diff)
    lookup("futurelastmod");
  else
    lookup("ok");
}

void header_location(const char *s)
{
  int r;
  r = regexec(&re_absolute_uri, s, 0, 0, 0);
  if (r)
    lookup("badlocation");
  else
    lookup("ok");
}

void header_pragma(const char *s)
{
  if (parse_list(s, &re_token_value, 1, UINT_MAX, 0))
    lookup("ok");
  else
    lookup("badpragma");
}

void header_retry_after(const char *s)
{
  struct tm tm;

  if (s[0] != 0 && strspn(s, NUMBER) == strlen(s)) {
    lookup("ok");
    return;
  }

  if (!parse_date(s, &tm))
    return;

  lookup("ok");
}

void header_server(const char *s)
{
  int r;
  r = regexec(&re_server, s, 0, 0, 0);
  if (r)
    lookup("badserver");
  else
    lookup("ok");
}

void header_trailer(const char *s)
{
  if (parse_list(s, &re_token, 1, UINT_MAX, 0))
    lookup("ok");
  else
    lookup("badtrailer");
}

void header_transfer_encoding(const char *s)
{
  if (parse_list(s, &re_transfer_coding, 1, UINT_MAX,
      header_transfer_encoding_callback))
    lookup("ok");
  else
    lookup("badtransenc");
}

char transfer_coding_list[][20] = {
  "chunked", "compress", "deflate", "gzip", "identity"
};

void header_transfer_encoding_callback(const char *s, regmatch_t pmatch[])
{
  size_t len = pmatch[1].rm_eo - pmatch[1].rm_so;
  char name[20];
  char *dir;

  if (19 < len) {
    lookup("unknowntransenc");
    return;
  }

  strncpy(name, s + pmatch[1].rm_so, len);
  name[len] = 0;

  dir = bsearch(name, transfer_coding_list,
      sizeof transfer_coding_list / sizeof transfer_coding_list[0],
      sizeof transfer_coding_list[0],
      (int (*)(const void *, const void *)) strcasecmp);
  if (!dir) {
    if (html)
      printf("<li class='warning'>");
    printf("    Transfer-Encoding '%s':\n", name);
    if (html)
      printf("</li>\n");
    lookup("unknowntransenc");
  }
}

void header_upgrade(const char *s)
{
  int r;
  r = regexec(&re_upgrade, s, 0, 0, 0);
  if (r)
    lookup("badupgrade");
  else
    lookup("ok");
}

void header_vary(const char *s)
{
  if (strcmp(s, "*") == 0 || parse_list(s, &re_token, 1, UINT_MAX, 0))
    lookup("ok");
  else
    lookup("badvary");
}

void header_via(const char *s)
{
  UNUSED(s);
  lookup("via");
}

/* http://wp.netscape.com/newsref/std/cookie_spec.html */
void header_set_cookie(const char *s)
{
  bool ok = true;
  int r;
  const char *semi = strchr(s, ';');
  const char *s2;
  struct tm tm;
  double diff;
  time_t time0, time1;
  regmatch_t pmatch[20];

  if (semi)
    s2 = strndup(s, semi - s);
  else
    s2 = s;

  r = regexec(&re_cookie_nameval, s2, 0, 0, 0);
  if (r) {
    lookup("cookiebadnameval");
    ok = false;
  }

  if (!semi)
    return;

  s = skip_lws(semi + 1);

  while (*s) {
    semi = strchr(s, ';');
    if (semi)
      s2 = strndup(s, semi - s);
    else
      s2 = s;

    if (strncasecmp(s2, "expires=", 8) == 0) {
      s2 += 8;
      r = regexec(&re_cookie_expires, s2, 20, pmatch, 0);
      if (r == 0) {
        tm.tm_mday = atoi(s2 + pmatch[2].rm_so);
        tm.tm_mon = month(s2 + pmatch[3].rm_so);
        tm.tm_year = atoi(s2 + pmatch[4].rm_so) - 1900;
        tm.tm_hour = atoi(s2 + pmatch[5].rm_so);
        tm.tm_min = atoi(s2 + pmatch[6].rm_so);
        tm.tm_sec = atoi(s2 + pmatch[7].rm_so);

        time0 = time(0);
        time1 = mktime_from_utc(&tm);

        diff = difftime(time0, time1);
        if (10 < diff) {
          lookup("cookiepastdate");
          ok = false;
        }
      } else {
        lookup("cookiebaddate");
        ok = false;
      }
    } else if (strncasecmp(s2, "domain=", 7) == 0) {
    } else if (strncasecmp(s2, "path=", 5) == 0) {
      if (s2[5] != '/') {
        lookup("cookiebadpath");
        ok = false;
      }
    } else if (strcasecmp(s, "secure") == 0) {
    } else {
      if (html)
        printf("<li class='warning'>");
      printf("    Set-Cookie field '%s':\n", s2);
      if (html)
        printf("</li>\n");
      lookup("cookieunknownfield");
      ok = false;
    }

    if (semi)
      s = skip_lws(semi + 1);
    else
      break;
  }

  if (ok)
    lookup("ok");
}


/**
 * Print an error message and exit.
 */
void die(const char *error)
{
  fprintf(stderr, "httplint: %s\n", error);
  exit(EXIT_FAILURE);
}


/**
 * Print a string which contains control characters.
 */
void print(const char *s, size_t len)
{
  size_t i;
  for (i = 0; i != len; i++) {
    if (html && s[i] == '<')
      printf("&lt;");
    else if (html && s[i] == '>')
      printf("&gt;");
    else if (html && s[i] == '&')
      printf("&amp;");
    else if (31 < s[i] && s[i] < 127)
      putchar(s[i]);
    else {
      if (html)
        printf("<span class='cc'>");
      printf("[%.2x]", s[i]);
      if (html)
        printf("</span>");
    }
  }
}


struct message_entry {
  const char key[20];
  const char *value;
} message_table[] = {
  { "1xx", "A response status code in the range 100 - 199 indicates a "
           "'provisional response'." },
  { "2xx", "A response status code in the range 200 - 299 indicates that "
           "the request was successful." },
  { "3xx", "A response status code in the range 300 - 399 indicates that "
           "the client should redirect to a new URL." },
  { "4xx", "A response status code in the range 400 - 499 indicates that "
           "the request could not be fulfilled due to client error." },
  { "5xx", "A response status code in the range 500 - 599 indicates that "
           "an error occurred on the server." },
  { "asctime", "Warning: This date is in the obsolete asctime() format. "
               "Consider using the RFC 1123 format instead." },
  { "badage", "Error: The Age header must be one number." },
  { "badallow", "Error: The Allow header must be a comma-separated list of "
                "HTTP methods." },
  { "badcachecont", "Error: The Cache-Control header must be a "
                    "comma-separated list of directives." },
  { "badconnection", "Warning: The only value of the Connection header "
                     "defined by HTTP/1.1 is \"close\"." },
  { "badcontenc", "Error: The Content-Encoding header must be a "
                  "comma-separated list of encodings." },
  { "badcontenttype", "Error: The Content-Type header must be of the form "
                      "'type/subtype (; optional parameters)'." },
  { "badcontlang", "Error: The Content-Language header must be a "
                   "comma-separated list of language tags." },
  { "badcontlen", "Error: The Content-Length header must be a number." },
  { "badcontloc", "Error: The Content-Location header must be an absolute "
                  "or relative URI." },
  { "badcontmd5", "Error: The Content-MD5 header must be a base64 encoded "
                  "MD5 sum." },
  { "baddate", "Error: Failed to parse this date. Dates should be in the RFC "
               "1123 format." },
  { "badetag", "Error: The ETag header must be a quoted string (optionally "
               "preceded by \"W/\" for a weak tag)." },
  { "badlocation", "Error: The Location header must be an absolute URI. "
                   "Relative URIs are not permitted." },
  { "badpragma", "Error: The Pragma header must be a comma-separated list of "
                 "directives." },
  { "badserver", "Error: The Server header must be a space-separated list of "
                 "products of the form Name/optional-version and comments "
                 "in ()." },
  { "badstatus", "Warning: The response status code is outside the standard "
                 "range 100 - 599." },
  { "badstatusline", "Error: Failed to parse the response Status-Line. The "
                     "status line must be of the form 'HTTP/n.n <3-digit "
                     "status> <reason phrase>'." },
  { "badtrailer", "Error: The Trailer header must be a comma-separated list "
                  "of header names." },
  { "badtransenc", "Error: The Transfer-Encoding header must be a "
                   "comma-separated of encodings." },
  { "badupgrade", "Error: The Upgrade header must be a comma-separated list "
                  "of product identifiers." },
  { "badvary", "Error: The Vary header must be a comma-separated list "
                  "of header names, or \"*\"." },
  { "contentrange", "Warning: The Content-Range header should not be returned "
                    "by the server for this request." },
  { "cookiebaddate", "Error: The expires date must be in the form "
                     "\"Wdy, DD-Mon-YYYY HH:MM:SS GMT\"." },
  { "cookiebadnameval", "Error: A Set-Cookie header must start with "
                        "name=value, each excluding semi-colon, comma and "
                        "white space." },
  { "cookiebadpath", "Error: The path does not start with \"/\"." },
  { "cookiepastdate", "Warning: The expires date is in the past. The cookie "
                      "will be deleted by browsers." },
  { "cookieunknownfield", "Warning: This is not a standard Set-Cookie "
                          "field." },
  { "endofheaders", "End of headers." },
  { "futurehttp", "Warning: I only understand HTTP/1.1. Check for a newer "
                  "version of this tool." },
  { "futurelastmod", "Error: The specified Last-Modified date-time is in "
                     "the future." },
  { "headertoolong", "Warning: Header too long: ignored." },
  { "missingcolon", "Error: Headers must be of the form 'Name: value'." },
  { "missingcontenttype", "Warning: No Content-Type header was present. The "
                          "client will have to guess the media type or ask "
                          "the user. Adding a Content-Type header is strongly "
                          "recommended." },
  { "missingcontlang", "Consider adding a Content-Language header if "
                       "applicable for this document." },
  { "missingdate", "Warning: No Date header was present. A Date header must "
                   "be present, unless the server does not have a clock, or "
                   "the response is 100, 101, or 500 - 599." },
  { "missinglastmod", "No Last-Modified header was present. The "
                      "HTTP/1.1 specification states that this header should "
                      "be sent whenever feasible." },
  { "nocharset", "Warning: No character set is specified in the Content-Type. "
                 "Clients may assume the default of ISO-8859-1. Consider "
                 "appending '; charset=...'." },
  { "nonstandard", "Warning: I don't know anything about this header. Is it "
                   "a standard HTTP response header?" },
  { "notcrlf", "Error: This header line does not end in CR LF. HTTP requires "
               "that all header lines end with CR LF." },
  { "ok", "OK." },
  { "oldhttp", "Warning: This version of HTTP is obsolete. Consider upgrading "
               "to HTTP/1.1." },
  { "rfc1036", "Warning: This date is in the obsolete RFC 1036 format. "
               "Consider using the RFC 1123 format instead." },
  { "ugly", "This URL appears to contain implementation-specific parts such "
            "as an extension or a query string. This may make the URL liable "
            "to change when the implementation is changed, resulting in "
            "broken links. Consider using URL rewriting or equivalent to "
            "implement a future-proof URL space. See "
            "http://www.w3.org/Provider/Style/URI for more information." },
  { "unknowncachecont", "Warning: This Cache-Control directive is "
                        "non-standard and will have limited support." },
  { "unknowncontenc", "Warning: This is not a standard Content-Encoding." },
  { "unknownrange", "Warning: This range unit is not a standard HTTP/1.1 "
                    "range." },
  { "unknowntransenc", "Warning: This is not a standard Transfer-Encoding." },
  { "via", "This header was added by a proxy, cache or gateway." },
  { "wrongdate", "Warning: The server date-time differs from this system's "
                 "date-time by more than 10 seconds. Check that both the "
                 "system clocks are correct." },
  { "xheader", "This is an extension header. I don't know how to check it." }
};


/**
 * Look up and output the string referenced by a key.
 */
void lookup(const char *key)
{
  const char *s, *spc;
  int x;
  struct message_entry *message;

  message = bsearch(key, message_table,
      sizeof message_table / sizeof message_table[0],
      sizeof message_table[0],
      (int (*)(const void *, const void *)) strcasecmp);
  if (message)
    s = message->value;
  else
    s = key;

  if (html) {
    if (strncmp(s, "Warning:", 8) == 0)
      printf("<li class='warning'>");
    else if (strncmp(s, "Error:", 6) == 0)
      printf("<li class='error'>");
    else if (strncmp(s, "OK", 2) == 0)
      printf("<li class='ok'>");
    else
      printf("<li>");
    for (; *s; s++) {
      if (strncmp(s, "http://", 7) == 0) {
        spc = strchr(s, ' ');
        printf("<a href='%.*s'>%.*s</a>", spc - s, s, spc - s, s);
        s = spc;
      }
      switch (*s) {
        case '<': printf("&lt;"); break;
        case '>': printf("&gt;"); break;
        case '&': printf("&amp;"); break;
        default: printf("%c", *s); break;
      }
    }
    printf("</li>\n");

  } else {
    printf("    ");
    x = 4;
    while (*s) {
      spc = strchr(s, ' ');
      if (!spc)
        spc = s + strlen(s);
      if (75 < x + (spc - s)) {
        printf("\n    ");
        x = 4;
      }
      x += spc - s + 1;
      printf("%.*s ", spc - s, s);
      if (*spc)
        s = spc + 1;
      else
        s = spc;
    }
    printf("\n\n");
  }
}

