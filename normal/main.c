/* main.c - the normal mode main routine */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2000,2001,2002,2003,2005,2006  Free Software Foundation, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <grub/kernel.h>
#include <grub/normal.h>
#include <grub/dl.h>
#include <grub/rescue.h>
#include <grub/misc.h>
#include <grub/file.h>
#include <grub/mm.h>
#include <grub/term.h>
#include <grub/env.h>
#include <grub/parser.h>
#include <grub/script.h>

grub_jmp_buf grub_exit_env;

static grub_fs_module_list_t fs_module_list = 0;

/* The menu to which the new entries are added by the parser.  */
static grub_menu_t current_menu = 0;

#define GRUB_DEFAULT_HISTORY_SIZE	50

/* Read a line from the file FILE.  */
static int
get_line (grub_file_t file, char cmdline[], int max_len)
{
  char c;
  int pos = 0;
  int literal = 0;
  int comment = 0;

  while (1)
    {
      if (grub_file_read (file, &c, 1) != 1)
	break;

      /* Skip all carriage returns.  */
      if (c == '\r')
	continue;

      /* Replace tabs with spaces.  */
      if (c == '\t')
	c = ' ';

      /* The previous is a backslash, then...  */
      if (literal)
	{
	  /* If it is a newline, replace it with a space and continue.  */
	  if (c == '\n')
	    {
	      c = ' ';

	      /* Go back to overwrite the backslash.  */
	      if (pos > 0)
		pos--;
	    }

	  literal = 0;
	}

      if (c == '\\')
	literal = 1;

      if (comment)
	{
	  if (c == '\n')
	    comment = 0;
	}
      else if (pos == 0)
	{
	  if (c == '#')
	    comment = 1;
	  else if (! grub_isspace (c))
	    cmdline[pos++] = c;
	}
      else
	{
	  if (c == '\n')
	    break;

	  if (pos < max_len)
	    cmdline[pos++] = c;
	}
    }

  cmdline[pos] = '\0';
  
  return pos;
}

static void
free_menu (grub_menu_t menu)
{
  grub_menu_entry_t entry = menu->entry_list;
  
  while (entry)
    {
      grub_menu_entry_t next_entry = entry->next;

      grub_script_free (entry->commands);
      grub_free ((void *) entry->title);
      grub_free ((void *) entry->sourcecode);
      entry = next_entry;
    }

  grub_free (menu);
}

grub_err_t
grub_normal_menu_addentry (const char *title, struct grub_script *script,
			   const char *sourcecode)
{
  const char *menutitle;
  grub_menu_entry_t *last = &current_menu->entry_list;

  menutitle = grub_strdup (title);
  if (! menutitle)
    return grub_errno;

  /* Add the menu entry at the end of the list.  */
  while (*last)
    last = &(*last)->next;

  *last = grub_malloc (sizeof (**last));
  if (! *last)
    {
      grub_free ((void *) menutitle);
      grub_free ((void *) sourcecode);
      return grub_errno;
    }

  (*last)->commands = script;
  (*last)->title = menutitle;
  (*last)->next = 0;
  (*last)->sourcecode = sourcecode;

  current_menu->size++;

  return GRUB_ERR_NONE;
}

static grub_menu_t
read_config_file (const char *config)
{
  grub_file_t file;
  auto grub_err_t getline (char **line);
  int currline = 0;
  int errors = 0;
  
  grub_err_t getline (char **line)
    {
      char cmdline[100];
      currline++;

      if (! get_line (file, cmdline, sizeof (cmdline)))
	return 0;
      
      *line = grub_strdup (cmdline);
      if (! *line)
	return grub_errno;

      return GRUB_ERR_NONE;
    }

  char cmdline[100];
  grub_menu_t newmenu;

  newmenu = grub_malloc (sizeof (*newmenu));
  if (! newmenu)
    return 0;
  newmenu->size = 0;
  newmenu->entry_list = 0;
  current_menu = newmenu;

  /* Try to open the config file.  */
  file = grub_file_open (config);
  if (! file)
    return 0;

  while (get_line (file, cmdline, sizeof (cmdline)))
    {
      struct grub_script *parsed_script;
      int startline;
      
      startline = ++currline;

      /* Execute the script, line for line.  */
      parsed_script = grub_script_parse (cmdline, getline);

      if (! parsed_script)
	{
	  grub_printf ("(line %d-%d)\n", startline, currline);
	  errors++;
	  continue;
	}

      /* Execute the command(s).  */
      grub_script_execute (parsed_script);

      /* The parsed script was executed, throw it away.  */
      grub_script_free (parsed_script);
    }

  grub_file_close (file);

  if (errors > 0)
    {
      /* Wait until the user pushes any key so that the user can
	 see what happened.  */
      grub_printf ("\nPress any key to continue...");
      (void) grub_getkey ();
    }

  /* If the menu is empty, just drop it.  */
  if (current_menu->size == 0)
    {
      grub_free (current_menu);
      return 0;
    }

  return newmenu;
}

/* This starts the normal mode.  */
void
grub_enter_normal_mode (const char *config)
{
  if (grub_setjmp (grub_exit_env) == 0)
    grub_normal_execute (config, 0);
}

/* Initialize the screen.  */
void
grub_normal_init_page (void)
{
  grub_cls ();
  grub_printf ("\n\
                         GNU GRUB  version %s\n\n",
	       PACKAGE_VERSION);
}

