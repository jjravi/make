/* C++ Module Mapper Machinery.  Experimental!
WARNING:FSF Assignment for this project in progress
   Copyright (C) 2019 Nathan Sidwell, I guess
Written by Nathan Sidwell <nathan@acm.org> while at FaceBook

This file is part of GNU Make.

GNU Make is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 3 of the License, or (at your option) any later
version.

GNU Make is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* Experimental component to deal with C++ modules.  Perhaps a more
   general plugin archicture is needed to make this acceptable?  Let's
   at least get it to work first.  */

/* Only local connections for now -- unlike GCC's mapper example,
   which also permits ipv6.
   Error behaviour is rather abrupt, and incomplete.  */

extern "C" { 
#include "makeint.h"
#include "os.h"
#include "filedef.h"
#include "variable.h"
#include "dep.h"
#include "job.h"
#include "rule.h"
#include "debug.h"
}

#if defined (HAVE_SYS_WAIT_H) || defined (HAVE_UNION_WAIT)
# include <sys/wait.h>
#endif

#define MAPPER_FOR_GCC 1
#include "cxx-mapper.hh"

#include <stdarg.h>
#include <stdio.h>
#include <string>

#include <cassert>

#ifdef MAKE_CXX_MAPPER

/* Just presume these exist for now.  */
#define HAVE_AF_UNIX 1

#if defined (HAVE_AF_UNIX)
/* socket, bind, listen, accept{4}  */
# define NETWORKING 1
# include <sys/socket.h>
# ifdef HAVE_AF_UNIX
/* sockaddr_un  */
#  include <sys/un.h>
# endif
# include <netinet/in.h>
#endif

#include <sys/select.h>

static void
delete_client (struct client_state *client, unsigned slot)
{
  DB (DB_PLUGIN, ("mapper:%u destroyed\n", client->cix));

  close (client->GetFDRead ());

  delete client;

  if (slot + 1 != num_clients)
    clients[slot] = clients[num_clients-1];
  clients[--num_clients] = NULL; /* Make unreachable.  */
}

void
mapper_clear (void)
{
  if (sock_fd >= 0)
    close (sock_fd);
  sock_fd = -1;
  if (sock_name && sock_name[0] == '=')
    unlink (sock_name + 1);
  free (sock_name);
  sock_name = NULL;

  while (num_clients)
    delete_client (clients[0], 0);

  free (clients);
  clients = NULL;
  num_clients = alloc_clients = 0;
}

/* Set up a new connection.  */
static void
new_client (void)
{
  static unsigned factory = 0;

  struct client_state *client;
  int client_fd = accept (sock_fd, NULL, NULL);
  if (client_fd < 0)
    {
      mapper_clear ();
      return;
    }

  client = new client_state(&rr, client_fd, ++factory);

  if (num_clients == alloc_clients)
    {
      alloc_clients = (alloc_clients ? alloc_clients : 10) * 2;
      clients = (client_state **)xrealloc (clients, alloc_clients * sizeof (*clients));
    }

  DB (DB_PLUGIN, ("mapper:%u connected\n", client->cix));

  clients[num_clients++] = client;
}

void
track_lto_command (void)
{
  clients[0]->num_awaiting++;
}

void
mapper_file_finish (struct file *file)
{
//  do_mapper_file_finish (file, "Make terminated");

  clients[0]->num_awaiting--;
  // DB (DB_JOBS, ("mapper:%u unpausing job %s\n", client->cix,
  // client->job->file->name));
  // if(jobs_paused > 0) {
    // jobs_paused--;
  // }
  if (job_slots)
    job_slots--;
  waiting_clients--;


  if (file->lto_command) {
    if (!clients[0]->num_awaiting)
    {
      // client_write (client, slot);
    
      // TODO: support more than one
      if(clients && clients[0]) {
        if(clients[0]->is_lto_command) {
          if(file->update_status == us_success) {
            clients[0]->InvokedResponse("success");
          }
          else {
            clients[0]->InvokedResponse("failed");
          }
          clients[0]->PrepareToWrite();
        }
      }
    }
  }

}

