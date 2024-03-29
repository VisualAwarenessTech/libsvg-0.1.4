/* libsvg - Library for parsing/rendering SVG documents

   Copyright � 2002 USC/Information Sciences Institute

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.
  
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.
  
   You should have received a copy of the GNU Library General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
  
   Author: Carl Worth <cworth@isi.edu>
*/

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include "svgint.h"
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#include <libgen.h>
#include <sys/param.h>
#else
#include <io.h>
#include <memory.h>
#include <stdlib.h>
#define MAXPATHLEN _MAX_PATH
#include <direct.h>
#endif
#include <zlib.h>


static svg_status_t
_svg_init (svg_t *svg);

svg_status_t
svg_create (svg_t **svg)
{
    *svg = malloc (sizeof (svg_t));
    if (*svg == NULL) {
	return SVG_STATUS_NO_MEMORY;
    }

    return _svg_init (*svg);
}

static svg_status_t
_svg_init (svg_t *svg)
{
    svg->dpi = 100;

    svg->dir_name = _strdup (".");

    svg->group_element = NULL;

    _svg_parser_init (&svg->parser, svg);

    svg->engine = NULL;

    svg->element_ids = _svg_xml_hash_create (100);

    return SVG_STATUS_SUCCESS;
}

static svg_status_t
_svg_deinit (svg_t *svg)
{
    free (svg->dir_name);
    svg->dir_name = NULL;

    if (svg->group_element)
	_svg_element_destroy (svg->group_element);

    _svg_parser_deinit (&svg->parser);

    svg->engine = NULL;

    _svg_xml_hash_free (svg->element_ids);

    return SVG_STATUS_SUCCESS;
}

svg_status_t
svg_destroy (svg_t *svg)
{
    svg_status_t status;
    status = _svg_deinit (svg);
    free (svg);

    return status;
}

#define SVG_PARSE_BUFFER_SIZE (8 * 1024)

svg_status_t
svg_parse_file (svg_t *svg, FILE *file)
{
    svg_status_t status = SVG_STATUS_SUCCESS;
    gzFile zfile;
    char buf[SVG_PARSE_BUFFER_SIZE];
    int read;

    zfile = gzdopen (_dup(_fileno(file)), "r");
    if (zfile == NULL) {
	switch (errno) {
	case ENOMEM:
	    return SVG_STATUS_NO_MEMORY;
	case ENOENT:
	    return SVG_STATUS_FILE_NOT_FOUND;
	default:
	    return SVG_STATUS_IO_ERROR;
	}
    }

    status = svg_parse_chunk_begin (svg);
    if (status)
	goto CLEANUP;

    while (! gzeof (zfile)) {
	read = gzread (zfile, buf, SVG_PARSE_BUFFER_SIZE);
	if (read > -1) {
	    status = svg_parse_chunk (svg, buf, read);
	    if (status)
		goto CLEANUP;
	} else {
	    status = SVG_STATUS_IO_ERROR;
	    goto CLEANUP;
	}
    }

    status = svg_parse_chunk_end (svg);

 CLEANUP:
    gzclose (zfile);
    return status;
}

char *
dirname(char *path)
{
	static const char dot[] = ".";
	char *last_slash;
	/* Find last '/'.  */
	last_slash = path != NULL ? strrchr(path, '/') : NULL;
	if (last_slash != NULL && last_slash != path && last_slash[1] == '\0')
	{
		/* Determine whether all remaining characters are slashes.  */
		char *runp;
		for (runp = last_slash; runp != path; --runp)
			if (runp[-1] != '/')
				break;
		/* The '/' is the last character, we have to look further.  */
		if (runp != path)
			last_slash = memchr(path, '/', runp - path);
	}
	if (last_slash != NULL)
	{
		/* Determine whether all remaining characters are slashes.  */
		char *runp;
		for (runp = last_slash; runp != path; --runp)
			if (runp[-1] != '/')
				break;
		/* Terminate the path.  */
		if (runp == path)
		{
			/* The last slash is the first character in the string.  We have to
			   return "/".  As a special case we have to return "//" if there
			   are exactly two slashes at the beginning of the string.  See
			   XBD 4.10 Path Name Resolution for more information.  */
			if (last_slash == path + 1)
				++last_slash;
			else
				last_slash = path + 1;
		}
		else
			last_slash = runp;
		last_slash[0] = '\0';
	}
	else
		/* This assignment is ill-designed but the XPG specs require to
		   return a string containing "." in any case no directory part is
		   found and so a static and constant string is required.  */
		path = (char *)dot;

	return path;

}

