/*****************************************************************************/
/*  local.c - functions that will use the local system                       */
/*  Copyright (C) 1998-2002 Brian Masney <masneyb@gftp.org>                  */
/*                                                                           */
/*  This program is free software; you can redistribute it and/or modify     */
/*  it under the terms of the GNU General Public License as published by     */
/*  the Free Software Foundation; either version 2 of the License, or        */
/*  (at your option) any later version.                                      */
/*                                                                           */
/*  This program is distributed in the hope that it will be useful,          */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of           */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            */
/*  GNU General Public License for more details.                             */
/*                                                                           */
/*  You should have received a copy of the GNU General Public License        */
/*  along with this program; if not, write to the Free Software              */
/*  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111 USA      */
/*****************************************************************************/

#include "gftp.h"

static void local_destroy 			( gftp_request * request );
static void local_remove_key 			( gpointer key, 
						  gpointer value, 
						  gpointer user_data );
static int local_connect 			( gftp_request * request );
static void local_disconnect 			( gftp_request * request );
static long local_get_file 			( gftp_request * request, 
						  const char *filename,
						  FILE * fd,
						  off_t startsize );
static int local_put_file 			( gftp_request * request, 
						  const char *filename,
						  FILE * fd,
						  off_t startsize,
						  off_t totalsize );
static int local_end_transfer 			( gftp_request * request );
static int local_get_next_file 			( gftp_request * request, 
						  gftp_file * fle, 
						  FILE * fd );
static int local_list_files 			( gftp_request * request );
static off_t local_get_file_size 		( gftp_request * request,
						  const char *filename );
static int local_chdir 				( gftp_request * request, 
						  const char *directory );
static int local_rmdir 				( gftp_request * request, 
						  const char *directory );
static int local_rmfile 			( gftp_request * request, 
						  const char *file );
static int local_mkdir 				( gftp_request * request, 
						  const char *directory );
static int local_rename 			( gftp_request * request, 
						  const char *oldname,
						  const char *newname );
static int local_chmod 				( gftp_request * request, 
						  const char *file, 
						  int mode );
static int local_set_file_time 			( gftp_request * request, 
						  const char *file,
						  time_t datetime );
static char *make_text_mode			( gftp_file * fle,
						  mode_t mode );
static gint hash_compare 			( gconstpointer path1, 
						  gconstpointer path2 );
static guint hash_function 			( gconstpointer key );

typedef struct local_protocol_data_tag
{
  DIR *dir;
  GHashTable *userhash, *grouphash;
} local_protocol_data;


void
local_init (gftp_request * request)
{
  local_protocol_data *lpd;

  g_return_if_fail (request != NULL);

  request->protonum = GFTP_LOCAL_NUM;
  request->init = local_init;
  request->destroy = local_destroy;
  request->connect = local_connect;
  request->disconnect = local_disconnect;
  request->get_file = local_get_file;
  request->put_file = local_put_file;
  request->transfer_file = NULL;
  request->get_next_file_chunk = NULL;
  request->put_next_file_chunk = NULL;
  request->end_transfer = local_end_transfer;
  request->list_files = local_list_files;
  request->get_next_file = local_get_next_file;
  request->set_data_type = NULL;
  request->get_file_size = local_get_file_size;
  request->chdir = local_chdir;
  request->rmdir = local_rmdir;
  request->rmfile = local_rmfile;
  request->mkdir = local_mkdir;
  request->rename = local_rename;
  request->chmod = local_chmod;
  request->set_file_time = local_set_file_time;
  request->site = NULL;
  request->parse_url = NULL;
  request->url_prefix = "file";
  request->protocol_name = "Local";
  request->need_hostport = 0;
  request->need_userpass = 0;
  request->use_cache = 0;
  request->use_threads = 0;
  request->always_connected = 1;
  gftp_set_config_options (request);

  lpd = g_malloc0 (sizeof (*lpd));
  request->protocol_data = lpd;
  lpd->userhash = g_hash_table_new (hash_function, hash_compare);
  lpd->grouphash = g_hash_table_new (hash_function, hash_compare);
}


static void
local_destroy (gftp_request * request)
{
  local_protocol_data * lpd;

  g_return_if_fail (request != NULL);
  g_return_if_fail (request->protonum == GFTP_LOCAL_NUM);

  lpd = request->protocol_data;
  g_hash_table_foreach (lpd->userhash, local_remove_key, NULL);
  g_hash_table_destroy (lpd->userhash);
  g_hash_table_foreach (lpd->grouphash, local_remove_key, NULL);
  g_hash_table_destroy (lpd->grouphash);
  lpd->userhash = lpd->grouphash = NULL;
}


