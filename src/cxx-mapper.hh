#pragma once

#include "mapper.h"

//struct client_state 
//{
//  struct child *job;  /* The job this is for.  */
//
//  char *buf;
//  size_t buf_size;
//  size_t buf_pos;
//
//  unsigned cix;
//  int fd;
//
//  int reading : 1;  /* Filling read buffer.  */
//  int bol : 1;
//  int last : 1;
//  int corking : 16;  /* number of lines, if corked.  */
//
//  struct client_request *requests;
//  unsigned num_requests;
//  unsigned num_awaiting;
//};

static int sock_fd = -1;
static char *sock_name = NULL;
static struct client_state **clients = NULL;
static unsigned num_clients = 0;
static unsigned alloc_clients = 0;
static unsigned waiting_clients = 0;
static module_resolver rr;

extern void track_lto_command (void);

extern int mapper_enabled (void);
extern int mapper_setup (const char *option);
extern void mapper_clear (void);
extern int mapper_pre_pselect (int, fd_set *, fd_set *);
extern int mapper_post_pselect (int, fd_set *, fd_set *);
extern pid_t mapper_wait (int *);
extern char *mapper_ident (void *);