svg_status_t
svg_parse (svg_t *svg, const char *filename)
{
    svg_status_t status = SVG_STATUS_SUCCESS;
    FILE *file;
    char *tmp;

    free (svg->dir_name);
    /* awful dirname semantics require some hoops */
    tmp = _strdup (filename);
    svg->dir_name = _strdup (dirname (tmp));
    free (tmp);

//  file = fopen (filename, "r");
	errno_t fstatus = fopen_s(&file, filename, "r");
	if(fstatus)
		if (file == NULL) {
			switch (fstatus) {
				case ENOMEM:
				return SVG_STATUS_NO_MEMORY;
			case ENOENT:
				return SVG_STATUS_FILE_NOT_FOUND;
			default:
			return SVG_STATUS_IO_ERROR;
		}
    }
    status = svg_parse_file (svg, file);
    fclose (file);
    return status;
}

svg_status_t
svg_parse_buffer (svg_t *svg, const char *buf, size_t count)
{
    svg_status_t status;

    status = svg_parse_chunk_begin (svg);
    if (status)
	return status;

    status = svg_parse_chunk (svg, buf, count);
    if (status)
	return status;

    status = svg_parse_chunk_end (svg);

    return status;
}

svg_status_t
svg_parse_chunk_begin (svg_t *svg)
{
    return _svg_parser_begin (&svg->parser);
}

svg_status_t
svg_parse_chunk (svg_t *svg, const char *buf, size_t count)
{
    return _svg_parser_parse_chunk (&svg->parser, buf, count);
}

svg_status_t
svg_parse_chunk_end (svg_t *svg)
{
    return _svg_parser_end (&svg->parser);
}

svg_status_t
svg_render (svg_t		*svg,
	    svg_render_engine_t	*engine,
	    void		*closure)
{
    svg_status_t status;
    char orig_dir[MAXPATHLEN];

    if (svg->group_element == NULL)
	return SVG_STATUS_SUCCESS;

    /* XXX: Currently, the SVG parser doesn't resolve relative URLs
       properly, so I'll just cheese things in by changing the current
       directory -- at least I'll be nice about it and restore it
       afterwards. */

    _getcwd (orig_dir, MAXPATHLEN);
    _chdir (svg->dir_name);
    
    status = svg_element_render (svg->group_element, engine, closure);

    _chdir (orig_dir);

    return status;
}

svg_status_t
_svg_store_element_by_id (svg_t *svg, svg_element_t *element)
{
    _svg_xml_hash_add_entry (svg->element_ids,
			     (unsigned char *)element->id,
			     element);

    return SVG_STATUS_SUCCESS;
}

svg_status_t
_svg_fetch_element_by_id (svg_t *svg, const char *id, svg_element_t **element_ret)
{
    *element_ret = _svg_xml_hash_lookup (svg->element_ids, (unsigned char *)id);

    return SVG_STATUS_SUCCESS;
}

void
svg_get_size (svg_t *svg, svg_length_t *width, svg_length_t *height)
{
    if (svg->group_element) {
	_svg_group_get_size (&svg->group_element->e.group, width, height);
    } else {
	_svg_length_init (width, 0.0);
	_svg_length_init (height, 0.0);
    }
}