/* Set bits in READERS for clients we're listening to.  */

int
mapper_pre_pselect (int hwm, fd_set *readers, fd_set *writers)
{
  unsigned ix;

  if (sock_fd >=0)
  {
    if (hwm < sock_fd)
      hwm = sock_fd;
    FD_SET (sock_fd, readers);
    FD_SET (sock_fd, writers);
  }

  for (ix = num_clients; ix--;) {
    if (clients[ix]->GetDirection() == Cody::Server::READING)
    {
      int fd = clients[ix]->GetFDRead();

      if (hwm < fd)
        hwm = fd;
      FD_SET (fd, readers);
    }
    else if (clients[ix]->GetDirection() == Cody::Server::WRITING)
    {
      int fd = clients[ix]->GetFDWrite();

      if (hwm < fd)
        hwm = fd;
      FD_SET (fd, writers);
    }
  }

  return hwm;
}

/* Read data from a client.  Return non-zero if we blocked.  */

static int
client_read (struct client_state *client, unsigned slot)
{

  switch (client->GetDirection ())
  {
  case Cody::Server::READING:
    if (int err = client->Read ()) {
      // fprintf(stderr, "client->Read() err: %d\n", err);
      return !(err == EINTR || err == EAGAIN);
    }

    client->ProcessRequests ();
    if(!client->is_lto_command) {
      client->PrepareToWrite ();
    }
    break;
  
  case Cody::Server::WRITING:
    while (int err = client->Write ()) {
    // fprintf(stderr, "client->Write() err: %d\n", err);
      if (!(err == EINTR || err == EAGAIN)){
        return true;
      }
    }
    client->PrepareToRead ();
    break;
  
  default:
    // We should never get here
    return true;
  }
  
  return false;
}



/* Process bits in READERS for clients that have something for us.  */

int
mapper_post_pselect (int r, fd_set *readers, fd_set *writers)
{
  int blocked = 0;
  unsigned ix;

  if (sock_fd >= 0 && (FD_ISSET (sock_fd, readers) || FD_ISSET (sock_fd, writers)))
    {
      r--;
      new_client ();
      // fprintf(stderr, "new_client here\n");
    }

  if (r)
    /* Do backwards because reading can cause client deletion.  */
    for (ix = num_clients; ix--;)
      if (((clients[ix]->GetDirection() == Cody::Server::READING) && FD_ISSET(clients[ix]->GetFDRead(), readers)) || ((clients[ix]->GetDirection() == Cody::Server::WRITING) && FD_ISSET(clients[ix]->GetFDWrite(), writers)) )
      {

        int cblocked = client_read (clients[ix], ix);

        if(cblocked) {
          delete_client (clients[ix], ix);
        }

        blocked |= cblocked;
      }

  return blocked;
}

pid_t
mapper_wait (int *status)
{
  int r;
  sigset_t empty;
  struct timespec spec;
  struct timespec *specp = NULL;
  
  spec.tv_sec = spec.tv_nsec = 0;
  
  sigemptyset (&empty);
  for (;;)
  {
    fd_set readfds;
    fd_set writefds;
    int hwm = 0;
    
    if (!specp && waiting_clients == num_clients
      && jobs_paused == (job_slots ? job_slots_used : jobserver_tokens)) {
      //do_mapper_file_finish (NULL, "Circular module dependency");
      abort();
    }
    
    FD_ZERO (&readfds);
    FD_ZERO (&writefds);
    hwm = mapper_pre_pselect (0, &readfds, &writefds);
    // jobs_paused++;
    r = pselect (hwm + 1, &readfds, &writefds, NULL, specp, &empty);
    // jobs_paused--;
    if (r < 0)
      switch (errno)
      {
      case EINTR:
        {
          // fprintf(stderr, "pselect r: %d\n", errno);
          
          // TODO: stop when child process dies?
          /* SIGCHLD will show up as an EINTR.  We're in a loop,
             so no need to EINTRLOOP here.  */
          pid_t pid = waitpid ((pid_t)-1, status, WNOHANG);
          if (pid > 0)
            return pid;
        }
        break;
    
      default:
        pfatal_with_name (_("pselect mapper"));
      }
    else if (!r)
      return 0; /* Timed out, but have new suspended job.  */
    else if (mapper_post_pselect (r, &readfds, &writefds))
      specp = &spec;
  }
}

