#pragma once

#include <stdarg.h>

typedef int CURLcode;
typedef struct CURL CURL;
typedef struct curl_slist { int unused; } curl_slist;
typedef struct curl_mime curl_mime;
typedef struct curl_mimepart curl_mimepart;

enum {
    CURLE_OK = 0,
    CURLE_UNSUPPORTED_PROTOCOL = 1,
    CURLE_FAILED_INIT = 2,
    CURLE_COULDNT_RESOLVE_HOST = 6,
    CURLE_OPERATION_TIMEDOUT = 28,
};
enum {
    CURL_GLOBAL_DEFAULT = 0,
    CURLPROTO_HTTPS = 1,
    CURLOPT_URL = 1,
    CURLOPT_PROTOCOLS,
    CURLOPT_REDIR_PROTOCOLS,
    CURLOPT_FOLLOWLOCATION,
    CURLOPT_SSL_VERIFYPEER,
    CURLOPT_SSL_VERIFYHOST,
    CURLOPT_TIMEOUT,
    CURLOPT_CONNECTTIMEOUT,
    CURLOPT_USERAGENT,
    CURLOPT_WRITEFUNCTION,
    CURLOPT_WRITEDATA,
    CURLOPT_POSTFIELDS,
    CURLOPT_HTTPHEADER,
    CURLOPT_RESOLVE,
    CURLOPT_CUSTOMREQUEST,
    CURLOPT_MIMEPOST,
    CURLINFO_RESPONSE_CODE = 100,
};

CURLcode curl_global_init(long flags);
void curl_global_cleanup(void);
CURL *curl_easy_init(void);
void curl_easy_cleanup(CURL *curl);
CURLcode curl_easy_setopt(CURL *curl, int option, ...);
CURLcode curl_easy_perform(CURL *curl);
CURLcode curl_easy_getinfo(CURL *curl, int info, ...);
const char *curl_easy_strerror(CURLcode code);
char *curl_easy_escape(CURL *curl, const char *text, int length);
void curl_free(void *value);
curl_slist *curl_slist_append(curl_slist *list, const char *value);
void curl_slist_free_all(curl_slist *list);
curl_mime *curl_mime_init(CURL *curl);
curl_mimepart *curl_mime_addpart(curl_mime *mime);
CURLcode curl_mime_name(curl_mimepart *part, const char *name);
CURLcode curl_mime_filename(curl_mimepart *part, const char *name);
CURLcode curl_mime_type(curl_mimepart *part, const char *mime_type);
CURLcode curl_mime_filedata(curl_mimepart *part, const char *filename);
void curl_mime_free(curl_mime *mime);