static void
local_remove_key (gpointer key, gpointer value, gpointer user_data)
{
  g_free (value);
}


static int
local_connect (gftp_request * request)
{
  char tempstr[PATH_MAX];

  g_return_val_if_fail (request != NULL, -2);
  g_return_val_if_fail (request->protonum == GFTP_LOCAL_NUM, -2);

  request->sockfd = (void *) 1;

  if (request->directory)
    {
      if (chdir (request->directory) != 0)
        {
          request->logging_function (gftp_logging_error, request->user_data,
                             _("Could not change local directory to %s: %s\n"),
                             request->directory, g_strerror (errno));
        }
      g_free (request->directory);
      request->directory = NULL;
    }

  if (getcwd (tempstr, sizeof (tempstr)) != NULL)
    {
      tempstr[sizeof (tempstr) - 1] = '\0';
      request->directory = g_malloc (strlen (tempstr) + 1);
      strcpy (request->directory, tempstr);
    }
  else
    request->logging_function (gftp_logging_error, request->user_data,
                             _("Could not get current working directory: %s\n"),
                             g_strerror (errno));

  return (0);
}


static void
local_disconnect (gftp_request * request)
{
  g_return_if_fail (request != NULL);
  g_return_if_fail (request->protonum == GFTP_LOCAL_NUM);

  if (request->datafd != NULL)
    {
      if (fclose (request->datafd) < 0)
        request->logging_function (gftp_logging_error, request->user_data,
                                   _("Error closing file descriptor: %s\n"),
                                   g_strerror (errno));
      request->datafd = NULL;
    }
  request->sockfd = NULL;
}


static long 
local_get_file (gftp_request * request, const char *filename, FILE * fd,
                off_t startsize)
{
  size_t size;
  int sock, flags;

  g_return_val_if_fail (request != NULL, -2);
  g_return_val_if_fail (request->protonum == GFTP_LOCAL_NUM, -2);
  g_return_val_if_fail (filename != NULL, -2);

  if (fd == NULL)
    {
      flags = O_RDONLY;
#if defined (_LARGEFILE_SOURCE)
      flags |= O_LARGEFILE;
#endif

      if ((sock = open (filename, flags)) < 0)
        {
          request->logging_function (gftp_logging_error, request->user_data,
                                   _("Error: Cannot open local file %s: %s\n"),
                                   filename, g_strerror (errno));
          return (-2);
        }

      if ((request->datafd = fdopen (sock, "rb")) == NULL)
        {
          request->logging_function (gftp_logging_error, request->user_data,
                                     _("Cannot fdopen() socket for %s: %s\n"),
                                     filename, g_strerror (errno));
          close (sock);
          return (-2);
        }
    }
  else
    request->datafd = fd;

  if (lseek (fileno (request->datafd), 0, SEEK_END) == -1)
    {
      request->logging_function (gftp_logging_error, request->user_data,
                                 _("Error: Cannot seek on file %s: %s\n"),
                                 filename, g_strerror (errno));
      fclose (request->datafd);
      request->datafd = NULL;
    }

  if ((size = ftell (request->datafd)) == -1)
    {
      request->logging_function (gftp_logging_error, request->user_data,
                                 _("Error: Cannot seek on file %s: %s\n"),
                                 filename, g_strerror (errno));
      fclose (request->datafd);
      request->datafd = NULL;
    }

  if (lseek (fileno (request->datafd), startsize, SEEK_SET) == -1)
    {
      request->logging_function (gftp_logging_error, request->user_data,
                                 _("Error: Cannot seek on file %s: %s\n"),
                                 filename, g_strerror (errno));
      fclose (request->datafd);
      request->datafd = NULL;
    }

  return (size);
}