/* Read the file command.lst for auto-loading.  */
static void
read_command_list (void)
{
  const char *prefix;
  
  prefix = grub_env_get ("prefix");
  if (prefix)
    {
      char *filename;

      filename = grub_malloc (grub_strlen (prefix) + sizeof ("/command.lst"));
      if (filename)
	{
	  grub_file_t file;
	  
	  grub_sprintf (filename, "%s/command.lst", prefix);
	  file = grub_file_open (filename);
	  if (file)
	    {
	      char buf[80]; /* XXX arbitrary */

	      while (get_line (file, buf, sizeof (buf)))
		{
		  char *p;
		  grub_command_t cmd;
		  
		  if (! grub_isgraph (buf[0]))
		    continue;

		  p = grub_strchr (buf, ':');
		  if (! p)
		    continue;

		  *p = '\0';
		  while (*++p == ' ')
		    ;

		  if (! grub_isgraph (*p))
		    continue;

		  cmd = grub_register_command (buf, 0,
					       GRUB_COMMAND_FLAG_NOT_LOADED,
					       0, 0, 0);
		  if (! cmd)
		    continue;

		  cmd->module_name = grub_strdup (p);
		  if (! cmd->module_name)
		    grub_unregister_command (buf);
		}

	      grub_file_close (file);
	    }

	  grub_free (filename);
	}
    }

  /* Ignore errors.  */
  grub_errno = GRUB_ERR_NONE;
}

/* The auto-loading hook for filesystems.  */
static int
autoload_fs_module (void)
{
  grub_fs_module_list_t p;

  while ((p = fs_module_list) != 0)
    {
      if (! grub_dl_get (p->name) && grub_dl_load (p->name))
	return 1;

      fs_module_list = p->next;
      grub_free (p->name);
      grub_free (p);
    }

  return 0;
}

/* Read the file fs.lst for auto-loading.  */
static void
read_fs_list (void)
{
  const char *prefix;
  
  prefix = grub_env_get ("prefix");
  if (prefix)
    {
      char *filename;

      filename = grub_malloc (grub_strlen (prefix) + sizeof ("/fs.lst"));
      if (filename)
	{
	  grub_file_t file;
	  
	  grub_sprintf (filename, "%s/fs.lst", prefix);
	  file = grub_file_open (filename);
	  if (file)
	    {
	      char buf[80]; /* XXX arbitrary */

	      while (get_line (file, buf, sizeof (buf)))
		{
		  char *p = buf;
		  char *q = buf + grub_strlen (buf) - 1;
		  grub_fs_module_list_t fs_mod;
		  
		  /* Ignore space.  */
		  while (grub_isspace (*p))
		    p++;

		  while (p < q && grub_isspace (*q))
		    *q-- = '\0';

		  /* If the line is empty, skip it.  */
		  if (p >= q)
		    continue;

		  fs_mod = grub_malloc (sizeof (*fs_mod));
		  if (! fs_mod)
		    continue;

		  fs_mod->name = grub_strdup (p);
		  if (! fs_mod->name)
		    {
		      grub_free (fs_mod);
		      continue;
		    }

		  fs_mod->next = fs_module_list;
		  fs_module_list = fs_mod;
		}

	      grub_file_close (file);
	    }

	  grub_free (filename);
	}
    }

  /* Ignore errors.  */
  grub_errno = GRUB_ERR_NONE;

  /* Set the hook.  */
  grub_fs_autoload_hook = autoload_fs_module;
}

/* Read the config file CONFIG and execute the menu interface or
   the command-line interface.  */
void
grub_normal_execute (const char *config, int nested)
{
  grub_menu_t menu = 0;

  read_command_list ();
  read_fs_list ();
  
  if (config)
    {
      menu = read_config_file (config);

      /* Ignore any error.  */
      grub_errno = GRUB_ERR_NONE;
    }

  if (menu)
    {
      grub_env_set_data_slot ("menu", menu);
      grub_menu_run (menu, nested);
      grub_env_unset_data_slot ("menu");
      free_menu (menu);
    }
  else
    grub_cmdline_run (nested);
}

/* Enter normal mode from rescue mode.  */
static void
grub_rescue_cmd_normal (int argc, char *argv[])
{
  if (argc == 0)
    {
      /* Guess the config filename. It is necessary to make CONFIG static,
	 so that it won't get broken by longjmp.  */
      static char *config;
      const char *prefix;
      
      prefix = grub_env_get ("prefix");
      if (prefix)
	{
	  config = grub_malloc (grub_strlen (prefix) + sizeof ("/grub.cfg"));
	  if (! config)
	    return;

	  grub_sprintf (config, "%s/grub.cfg", prefix);
	  grub_enter_normal_mode (config);
	  grub_free (config);
	}
      else
	grub_enter_normal_mode (0);
    }
  else
    grub_enter_normal_mode (argv[0]);
}

GRUB_MOD_INIT(normal)
{
  /* Normal mode shouldn't be unloaded.  */
  if (mod)
    grub_dl_ref (mod);

  grub_set_history (GRUB_DEFAULT_HISTORY_SIZE);

  /* Register a command "normal" for the rescue mode.  */
  grub_rescue_register_command ("normal", grub_rescue_cmd_normal,
				"enter normal mode");

  /* This registers some built-in commands.  */
  grub_command_init ();
}

GRUB_MOD_FINI(normal)
{
  grub_set_history (0);
  grub_rescue_unregister_command ("normal");
}

