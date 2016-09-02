#ifndef GITHACK_H
#define GITHACK_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#define __USE_XOPEN
#include <time.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#define _BSD_SOURCE
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <assert.h>
#include <zlib.h>
#include <dirent.h>
#include <errno.h>
#include <sys/select.h>
#include <pthread.h>
#include "http.h"
#include "thpool.h"

#ifndef bool
#   define bool           unsigned char
#endif

#ifndef false
#   define false          (0)
#endif

#ifndef true
#   define true           (!(false))
#endif

#define ENTRY_SIZE   62
#define SHA1_SIZE    160 /* 160 bits*/
#define BUFFER_SIZE  1024
#define BLOB_MAX_LEN 100
#define ESC          "\033"
#define DEFAULT_PORT 80;

typedef struct
{
    unsigned char signature[4];
    unsigned char version[4];
    unsigned char file_num[4];
} magic_hdr, *magic_hdr_t;


struct cache_time
{
    unsigned char sec[4];
    unsigned char nsec[4];
};

struct _stage
{
    int stage_one;
    int stage_two;
};

struct _flags
{
    int assume_valid;
    int extended;
    struct _stage stage;
};

struct _extra_flags
{
    int reserved;
    int skip_worktree;
    int intent_to_add;
    int unused;
};

struct url_combo
{
    char protocol[10];
    char host[BUFFER_SIZE];
    char *uri;
};

typedef struct {
    struct cache_time sd_ctime;
    struct cache_time sd_mtime;
    unsigned char dev[4];
    unsigned char ino[4];
    unsigned char file_mode[4];
    unsigned char uid[4];
    unsigned char gid[4];
    unsigned char size[4];
    unsigned char sha1[20];
    unsigned char ce_flags[2];
} __attribute__ ((packed)) entry_body, *entry_body_t;


typedef struct
{
    entry_body_t entry_body;
    int entry_len;
    char *name;
} ce_body, *ce_body_t;

int hex2dec (unsigned char *hex, int len);

char* sha12hex (unsigned char *sha1);

bool signature_check (magic_hdr_t magic_hdr);

bool version_check (magic_hdr_t magic_hdr);

void init_check (int sockfd, magic_hdr_t  magic_hdr);

int sed2bed (int value);

void pad_entry (int sockfd, int entry_len);

char* get_name (int sockfd, size_t namelen, int *entry_len);

void handle_version3orlater (int sockfd, int *entry_len);

void parse_http_url (char *http_url, struct url_combo *url_combo);

void setnonblocking(int sockfd);

void setblocking(int sockfd);

ssize_t writen(int fd, const void *vptr, size_t n);

void touch_file_et(http_res_t *response, const char *filename, size_t filesize);

int create_dir (const char *sPathName);

void create_all_path_dir(ce_body_t ce_body);

void mk_dir (char *path);

int force_rm_dir(const char *path);

void concat_object_url(entry_body_t entry_bd, char *object_url);

bool check_argv(int argc, char *argv[]);

ssize_t readn(int fd, void *vptr, size_t n);

void parse_index_object (int sockfd);

int strip_http_header (int sockfd);

void task_func (void *arg);

#endif /* GITHACK_H */