static int
local_put_file (gftp_request * request, const char *filename, FILE * fd,
                off_t startsize, off_t totalsize)
{
  int sock, flags;

  g_return_val_if_fail (request != NULL, -2);
  g_return_val_if_fail (request->protonum == GFTP_LOCAL_NUM, -2);
  g_return_val_if_fail (filename != NULL, -2);

  if (fd == NULL)
    {
      flags = O_WRONLY | O_CREAT;
      if (startsize > 0)
         flags |= O_APPEND;
#if defined (_LARGEFILE_SOURCE)
      flags |= O_LARGEFILE;
#endif

      if ((sock = open (filename, flags, S_IRUSR | S_IWUSR)) < 0)
        {
          request->logging_function (gftp_logging_error, request->user_data,
                                   _("Error: Cannot open local file %s: %s\n"),
                                   filename, g_strerror (errno));
          return (-2);
        }

      if ((request->datafd = fdopen (sock, "ab")) == NULL)
        {
          request->logging_function (gftp_logging_error, request->user_data,
                                     _("Cannot fdopen() socket for %s: %s\n"),
                                     filename, g_strerror (errno));
          close (sock);
          return (-2);
        }
    }
  else
    request->datafd = fd;

  if (ftruncate (fileno (request->datafd), startsize) == -1)
    {
      request->logging_function (gftp_logging_error, request->user_data,
                               _("Error: Cannot truncate local file %s: %s\n"),
                               filename, g_strerror (errno));
      fclose (request->datafd);
      request->datafd = NULL;
      return (-2);
    }
    
  if (fseek (request->datafd, startsize, SEEK_SET) == -1)
    {
      request->logging_function (gftp_logging_error, request->user_data,
                                 _("Error: Cannot seek on file %s: %s\n"),
                                 filename, g_strerror (errno));
      fclose (request->datafd);
      request->datafd = NULL;
      return (-2);
    }
  return (0);
}


static int
local_end_transfer (gftp_request * request)
{
  local_protocol_data * lpd;

  lpd = request->protocol_data;
  if (lpd->dir)
    {
      closedir (lpd->dir);
      lpd->dir = NULL;
    }

  if (request->datafd != NULL)
    {
      if (fclose (request->datafd) < 0)
        request->logging_function (gftp_logging_error, request->user_data,
                                   _("Error closing file descriptor: %s\n"),
                                   g_strerror (errno));

      request->datafd = NULL;
    }

  return (0);
}


static int
local_get_next_file (gftp_request * request, gftp_file * fle, FILE * fd)
{
  local_protocol_data * lpd;
  struct dirent *dirp;
  char *user, *group;
  struct passwd *pw;
  struct group *gr;
  struct stat st;

  /* the struct passwd and struct group are not thread safe. But,
     we're ok here because I have threading turned off for the local
     protocol (see use_threads in local_init above) */
  g_return_val_if_fail (request != NULL, -2);
  g_return_val_if_fail (request->protonum == GFTP_LOCAL_NUM, -2);
  g_return_val_if_fail (fle != NULL, -2);

  lpd = request->protocol_data;
  memset (fle, 0, sizeof (*fle));

  if ((dirp = readdir (lpd->dir)) == NULL)
    {
      closedir (lpd->dir);
      lpd->dir = NULL;
      return (-2);
    }

  fle->file = g_malloc (strlen (dirp->d_name) + 1);
  strcpy (fle->file, dirp->d_name);
  if (lstat (fle->file, &st) != 0)
    {
      closedir (lpd->dir);
      lpd->dir = NULL;
      return (-2);
    }

  if ((user = g_hash_table_lookup (lpd->userhash, 
                                   GUINT_TO_POINTER(st.st_uid))) != NULL)
    {
      fle->user = g_malloc (strlen (user) + 1);
      strcpy (fle->user, user);
    }
  else
    {
      if ((pw = getpwuid (st.st_uid)) == NULL)
        fle->user = g_strdup_printf ("%u", st.st_uid); 
      else
        {
          fle->user = g_malloc (strlen (pw->pw_name) + 1);
          strcpy (fle->user, pw->pw_name);
        }

      user = g_malloc (strlen (fle->user) + 1);
      strcpy (user, fle->user);
      g_hash_table_insert (lpd->userhash, GUINT_TO_POINTER (st.st_uid), user);
    }

  if ((group = g_hash_table_lookup (lpd->grouphash, 
                                    GUINT_TO_POINTER(st.st_gid))) != NULL)
    {
      fle->group = g_malloc (strlen (group) + 1);
      strcpy (fle->group, group);
    }
  else
    {
      if ((gr = getgrgid (st.st_gid)) == NULL)
        fle->group = g_strdup_printf ("%u", st.st_gid); 
      else
        {
          fle->group = g_malloc (strlen (gr->gr_name) + 1);
          strcpy (fle->group, gr->gr_name);
        }

      group = g_malloc (strlen (fle->group) + 1);
      strcpy (group, fle->group);
      g_hash_table_insert (lpd->grouphash, GUINT_TO_POINTER (st.st_gid), group);
    }

  fle->size = st.st_size;
  fle->datetime = st.st_mtime;
  fle->attribs = make_text_mode (fle, st.st_mode);

  if (*fle->attribs == 'd')
    fle->isdir = 1;
  if (*fle->attribs == 'l')
    fle->islink = 1;
  if (strchr (fle->attribs, 'x') != NULL && !fle->isdir && !fle->islink)
    fle->isexe = 1;
  if (*fle->attribs == 'b')
    fle->isblock = 1;
  if (*fle->attribs == 'c')
    fle->ischar = 1;
  if (*fle->attribs == 's')
    fle->issocket = 1;
  if (*fle->attribs == 'p')
    fle->isfifo = 1;
  return (1);
}


