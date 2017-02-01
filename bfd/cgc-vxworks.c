/* VxWorks support for CGC
   Copyright 2005, 2006, 2007, 2009, 2012 Free Software Foundation, Inc.

   This file is part of BFD, the Binary File Descriptor library.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston,
   MA 02111-1307, USA.  */

/* This file provides routines used by all VxWorks targets.  */

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "cgc-bfd.h"
#include "cgc-vxworks.h"
#include "cgc/vxworks.h"

/* Return true if symbol NAME, as defined by ABFD, is one of the special
   __GOTT_BASE__ or __GOTT_INDEX__ symbols.  */

static bfd_boolean
cgc_vxworks_gott_symbol_p (bfd *abfd, const char *name)
{
  char leading;

  leading = bfd_get_symbol_leading_char (abfd);
  if (leading)
    {
      if (*name != leading)
	return FALSE;
      name++;
    }
  return (strcmp (name, "__GOTT_BASE__") == 0
	  || strcmp (name, "__GOTT_INDEX__") == 0);
}

/* Tweak magic VxWorks symbols as they are loaded.  */
bfd_boolean
cgc_vxworks_add_symbol_hook (bfd *abfd,
			     struct bfd_link_info *info,
			     Cgc_Internal_Sym *sym,
			     const char **namep,
			     flagword *flagsp,
			     asection **secp ATTRIBUTE_UNUSED,
			     bfd_vma *valp ATTRIBUTE_UNUSED)
{
  /* Ideally these "magic" symbols would be exported by libc.so.1
     which would be found via a DT_NEEDED tag, and then handled
     specially by the linker at runtime.  Except shared libraries
     don't even link to libc.so.1 by default...
     If the symbol is imported from, or will be put in a shared library,
     give the symbol weak binding to get the desired samantics.
     This transformation will be undone in
     cgc_i386_vxworks_link_output_symbol_hook. */
  if ((info->shared || abfd->flags & DYNAMIC)
      && cgc_vxworks_gott_symbol_p (abfd, *namep))
    {
      sym->st_info = CGC_ST_INFO (STB_WEAK, CGC_ST_TYPE (sym->st_info));
      *flagsp |= BSF_WEAK;
    }

  return TRUE;
}

/* Perform VxWorks-specific handling of the create_dynamic_sections hook.
   When creating an executable, set *SRELPLT2_OUT to the .rel(a).plt.unloaded
   section.  */

bfd_boolean
cgc_vxworks_create_dynamic_sections (bfd *dynobj, struct bfd_link_info *info,
				     asection **srelplt2_out)
{
  struct cgc_link_hash_table *htab;
  const struct cgc_backend_data *bed;
  asection *s;

  htab = cgc_hash_table (info);
  bed = get_cgc_backend_data (dynobj);

  if (!info->shared)
    {
      s = bfd_make_section_anyway_with_flags (dynobj,
					      bed->default_use_rela_p
					      ? ".rela.plt.unloaded"
					      : ".rel.plt.unloaded",
					      SEC_HAS_CONTENTS | SEC_IN_MEMORY
					      | SEC_READONLY
					      | SEC_LINKER_CREATED);
      if (s == NULL
	  || !bfd_set_section_alignment (dynobj, s, bed->s->log_file_align))
	return FALSE;

      *srelplt2_out = s;
    }

  /* Mark the GOT and PLT symbols as having relocations; they might
     not, but we won't know for sure until we build the GOT in
     finish_dynamic_symbol.  Also make sure that the GOT symbol
     is entered into the dynamic symbol table; the loader uses it
     to initialize __GOTT_BASE__[__GOTT_INDEX__].  */
  if (htab->hgot)
    {
      htab->hgot->indx = -2;
      htab->hgot->other &= ~CGC_ST_VISIBILITY (-1);
      htab->hgot->forced_local = 0;
      if (!bfd_cgc_link_record_dynamic_symbol (info, htab->hgot))
	return FALSE;
    }
  if (htab->hplt)
    {
      htab->hplt->indx = -2;
      htab->hplt->type = STT_FUNC;
    }

  return TRUE;
}

/* Tweak magic VxWorks symbols as they are written to the output file.  */
int
cgc_vxworks_link_output_symbol_hook (struct bfd_link_info *info
				       ATTRIBUTE_UNUSED,
				     const char *name,
				     Cgc_Internal_Sym *sym,
				     asection *input_sec ATTRIBUTE_UNUSED,
				     struct cgc_link_hash_entry *h)
{
  /* Reverse the effects of the hack in cgc_vxworks_add_symbol_hook.  */
  if (h
      && h->root.type == bfd_link_hash_undefweak
      && cgc_vxworks_gott_symbol_p (h->root.u.undef.abfd, name))
    sym->st_info = CGC_ST_INFO (STB_GLOBAL, CGC_ST_TYPE (sym->st_info));

  return 1;
}

/* Copy relocations into the output file.  Fixes up relocations against PLT
   entries, then calls the generic routine.  */

bfd_boolean
cgc_vxworks_emit_relocs (bfd *output_bfd,
			 asection *input_section,
			 Cgc_Internal_Shdr *input_rel_hdr,
			 Cgc_Internal_Rela *internal_relocs,
			 struct cgc_link_hash_entry **rel_hash)
{
  const struct cgc_backend_data *bed;
  int j;

  bed = get_cgc_backend_data (output_bfd);