/* Install the implicit rules.  */

static void
mapper_default_rules (void)
{
  static struct pspec rules[] =
    {
      {"\"%\"." MODULE_SUFFIX, "$(" PREFIX_VAR ")%." BMI_SUFFIX "u", ""},
      {"<%>." MODULE_SUFFIX, "$(" PREFIX_VAR ")%." BMI_SUFFIX "s", ""},
      {"%." MODULE_SUFFIX, "$(" PREFIX_VAR ")%." BMI_SUFFIX, ""},

      /* Order Only! */
      {"$(" PREFIX_VAR ")%." BMI_SUFFIX, "| %.o", ""},

      {"$(" PREFIX_VAR ")%." BMI_SUFFIX "u", "%",
       "$(COMPILE.cc) $(call " HEADER_VAR ",\"$*\") $<"},
      {"$(" PREFIX_VAR ")%." BMI_SUFFIX "s", "%",
       "$(COMPILE.cc) $(call " HEADER_VAR ",<$*>) $<"},
	
      {0, 0, 0}
    };
  struct pspec *p;

  define_variable_global (HEADER_VAR, strlen (HEADER_VAR),
			  "-fmodule-header='$1'", o_default, 1, NILF);

  for (p = rules; p->target; p++)
    {
      /* We must expand the PREFIX_VAR now.  */
      if (p->target[0] == '$')
	p->target = xstrdup (variable_expand (p->target));
      if (p->dep[0] == '$')
	p->dep = xstrdup (variable_expand (p->dep));
      install_pattern_rule (p, 0);
    }
}

/* Non-zero if the mapper is running.  */

int
mapper_enabled (void)
{
  return sock_fd >= 0;
}

/* Setup a socket according to bound to the address OPTION.
   Listen for connections.
   Returns non-zero if enabled.  */

int
mapper_setup (const char *coption)
{

  if(!coption) {
    fprintf(stderr, "%s is not defined", MAPPER_VAR);
  }

  char const *errmsg = nullptr;
  std::string option(coption);
  printf("make CXX_MAPPER: %s\n", option.c_str());

  int fd; 

  auto colon = option.find_last_of (':');
  if (colon != option.npos)
  {
    /* Try a hostname:port address.  */
    char const *cptr = option.c_str () + colon;
    char *endp;
    unsigned port = strtoul (cptr + 1, &endp, 10);

    if (port && endp != cptr + 1 && !*endp)
    {
      /* Ends in ':number', treat as ipv6 domain socket.  */
      option.erase (colon);
      fd = Cody::ListenInet6 (&errmsg, option.c_str (), port);
    }
  }

  sock_fd = fd;

  sock_name = (char *)coption;

  /* Force it to be undefined now, and we'll define it per-job.  */
  undefine_variable_global (MAPPER_VAR, strlen (MAPPER_VAR), o_automatic);

  // if (!no_builtin_rules_flag)
  //   mapper_default_rules ();
  return 1;
}

char *
mapper_ident (void *cookie)
{
  char *assn;
  
  if (!sock_name)
    return 0;
  assn = (char *)xmalloc (100);
  sprintf (assn, MAPPER_VAR "=%s?%#lx", sock_name, (unsigned long)cookie);
  return assn;
}

//// namespace MakeJR {

// These do not need to be members
static module_resolver *ConnectRequest (client_state *, module_resolver *,
         std::vector<std::string> &words);
