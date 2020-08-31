/* C++ modules.  Experimental!	-*- c++ -*-
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

// Mapper interface for client and server bits
#include "cody.hh"
// C++
#include <string>
#include <map>

#define MAPPER_VERSION 1

#define HEADER_VAR "CXX_MODULE_HEADER"
#define MAPPER_VAR "CXX_MODULE_MAPPER"
#define PREFIX_VAR "CXX_MODULE_PREFIX"
#define MODULE_SUFFIX "c++m"
#define BMI_SUFFIX "gcm"

enum response_codes
{
  CC_HANDSHAKE,
  CC_IMPORT,
  CC_INCLUDE,
  CC_EXPORT,
  CC_LTOCOMPILE,
  CC_DONE,
  CC_ERROR,
  CC_TRANSLATE,
  CC_IMPORTING,
};

struct client_request
{
  enum response_codes code : 8;
  union 
  {
    const char *resp;
    struct file *file;
  } u;
};


class client_state : public Cody::Server {

public: 
  unsigned cix;
  unsigned num_awaiting;

  client_state(Cody::Resolver *rr, int fd, int ix) : Cody::Server(rr, fd)
  {
    cix=ix;
    num_awaiting=0;
  }

};

typedef unsigned location_t;

// This is a GCC class, so GCC coding conventions on new bits.  
class module_resolver : public Cody::Resolver
{
public:
  using parent = Cody::Resolver;
  using module_map = std::map<std::string, std::string>;

private:
  std::string repo;
  std::string ident;
  module_map map;
  int fd_repo = -1;
  bool provide_default = true;

public:
  module_resolver (bool def = true);
  virtual ~module_resolver () override;

public:
  void set_default (bool d)
  {
    provide_default = d;
  }
  void set_ident (char const *i)
  {
    ident = i;
  }
  bool set_repo (std::string &&repo, bool force = false);
  bool add_mapping (std::string &&module, std::string &&file,
		    bool force = false);

  // Return +ve line number of error, or -ve errno
  int read_tuple_file (int fd, char const *prefix, bool force = false);
  int read_tuple_file (int fd, std::string const &prefix,
			    bool force = false)
  {
    return read_tuple_file (fd, prefix.empty () ? nullptr : prefix.c_str (),
			    force);
  }

public:
  virtual void WaitUntilReady (Cody::Server *s) override;
  virtual void ErrorResponse (Cody::Server *s, std::string &&msg) override;

public:
  // Virtual overriders, names are controlle by Cody::Resolver
  virtual module_resolver *ConnectRequest (Cody::Server *, unsigned version,
					   std::string &agent,
					   std::string &ident)
    override;

  virtual int ModuleRepoRequest (Cody::Server *) override;

  virtual int ModuleExportRequest (Cody::Server *s, std::string &module)
    override;

  virtual int ModuleImportRequest (Cody::Server *s, std::string &module)
    override;

  virtual int ModuleCompiledRequest (Cody::Server *s, std::string &module)
    override;

  virtual int IncludeTranslateRequest (Cody::Server *s, std::string &include)
    override;

  virtual int InvokeSubProcessRequest (Cody::Server *s, std::vector<std::string> &args)
    override;

private:
  virtual std::string GetCMIName (std::string const &module) override;

  virtual char const *GetCMISuffix () override;

private:
  int cmi_response (Cody::Server *s, std::string &module);
};

#ifdef MAPPER_FOR_GCC
#ifndef HAVE_SIGHANDLER_T
typedef void (*sighandler_t) (int);
#endif

class module_client : public Cody::Client
{
//  //  pex_obj *pex = nullptr;
  sighandler_t sigpipe = SIG_IGN;

public:
  module_client (Cody::Server *s)
    : Client (s)
  {
  }

//  //module_client (int fd_from, int fd_to);
//
//  module_client (int fd_from, int fd_to)
//    : Client (fd_from, fd_to)
//  {
//  }
//
//public:
//  static module_client *open_module_client (location_t loc, const char *option,
//					    void (*set_repo) (const char *),
//					    char const *);
//  static void close_module_client (location_t loc, module_client *);
};

#endif
