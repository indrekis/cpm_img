// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
/***************************************************************************
 *                                                                         *
 *    LIBDSK: General floppy and diskimage access library                  *
 *    Copyright (C) 2001,2022  John Elliott <seasip.webmaster@gmail.com>   *
 *                                                                         *
 *    This library is free software; you can redistribute it and/or        *
 *    modify it under the terms of the GNU Library General Public          *
 *    License as published by the Free Software Foundation; either         *
 *    version 2 of the License, or (at your option) any later version.     *
 *                                                                         *
 *    This library is distributed in the hope that it will be useful,      *
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of       *
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU    *
 *    Library General Public License for more details.                     *
 *                                                                         *
 *    You should have received a copy of the GNU Library General Public    *
 *    License along with this library; if not, write to the Free           *
 *    Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,      *
 *    MA 02111-1307, USA                                                   *
 *                                                                         *
 ***************************************************************************/
/* Functions to open an image file and select the driver */

#include "drvi.h"
#include "drivers.h"
#include "compress.h"


static DRV_CLASS *classes[] = 
{

#include "drivers.inc"

	NULL
};

#undef strcmpi

#if HAVE_STRCMPI
        /* */
#else
 #if HAVE_STRICMP
  #define strcmpi stricmp
 #else
  #if HAVE_STRCASECMP
   #define strcmpi strcasecmp
  #else

/* Compiler does not provide a strcmpi, do a naive implementation */
int strcmpi(const char *a, const char *b)
{
	while (*a && *b)
	{
		if (toupper(*a) != toupper(*b))
			return toupper(*a) - toupper(*b);
		++a;
		++b;
	}
	/* At least one of *a and *b is 0 */
	return (toupper(*a) - toupper(*b));
}

  #endif
 #endif
#endif


/* [1.5.3] Allow for aliases in driver names. Extend driver name so it's
 * a chain of nul-terminated strings finally terminated by a double-nul */
static int match_drvname(const char *type, DRV_CLASS *dc)
{
	const char *dn = dc->dc_drvname;

	while (*dn)
	{
		if (!strcmpi(type, dn)) 
		{
			return 1;
		}
		dn += 1 + strlen(dn);
	}
	return 0;
}



static void dr_construct(DSK_DRIVER *self, DRV_CLASS *dc)
{
	memset(self, 0, dc->dc_selfsize);
/*	self->dr_forcehead = -1; 	 */
	self->dr_class = dc;
	self->dr_dirty = 0;
	self->dr_compress = NULL;
	self->dr_retry_count = 1;
}


/* Attempt to create a DSK file with driver number <ndrv> */
static dsk_err_t dsk_icreat(DSK_DRIVER **self, const char *filename, int ndrv, COMPRESS_DATA *cd)
{
	DRV_CLASS *dc = classes[ndrv];
	dsk_err_t err;

	if (!dc) return DSK_ERR_BADPTR;
	
	(*self) = dsk_malloc(dc->dc_selfsize);
	if (!*self) return DSK_ERR_NOMEM;
	dr_construct(*self, dc);

	if (dc->dc_creat) err = (dc->dc_creat)(*self, filename);
	else err = DSK_ERR_NOTIMPL;
	if (err == DSK_ERR_OK) 
	{
		(*self)->dr_compress = cd;
		return err;
	}
	dsk_free (*self);
	*self = NULL;
	return err;
}