//static int ModuleRepoRequest (Server *, Resolver *,
//			      std::vector<std::string> &words);
//static int ModuleExportRequest (Server *, Resolver *,
//				std::vector<std::string> &words);
//static int ModuleImportRequest (Server *, Resolver *,
//				std::vector<std::string> &words);
//static int ModuleCompiledRequest (Server *, Resolver *,
//				  std::vector<std::string> &words);
//static int IncludeTranslateRequest (Server *, Resolver *,
//				     std::vector<std::string> &words);
static int InvokeSubProcessRequest (client_state *, module_resolver *,
             std::vector<std::string> &words);
//
//namespace {
using RequestFn = int (client_state *, module_resolver *, std::vector<std::string> &);
using RequestPair = std::tuple<char const *, RequestFn *>;
static RequestPair
  const requestTable[Cody::Detail::RC_HWM] =
  {
   // Same order as enum RequestCode
   RequestPair {u8"HELLO", nullptr},
   RequestPair {u8"MODULE-REPO", nullptr},
    RequestPair {u8"MODULE-EXPORT", nullptr},
    RequestPair {u8"MODULE-IMPORT", nullptr},
    RequestPair {u8"MODULE-COMPILED", nullptr},
    RequestPair {u8"INCLUDE-TRANSLATE", nullptr},
   RequestPair {u8"INVOKE", InvokeSubProcessRequest},
  };
//}

module_resolver *ConnectRequest (client_state *s, module_resolver *r,
			  std::vector<std::string> &words)
{
  if (words.size () < 3 || words.size () > 4)
    return nullptr;

  if (words.size () == 3)
    words.emplace_back (u8"");
  char *eptr;
  unsigned long version = strtoul (words[1].c_str (), &eptr, 10);
  if (*eptr)
    return nullptr;

  return r->ConnectRequest (s, unsigned (version), words[2], words[3]);
}

int InvokeSubProcessRequest (client_state *s, module_resolver *r,
			     std::vector<std::string> &args)
{
  if (args.size () < 2 || args[1].empty ())
    return -1;

  return r->InvokeSubProcessRequest (s, args);
}

void client_state::ProcessRequests (void)
  {
    // fprintf(stderr, "client_state ProcessRequests()\n");
    std::vector<std::string> words;

    this->SetDirection(Cody::Server::PROCESSING);

    while (!read.IsAtEnd ())
     {
      int err = 0;
      unsigned ix = Cody::Detail::RC_HWM;
      if (!read.Lex (words))
      {
        assert (!words.empty ());
        while (ix--)
        {
         if (words[0] != std::get<0> (requestTable[ix]))
           continue; // not this one

         if (ix == Cody::Detail::RC_CONNECT)
         {
           // CONNECT
           if (IsConnected ())
             err = -1;
           else if (auto *r = ConnectRequest (this, &rr, words))
             rr = r;
           else
             err = -1;
         }
         else
         {
           if (!IsConnected ())
             err = -1;
           else if (int res = (std::get<1> (requestTable[ix])
               (this, &rr, words)))
             err = res;
         }
         break;
        }
      }

      if (err || ix >= Cody::Detail::RC_HWM)
      {
        // Some kind of error
        std::string msg;

        if (err > 0)
          msg = u8"error processing '";
        else if (ix >= Cody::Detail::RC_HWM)
          msg = u8"unrecognized '";
        else if (IsConnected () && ix == Cody::Detail::RC_CONNECT)
          msg = u8"already connected '";
        else if (!IsConnected () && ix != Cody::Detail::RC_CONNECT)
          msg = u8"not connected '";
        else
          msg = u8"malformed '";

        read.LexedLine (msg);
        msg.append (u8"'");
        if (err > 0)
        {
          msg.append (u8" ");
          msg.append (strerror (err));
        }
        rr.ErrorResponse (this, std::move (msg));
      }
    }
  }



#endif /* MAKE_CXX_MAPPER */