static int
local_list_files (gftp_request * request)
{
  local_protocol_data *lpd;
  char *tempstr;

  g_return_val_if_fail (request != NULL, -2);
  g_return_val_if_fail (request->protonum == GFTP_LOCAL_NUM, -2);
  lpd = request->protocol_data;

  tempstr = g_strconcat (request->directory, "/", NULL);

  if ((lpd->dir = opendir (tempstr)) == NULL)
    {
      request->logging_function (gftp_logging_error, request->user_data,
                           _("Could not get local directory listing %s: %s\n"),
                           tempstr, g_strerror (errno));
      g_free (tempstr);
      return (-1);
    }

  g_free (tempstr);
  return (0);
}


static off_t 
local_get_file_size (gftp_request * request, const char *filename)
{
  struct stat st;

  if (stat (filename, &st) == -1)
    return (-1);
  return (st.st_size);
}


static int
local_chdir (gftp_request * request, const char *directory)
{
  char tempstr[255];

  g_return_val_if_fail (request != NULL, -2);
  g_return_val_if_fail (request->protonum == GFTP_LOCAL_NUM, -2);
  g_return_val_if_fail (directory != NULL, -2);

  if (chdir (directory) == 0)
    {
      request->logging_function (gftp_logging_error, request->user_data,
                          _("Successfully changed local directory to %s\n"),
                          directory);
      if (request->directory != directory)
        {
          if (getcwd (tempstr, sizeof (tempstr)) == NULL)
	    {
              request->logging_function (gftp_logging_error, request->user_data,
                            _("Could not get current working directory: %s\n"),
                            g_strerror (errno));
	      return (-1);
	    }
          if (request->directory)
	    g_free (request->directory);
          request->directory = g_malloc (strlen (tempstr) + 1);
          strcpy (request->directory, tempstr);
        }
      return (0);
    }
  request->logging_function (gftp_logging_error, request->user_data,
                             _("Could not change local directory to %s: %s\n"),
                             directory, g_strerror (errno));
  return (-1);
}


static int
local_rmdir (gftp_request * request, const char *directory)
{
  g_return_val_if_fail (request != NULL, -2);
  g_return_val_if_fail (request->protonum == GFTP_LOCAL_NUM, -2);
  g_return_val_if_fail (directory != NULL, -2);

  if (rmdir (directory) == 0)
    {
      request->logging_function (gftp_logging_error, request->user_data,
                                 _("Successfully removed %s\n"), directory);
      return (0);
    }
  else
    {
      request->logging_function (gftp_logging_error, request->user_data,
                              _("Error: Could not remove directory %s: %s\n"),
                              directory, g_strerror (errno));
      return (-1);
    }
}


static int
local_rmfile (gftp_request * request, const char *file)
{
  g_return_val_if_fail (request != NULL, -2);
  g_return_val_if_fail (request->protonum == GFTP_LOCAL_NUM, -2);
  g_return_val_if_fail (file != NULL, -2);

  if (unlink (file) == 0)
    {
      request->logging_function (gftp_logging_error, request->user_data,
                                 _("Successfully removed %s\n"), file);
      return (0);
    }
  else
    {
      request->logging_function (gftp_logging_error, request->user_data,
                                 _("Error: Could not remove file %s: %s\n"),
                                 file, g_strerror (errno));
      return (-1);
    }
}