  if (output_bfd->flags & (DYNAMIC|EXEC_P))
    {
      Cgc_Internal_Rela *irela;
      Cgc_Internal_Rela *irelaend;
      struct cgc_link_hash_entry **hash_ptr;

      for (irela = internal_relocs,
	     irelaend = irela + (NUM_SHDR_ENTRIES (input_rel_hdr)
				 * bed->s->int_rels_per_ext_rel),
	     hash_ptr = rel_hash;
	   irela < irelaend;
	   irela += bed->s->int_rels_per_ext_rel,
	     hash_ptr++)
	{
	  if (*hash_ptr
	      && (*hash_ptr)->def_dynamic
	      && !(*hash_ptr)->def_regular
	      && ((*hash_ptr)->root.type == bfd_link_hash_defined
		  || (*hash_ptr)->root.type == bfd_link_hash_defweak)
	      && (*hash_ptr)->root.u.def.section->output_section != NULL)
	    {
	      /* This is a relocation from an executable or shared
	         library against a symbol in a different shared
	         library.  We are creating a definition in the output
	         file but it does not come from any of our normal (.o)
	         files. ie. a PLT stub.  Normally this would be a
	         relocation against against SHN_UNDEF with the VMA of
	         the PLT stub.  This upsets the VxWorks loader.
	         Convert it to a section-relative relocation.  This
	         gets some other symbols (for instance .dynbss), but
	         is conservatively correct.  */
	      for (j = 0; j < bed->s->int_rels_per_ext_rel; j++)
		{
		  asection *sec = (*hash_ptr)->root.u.def.section;
		  int this_idx = sec->output_section->target_index;

		  irela[j].r_info
		    = CGC32_R_INFO (this_idx, CGC32_R_TYPE (irela[j].r_info));
		  irela[j].r_addend += (*hash_ptr)->root.u.def.value;
		  irela[j].r_addend += sec->output_offset;
		}
	      /* Stop the generic routine adjusting this entry.  */
	      *hash_ptr = NULL;
	    }
	}
    }
  return _bfd_cgc_link_output_relocs (output_bfd, input_section,
				      input_rel_hdr, internal_relocs,
				      rel_hash);
}


/* Set the sh_link and sh_info fields on the static plt relocation secton.  */

void
cgc_vxworks_final_write_processing (bfd *abfd,
				    bfd_boolean linker ATTRIBUTE_UNUSED)
{
  asection * sec;
  struct bfd_cgc_section_data *d;

  sec = bfd_get_section_by_name (abfd, ".rel.plt.unloaded");
  if (!sec)
    sec = bfd_get_section_by_name (abfd, ".rela.plt.unloaded");
  if (!sec)
    return;
  d = cgc_section_data (sec);
  d->this_hdr.sh_link = cgc_onesymtab (abfd);
  sec = bfd_get_section_by_name (abfd, ".plt");
  if (sec)
    d->this_hdr.sh_info = cgc_section_data (sec)->this_idx;
}

/* Add the dynamic entries required by VxWorks.  These point to the
   tls sections.  */

bfd_boolean
cgc_vxworks_add_dynamic_entries (bfd *output_bfd, struct bfd_link_info *info)
{
  if (bfd_get_section_by_name (output_bfd, ".tls_data"))
    {
      if (!_bfd_cgc_add_dynamic_entry (info, DT_VX_WRS_TLS_DATA_START, 0)
	  || !_bfd_cgc_add_dynamic_entry (info, DT_VX_WRS_TLS_DATA_SIZE, 0)
	  || !_bfd_cgc_add_dynamic_entry (info, DT_VX_WRS_TLS_DATA_ALIGN, 0))
	return FALSE;
    }
  if (bfd_get_section_by_name (output_bfd, ".tls_vars"))
    {
      if (!_bfd_cgc_add_dynamic_entry (info, DT_VX_WRS_TLS_VARS_START, 0)
	  || !_bfd_cgc_add_dynamic_entry (info, DT_VX_WRS_TLS_VARS_SIZE, 0))
	return FALSE;
    }
  return TRUE;
}

/* If *DYN is one of the VxWorks-specific dynamic entries, then fill
   in the value now  and return TRUE.  Otherwise return FALSE.  */

bfd_boolean
cgc_vxworks_finish_dynamic_entry (bfd *output_bfd, Cgc_Internal_Dyn *dyn)
{
  asection *sec;

  switch (dyn->d_tag)
    {
    default:
      return FALSE;

    case DT_VX_WRS_TLS_DATA_START:
      sec = bfd_get_section_by_name (output_bfd, ".tls_data");
      dyn->d_un.d_ptr = sec->vma;
      break;

    case DT_VX_WRS_TLS_DATA_SIZE:
      sec = bfd_get_section_by_name (output_bfd, ".tls_data");
      dyn->d_un.d_val = sec->size;
      break;

    case DT_VX_WRS_TLS_DATA_ALIGN:
      sec = bfd_get_section_by_name (output_bfd, ".tls_data");
      dyn->d_un.d_val
	= (bfd_size_type)1 << bfd_get_section_alignment (output_bfd,
							 sec);
      break;

    case DT_VX_WRS_TLS_VARS_START:
      sec = bfd_get_section_by_name (output_bfd, ".tls_vars");
      dyn->d_un.d_ptr = sec->vma;
      break;

    case DT_VX_WRS_TLS_VARS_SIZE:
      sec = bfd_get_section_by_name (output_bfd, ".tls_vars");
      dyn->d_un.d_val = sec->size;
      break;
    }
  return TRUE;
}

