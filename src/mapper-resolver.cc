/* C++ modules.  Experimental!	-*- c++-mode -*-
   Copyright (C) 2017-2020 Free Software Foundation, Inc.
   Written by Nathan Sidwell <nathan@acm.org> while at FaceBook

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "mapper.h"
// C++
#include <algorithm>
// C
#include <cstring>
// OS
#include <fcntl.h>
///#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

//#include <numeric>

#include <sstream>
#include <iostream>
#include <vector>
#include <iterator>

///////////////////////
extern "C" { 
#include "makeint.h"
#include "os.h"
#include "filedef.h"
#include "variable.h"
#include "dep.h"
#include "job.h"
#include "rule.h"
#include "debug.h"
#include "commands.h"
}
/////////////////////////

module_resolver::module_resolver (bool def)
  : provide_default (def)
{
}

module_resolver::~module_resolver ()
{
//  if (fd_repo >= 0)
//    close (fd_repo);
}

bool
module_resolver::set_repo (std::string &&r, bool force)
{
  if (force || repo.empty ())
    {
      repo = std::move (r);
      force = true;
    }
  return force;
}

bool
module_resolver::add_mapping (std::string &&module, std::string &&file,
			      bool force)
{
  auto res = map.emplace (std::move (module), std::move (file));
  if (res.second)
    force = true;
  else if (force)
    res.first->second = std::move (file);

  return force;
}

int
module_resolver::read_tuple_file (int fd, char const *prefix, bool force)
{
  struct stat stat;
  if (fstat (fd, &stat) < 0)
    return -errno;

  // Just Map the file, we're gonna read all of it, so no need for
  // line buffering
  void *buffer = mmap (nullptr, stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (buffer == MAP_FAILED)
    return -errno;

  size_t prefix_len = prefix ? strlen (prefix) : 0;
  unsigned lineno = 0;

  for (char const *begin = reinterpret_cast <char const *> (buffer),
	 *end = begin + stat.st_size, *eol;
       begin != end; begin = eol + 1)
    {
      lineno++;
      eol = std::find (begin, end, '\n');
      if (eol == end)
	// last line has no \n, ignore the line, you lose
	break;

      auto *pos = begin;
      bool pfx_search = prefix_len != 0;

    pfx_search:
      while (*pos == ' ' || *pos == '\t')
	pos++;

      auto *space = pos;
      while (*space != '\n' && *space != ' ' && *space != '\t')
	space++;

      if (pos == space)
	// at end of line, nothing here	
	continue;

      if (pfx_search)
	{
	  if (size_t (space - pos) == prefix_len
	      && std::equal (pos, space, prefix))
	    pfx_search = false;
	  pos = space;
	  goto pfx_search;
	}

      std::string module (pos, space);
      while (*space == ' ' || *space == '\t')
	space++;
      std::string file (space, eol);

      if (module[0] == '$')
	{
	  if (module == "$root")
	    set_repo (std::move (file));
	  else
	    return lineno;
	}
      else
	{
	  if (file.empty ())
	    file = GetCMIName (module);
	  add_mapping (std::move (module), std::move (file), force);
	}
    }

  munmap (buffer, stat.st_size);

  return 0;
}

std::string 
module_resolver::GetCMIName (std::string const &module)
{
  return "gcm";
}


char const *
module_resolver::GetCMISuffix ()
{
  return "gcm";
}

void 
module_resolver::WaitUntilReady (Cody::Server *s)
{

}

void
module_resolver::ErrorResponse (Cody::Server *server, std::string &&msg)
{
  server->ErrorResponse (msg);
}

module_resolver *
module_resolver::ConnectRequest (Cody::Server *s, unsigned version,
				 std::string &a, std::string &i)
{
  fprintf(stderr, "\tJR: resolve connectrequest\n");

  if (!version || version > Cody::Version)
    s->ErrorResponse ("version mismatch");
  else if (a != "GCC")
    // Refuse anything but GCC
    ErrorResponse (s, std::string ("only GCC supported"));
  else if (!ident.empty () && ident != i)
    // Failed ident check
    ErrorResponse (s, std::string ("bad ident"));
  else
    // Success!
    s->ConnectResponse ("gcc");

  return this;
}

int
module_resolver::ModuleRepoRequest (Cody::Server *s)
{
  s->ModuleRepoResponse (repo);
  return 0;
}

int
module_resolver::cmi_response (Cody::Server *s, std::string &module)
{
  auto iter = map.find (module);
  if (iter == map.end ())
    {
      std::string file;
      if (provide_default)
	file = std::move (GetCMIName (module));
      auto res = map.emplace (module, file);
      iter = res.first;
    }

  if (iter->second.empty ())
    s->ErrorResponse ("no such module");
  else
    s->ModuleCMIResponse (iter->second);

  return 0;
}

int
module_resolver::ModuleExportRequest (Cody::Server *s, std::string &module)
{
  return cmi_response (s, module);
}

int
module_resolver::ModuleImportRequest (Cody::Server *s, std::string &module)
{
  return cmi_response (s, module);
}

int 
module_resolver::ModuleCompiledRequest (Cody::Server *s, std::string &)
{
  s->OKResponse ();
  return 0;
}



int
module_resolver::IncludeTranslateRequest (Cody::Server *s,
					      std::string &include)
{
  auto iter = map.find (include);
  if (iter == map.end ())
    {
      // Not found, look for it
      if (fd_repo == -1 && !repo.empty ())
	{
	  fd_repo = open (repo.c_str (), O_RDONLY | O_CLOEXEC | O_DIRECTORY);
	  if (fd_repo < 0)
	    fd_repo = -2;
	}

      if (fd_repo >= 0 || repo.empty ())
	{
	  auto file = GetCMIName (include);
	  struct stat statbuf;
	  if (fstatat (repo.empty () ? AT_FDCWD : fd_repo,
		       file.c_str (), &statbuf, 0) < 0
	      || !S_ISREG (statbuf.st_mode))
	    // Mark as not present
	    file.clear ();
	  auto res = map.emplace (include, file);
	  iter = res.first;
	}
    }

  if (iter == map.end () || iter->second.empty ())
    s->IncludeTranslateResponse (false);
  else
    s->ModuleCMIResponse (iter->second);

  return 0;
}

std::string getfilename(std::string path)
{
  path = path.substr(path.find_last_of("/\\") + 1);
  size_t dot_i = path.find_last_of('.');
  return path.substr(0, dot_i);
}

int module_resolver::InvokeSubProcessRequest (Cody::Server *s, std::vector<std::string> &args) {

  ////////////////////////////////////////////////////////////////////////////////////////////////////////

  client_state *cs = static_cast<client_state *>(s);

  //std::string temp_cmd = std::accumulate((args).begin()+1, (args).end(), std::string(" "));
  //std::cout << temp_cmd << std::endl;

  std::ostringstream result_stream;
  std::ostream_iterator< std::string > oit( result_stream, " " );
  std::copy( args.begin()+1, args.end(), oit );

  std::string temp_cmd = result_stream.str();

  // fprintf(stderr, "\tJR: resolve lto\n");
  // printf("command: \n");
  // printf("\t%s\n", temp_cmd.c_str());

  char *new_argv = (char *)temp_cmd.c_str();
  ///////////////////////////////////////////////////////////
  std::string lto_file_name = getfilename(args.back());

  char *target_name = (char *)xmalloc (lto_file_name.size());
  struct file *f;
  strcpy (target_name, lto_file_name.c_str());

  f = lookup_file (target_name);

  if (!f)
  {
    f = enter_file (strcache_add (target_name));

    f->phony = 1;
    f->is_target = 1;
    f->last_mtime = NONEXISTENT_MTIME;
    f->mtime_before_update = NONEXISTENT_MTIME;
    f->cmds = (commands *)xmalloc (sizeof (struct commands));

    f->cmds->fileinfo.filenm = 0;
    f->cmds->fileinfo.lineno = 0;
    f->cmds->fileinfo.offset = 0;

    f->cmds->commands = (char *)xmalloc (temp_cmd.length()*sizeof(char));
    strcpy(f->cmds->commands, new_argv);
    // strncpy(f->cmds->commands, new_argv, temp_cmd.length());
    // strncpy(f->cmds->commands, *new_argv, temp_cmd.length());

    // f->cmds->commands = xrealloc (commands, commands_len);
    f->cmds->command_lines = 0;
    f->cmds->recipe_prefix = RECIPEPREFIX_DEFAULT;
    f->lto_command = 1;
    // f->cmds->commands =
    // try_implicit_rule (f, 0);
  }

  execute_file_commands( f );
  free (target_name);

  // TODO
  // client->num_awaiting++;
  ///////////////////////////////////////////////////////////
  // TODO: wait here till the lto finishes

//  while (f->command_state == cs_running) {
//    reap_children (1, 0);
//  }

  // s->lto_state = LTO_RUNNING;

//  printf("lto command done %s:%d\n", __FILE__, __LINE__);
  cs->is_lto_command = true;

  // TODO: send back a compile status response
//  s->InvokedResponse("success");
  return 0;
}