static int
local_mkdir (gftp_request * request, const char *directory)
{
  g_return_val_if_fail (request != NULL, -2);
  g_return_val_if_fail (request->protonum == GFTP_LOCAL_NUM, -2);
  g_return_val_if_fail (directory != NULL, -2);

  if (mkdir (directory, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == 0)
    {
      request->logging_function (gftp_logging_error, request->user_data,
                                 _("Successfully made directory %s\n"),
                                 directory);
      return (0);
    }
  else
    {
      request->logging_function (gftp_logging_error, request->user_data,
                                 _("Error: Could not make directory %s: %s\n"),
                                 directory, g_strerror (errno));
      return (-1);
    }
}


static int
local_rename (gftp_request * request, const char *oldname,
	      const char *newname)
{
  g_return_val_if_fail (request != NULL, -2);
  g_return_val_if_fail (request->protonum == GFTP_LOCAL_NUM, -2);
  g_return_val_if_fail (oldname != NULL, -2);
  g_return_val_if_fail (newname != NULL, -2);

  if (rename (oldname, newname) == 0)
    {
      request->logging_function (gftp_logging_error, request->user_data,
                                 _("Successfully renamed %s to %s\n"),
                                 oldname, newname);
      return (0);
    }
  else
    {
      request->logging_function (gftp_logging_error, request->user_data,
                                 _("Error: Could not rename %s to %s: %s\n"),
                                 oldname, newname, g_strerror (errno));
      return (-1);
    }
}


static int
local_chmod (gftp_request * request, const char *file, int mode)
{
  char buf[10];
  int newmode;

  g_return_val_if_fail (request != NULL, -2);
  g_return_val_if_fail (request->protonum == GFTP_LOCAL_NUM, -2);
  g_return_val_if_fail (file != NULL, -2);

  g_snprintf (buf, sizeof (buf), "%d", mode);
  newmode = strtol (buf, NULL, 8);

  if (chmod (file, newmode) == 0) 
    {
      request->logging_function (gftp_logging_error, request->user_data, 
                                 _("Successfully changed mode of %s to %d\n"),
                                 file, mode);
      return (0);
    }
  else 
    {
      request->logging_function (gftp_logging_error, request->user_data, 
                          _("Error: Could not change mode of %s to %d: %s\n"),
                          file, mode, g_strerror (errno));
      return (-1);
    }
}


static int
local_set_file_time (gftp_request * request, const char *file,
		     time_t datetime)
{
  struct utimbuf time_buf;

  g_return_val_if_fail (request != NULL, -2);
  g_return_val_if_fail (request->protonum == GFTP_LOCAL_NUM, -2);
  g_return_val_if_fail (file != NULL, -2);

  time_buf.modtime = time_buf.actime = datetime;
  return (utime (file, &time_buf));
}


static char *
make_text_mode (gftp_file * fle, mode_t mode)
{
  char *str;

  str = g_malloc0 (11);
  
  str[0] = '?';
  if (S_ISREG (mode))
    str[0] = '-';

  if (S_ISLNK (mode))
    {
      fle->islink = 1; 
      str[0] = 'l';
    }

  if (S_ISBLK (mode))
     {
       fle->isblock = 1;
       str[0] = 'b';
     }

  if (S_ISCHR (mode))
     {
       fle->ischar = 1;
       str[0] = 'c';
    }

  if (S_ISFIFO (mode))
    {
      fle->isfifo = 1;
      str[0] = 'p';
    }

  if (S_ISSOCK (mode))
    {
      fle->issocket = 1;
      str[0] = 's';
    }

  if (S_ISDIR (mode))
    {
      fle->isdir = 1;
      str[0] = 'd';
    }

  str[1] = mode & S_IRUSR ? 'r' : '-';
  str[2] = mode & S_IWUSR ? 'w' : '-';

  if ((mode & S_ISUID) && (mode & S_IXUSR))
    str[3] = 's';
  else if (mode & S_ISUID)
    str[3] = 'S';
  else if (mode & S_IXUSR)
    str[3] = 'x';
  else
    str[3] = '-';
    
  str[4] = mode & S_IRGRP ? 'r' : '-';
  str[5] = mode & S_IWGRP ? 'w' : '-';

  if ((mode & S_ISGID) && (mode & S_IXGRP))
    str[6] = 's';
  else if (mode & S_ISGID)
    str[6] = 'S';
  else if (mode & S_IXGRP)
    str[6] = 'x';
  else
    str[6] = '-';

  str[7] = mode & S_IROTH ? 'r' : '-';
  str[8] = mode & S_IWOTH ? 'w' : '-';

  if ((mode & S_ISVTX) && (mode & S_IXOTH))
    str[9] = 't';
  else if (mode & S_ISVTX)
    str[9] = 'T';
  else if (mode & S_IXOTH)
    str[9] = 'x';
  else
    str[9] = '-';
  return (str);
}


static gint
hash_compare (gconstpointer path1, gconstpointer path2)
{
  return (GPOINTER_TO_UINT (path1) == GPOINTER_TO_UINT (path2));
}


static guint
hash_function (gconstpointer key)
{
  return (GPOINTER_TO_UINT (key));
}
