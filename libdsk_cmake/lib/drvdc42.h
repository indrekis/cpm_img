/***************************************************************************
 *                                                                         *
 *    LIBDSK: General floppy and diskimage access library                  *
 *    Copyright (C) 2001-2019  John Elliott <seasip.webmaster@gmail.com>   *
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

/* Declarations for the DiskCopy 4.2 driver. */

typedef struct
{
        LDBSDISK_DSK_DRIVER dc42_super;
	char *dc42_filename;

	FILE *dc42_fp;
	unsigned long data_cksum;
	unsigned long tag_cksum;
	int tag_skip;

} DC42_DSK_DRIVER;

dsk_err_t dc42_open(DSK_DRIVER *self, const char *filename, DSK_REPORTFUNC diagfunc);
dsk_err_t dc42_creat(DSK_DRIVER *self, const char *filename);
dsk_err_t dc42_close(DSK_DRIVER *self);