/* Attempt to open a DSK file with driver <ndrv> */
static dsk_err_t dsk_iopen(DSK_DRIVER **self, const char *filename, int ndrv, 
			COMPRESS_DATA *cd,
			DSK_REPORTFUNC diagfunc,
			DSK_REPORTEND diagend)
{
	DRV_CLASS *dc = classes[ndrv];
	dsk_err_t err;

	/* If we're handling compressed data, use the temporary uncompressed file */
	if (cd) filename = cd->cd_ufilename;

	if (!dc) return DSK_ERR_BADPTR;

	(*self) = dsk_malloc(dc->dc_selfsize);
	if (!*self) return DSK_ERR_NOMEM;
	dr_construct(*self, dc);

	err = (dc->dc_open)(*self, filename, diagfunc);
/*	printf("%s: open %s = %d\n", dc->dc_drvname, filename, err); */
	if (diagend)
	{
		(*diagend)();
	}
	if (err == DSK_ERR_OK) 
	{
		(*self)->dr_compress = cd;
		return err;
	}
	dsk_free (*self);
	*self = NULL;
	return err;
}

/* Create a disc file, of type "type" with name "filename". */
LDPUBLIC32 dsk_err_t LDPUBLIC16 dsk_creat(DSK_DRIVER **self, const char *filename, const char *type,
			const char *compress)
{
	int ndrv;
	dsk_err_t e;
	COMPRESS_DATA *cd = NULL;

	if (!self || !filename || !type) return DSK_ERR_BADPTR;

	dg_custom_init();
	if (compress && strcmp(compress, "none"))
	{	
		e = comp_creat(&cd, filename, compress);
		if (e) return e;
		if (cd) filename = cd->cd_ufilename;
	}

	for (ndrv = 0; classes[ndrv]; ndrv++)
	{
		if (match_drvname(type, classes[ndrv]))
		{
			e = dsk_icreat(self, filename, ndrv, cd);
			if (e != DSK_ERR_OK && cd) comp_abort(&cd);
			return e;
		}
	}
	if (cd) comp_abort(&cd);
	return DSK_ERR_NODRVR;
}


LDPUBLIC32 dsk_err_t LDPUBLIC16 dsk_open(DSK_DRIVER **self, 
			const char *filename, 
			const char *type,
			const char *compress)
{
	return dsk_diagopen(self, filename, type, compress, NULL, NULL);
}


LDPUBLIC32 dsk_err_t LDPUBLIC16 dsk_diagopen(DSK_DRIVER **self, 
			const char *filename, 
			const char *type,
			const char *compress,
			DSK_REPORTFUNC diag,
			DSK_REPORTEND diagend)
{
	int ndrv;
	dsk_err_t e;
	COMPRESS_DATA *cd = NULL;

	if (!self || !filename) return DSK_ERR_BADPTR;

	dg_custom_init();

	/* See if it's compressed */
	if (compress == NULL || strcmp(compress, "none"))
	{
		e = comp_open(&cd, filename, compress);
		if (e != DSK_ERR_OK && e != DSK_ERR_NOTME) return e;
		
		if (type)
		{
			for (ndrv = 0; classes[ndrv]; ndrv++)
			{
				if (match_drvname(type, classes[ndrv]))
				{
					e = dsk_iopen(self, filename, ndrv, cd,
						diag, diagend);
					if (e && cd) comp_abort(&cd);
					return e;
				}
			}
			if (cd) comp_abort(&cd);
			return DSK_ERR_NODRVR;
		}
	}
	for (ndrv = 0; classes[ndrv]; ndrv++)
	{
		e = dsk_iopen(self, filename, ndrv, cd, 
				diag, diagend);
		if (e != DSK_ERR_NOTME) 
		{
			if (e != DSK_ERR_OK && cd) comp_abort(&cd);
			return e;
		}
	}	
	if (cd) comp_abort(&cd);
	return DSK_ERR_NOTME;
}

/* Close a DSK file. Frees the pointer and sets it to null. */

LDPUBLIC32 dsk_err_t LDPUBLIC16 dsk_close(DSK_DRIVER **self)
{
	dsk_err_t e, e2;
	COMPRESS_DATA *dc;
	DSK_OPTION *opt, *opt2;

	if (!self || (!(*self)) || (!(*self)->dr_class))    return DSK_ERR_BADPTR;

	e = ((*self)->dr_class->dc_close)(*self);

	dc = (*self)->dr_compress;
	if (dc)
	{
		if ((*self)->dr_dirty) e2 = comp_commit(&dc); 
		else		       e2 = comp_abort (&dc);

		if (!e) e = e2;
	}
	/* Free any option blocks the driver has accumulated */
	opt = (*self)->dr_options;
	while (opt)
	{
		opt2 = opt->do_next;
		dsk_free(opt);
		opt = opt2;
	}
	/* And any comments */
	dsk_set_comment(*self, NULL);
	dsk_free (*self);
	*self = NULL;
	return e;
}

/* If "index" is in range, returns the n'th driver name in (*drvname).
 * Else sets (*drvname) to null. */
LDPUBLIC32 dsk_err_t LDPUBLIC16 dsk_type_enum(int index, char **drvname)
{
	int ndrv;

	if (!drvname) return DSK_ERR_BADPTR;

	for (ndrv = 0; classes[ndrv]; ndrv++)
	{
		if (index == ndrv)
		{
			*drvname = classes[ndrv]->dc_drvname;
			return DSK_ERR_OK;
		}
	}
	*drvname = NULL;
	return DSK_ERR_NODRVR;
}


/* If "index" is in range, returns the n'th driver description in (*drvdesc).
 * Else sets (*drvdesc) to null. */
LDPUBLIC32 dsk_err_t LDPUBLIC16 dsk_typedesc_enum(int index, char **drvdesc)
{
	int ndrv;

	if (!drvdesc) return DSK_ERR_BADPTR;

	for (ndrv = 0; classes[ndrv]; ndrv++)
	{
		if (index == ndrv)
		{
			*drvdesc = classes[ndrv]->dc_description;
			return DSK_ERR_OK;
		}
	}
	*drvdesc = NULL;
	return DSK_ERR_NODRVR;
}


LDPUBLIC32 dsk_err_t LDPUBLIC16 dsk_comp_enum(int index, char **compname)
{
	return comp_type_enum(index, compname);
}



LDPUBLIC32 unsigned char LDPUBLIC16 dsk_get_psh(size_t secsize)
{
	unsigned char psh;

	for (psh = 0; secsize > 128; secsize /= 2) psh++;
	return psh;
}


/* Treat secsize values of 9 and higher as though they were 8. This stops
 * libdsk crashing on disk images with funny PSH values, and matches how 
 * the real uPD765A handles sizes >= 8. */
LDPUBLIC32 size_t LDPUBLIC16 dsk_expand_psh(unsigned char psh)
{
	if (psh <= 8)
	{
		return 128 << psh;
	}
	return 32768;
}



LDPUBLIC32 const char * LDPUBLIC16 dsk_drvname(DSK_DRIVER *self)
{
	if (!self || !self->dr_class || !self->dr_class->dc_drvname)
		return "(null)";
	return self->dr_class->dc_drvname;
}


LDPUBLIC32 const char * LDPUBLIC16 dsk_drvdesc(DSK_DRIVER *self)
{
	if (!self || !self->dr_class || !self->dr_class->dc_description)
		return "(null)";
	return self->dr_class->dc_description;
}



LDPUBLIC32 const char * LDPUBLIC16 dsk_compname(DSK_DRIVER *self)
{
        if (!self) return "(null)";
	if (!self->dr_compress) return NULL;
	
	return comp_name(self->dr_compress);	
}


LDPUBLIC32 const char * LDPUBLIC16 dsk_compdesc(DSK_DRIVER *self)
{
        if (!self) return "(null)";
	if (!self->dr_compress) return "Not compressed";
	
	return comp_desc(self->dr_compress);	
}


int drv_instanceof(DSK_DRIVER *drv, DRV_CLASS *dc)
{
	DRV_CLASS *clazz = drv->dr_class;

	while (clazz)
	{
		if (clazz == dc) return 1;
		clazz = clazz->dc_super;
	}
	return 0;
}

