/* ffi.c - generate .ffi file from .h file
   Copyright (C) 2001, 2005 Clozure Associates.
   Copyright (C) 2004, Helmut Eller

This file is part of GNU CC (or, more accurately, part of a work
derived from GNU CC.)

GNU CC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU CC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "c-tree.h"
#include "c-common.h"
#include "ggc.h"
#include "flags.h"
#include "debug.h"
#include "diagnostic.h"         /* for errorcount */
#include "objc/objc-act.h"
#include "ffi-version.h"

/* Prototypes */

void ffi_rest_of_decl_compilation (tree, int, int);
void ffi_rest_of_type_compilation (tree, int);
void ffi_early_init (void);
void ffi_init (FILE *_file, char *);
void ffi_rest_of_objc_class_compilation (tree);
void ffi_rest_of_objc_category_compilation (tree);
void print_ffi_version(FILE *, char *);



void
print_ffi_version(FILE *out, char *indent)
{
  fprintf (out, "%s%sFFIGEN version %s\n", indent, *indent != 0 ? " " : "", ffi_version);
}

FILE *ffifile;

int ffi_initialized = 0;

struct ffi_input_file {
  char *pathname;
  struct ffi_input_file *prev;
};

struct ffi_input_file *current_ffi_input_file = NULL;
char *ffi_current_source_file;
char *ffi_primary_source_file;

static int ffi_indent_count = 1;

static void
ffi_debug_start_source_file (unsigned int line ATTRIBUTE_UNUSED, 
			     const char *file)
{
  struct ffi_input_file *savef = xmalloc (sizeof *savef);
  ffi_indent_count = 1;
  savef->pathname = ffi_current_source_file;
  savef->prev = current_ffi_input_file;
  current_ffi_input_file = savef;
  ffi_current_source_file = (char *) xstrdup (file);
}

static void 
ffi_debug_end_source_file (unsigned lineno ATTRIBUTE_UNUSED)
{
  struct ffi_input_file *currf = current_ffi_input_file;
  current_ffi_input_file = currf->prev;
  free (ffi_current_source_file);
  ffi_current_source_file = currf->pathname;
  free (currf);
}

static void 
ffi_indent (void)
{
  int i;
  
  fprintf (ffifile, "\n");

  for (i = 0; i < ffi_indent_count; i++) {
    fprintf (ffifile, " ");
  }
  ffi_indent_count++;
}

static void
ffi_unindent (int n)
{
  ffi_indent_count -= n;
}

enum ffi_typestatus {FFI_TYPE_UNDEFINED = 0,
		     FFI_TYPE_PRIMITIVE,
		     FFI_TYPE_TYPEDEF,
		     FFI_TYPE_STRUCT,
		     FFI_TYPE_UNION,
                     FFI_TYPE_TRANSPARENT_UNION,
		     FFI_TYPE_ENUM,
		     FFI_TYPE_POINTER,
		     FFI_TYPE_FUNCTION,
		     FFI_TYPE_ARRAY,
		     FFI_TYPE_FIELD};

static void ffi_define_type (tree decl, enum ffi_typestatus status);


struct ffi_typeinfo {
  enum ffi_typestatus status;
  int type_number;
  tree type;
  const char *name;
  char *source_file;
  unsigned source_line;
  int ref_type_number;		/* For pointer & array types, at least */
  int array_size;
  int first_field;
};

struct ffi_typeinfo *ffi_typevec;

static void ffi_emit_type (struct ffi_typeinfo *, tree);

int ffi_typevec_len = 0;
int next_ffi_type_number = 1;

typedef struct {
  const char *gcc_name;
  const char *ffi_name;
} ffi_primitive_name_map;

static ffi_primitive_name_map ffi_primitive_names[] =
{
  {"int", "int"}, 
  {"char", "char"},
  {"float", "float"},
  {"double", "double"},
  {"long double", "long-double"}, 
  {"void", "void"},
  {"long int", "long"}, 
  {"unsigned int", "unsigned"}, 
  {"long unsigned int", "unsigned-long"}, 
  {"long long int", "long-long"}, 
  {"long long unsigned int", "unsigned-long-long"}, 
  {"short int", "short"}, 
  {"short unsigned int", "unsigned-short"}, 
  {"signed char", "signed-char"}, 
  {"unsigned char", "unsigned-char"}, 
  {"complex int", "complex-int"},
  {"complex float", "complex-float"},
  {"complex double", "complex-double"},
  {"complex long double", "complex-long-double"},
  {"__vector unsigned char", "__vector-unsigned-char"},
  {"__vector signed char", "__vector-signed-char"},
  {"__vector bool char", "__vector-bool-char"},
  {"__vector unsigned short", "__vector-unsigned-short"},
  {"__vector signed short", "__vector-signed-short"},
  {"__vector bool short", "__vector-bool-short"},
  {"__vector unsigned long", "__vector-unsigned-long"},
  {"__vector signed long", "__vector-signed-long"},
  {"__vector bool long", "__vector-bool-long"},
  {"__vector float", "__vector-short-float"},
  {"__vector pixel", "__vector-pixel"},
  {"__vector", "__vector"},  
  {"_Bool", "unsigned"},
  {"__int128_t", "long-long-long"}, 
  {"__uint128_t", "unsigned-long-long-long"}, 
  {NULL, NULL}
};

/* Return the Lisp'y name for the type GCC_NAME */
static char *
to_primitive_type_name (char* gcc_name)
{
  ffi_primitive_name_map *map = ffi_primitive_names;
  for (; map->gcc_name; ++map)
    if (!strcmp (map->gcc_name, gcc_name))
      return (char*)map->ffi_name;
  fprintf (stderr, "Bug: couldn't find primitive name for %s\n", gcc_name);
  abort ();
}

static char *
ffi_primitive_type_name (tree decl)
{
  char *gcc_name = (char*)IDENTIFIER_POINTER (DECL_NAME (decl));
  return to_primitive_type_name (gcc_name);
}

static struct ffi_typeinfo *
find_ffi_type_info (tree type)
{
  int index = TYPE_SYMTAB_ADDRESS (type);
  struct ffi_typeinfo * info;

  if (index == 0)
    {
      index = next_ffi_type_number++;
      TYPE_SYMTAB_ADDRESS (type) = index;
      
      if (next_ffi_type_number == ffi_typevec_len) 
	{
	  ffi_typevec 
	    = (struct ffi_typeinfo *) xrealloc (ffi_typevec,
						ffi_typevec_len * 2 *
						sizeof ffi_typevec[0]);
	  memset ((char *) (ffi_typevec + ffi_typevec_len), 0,
		  ffi_typevec_len * sizeof ffi_typevec[0]);
	  ffi_typevec_len *= 2;
	}
    }
  
  info = ffi_typevec + index;
  info->type_number = index;
  info->type = type;
  return info;
}


/*
  'type' may be an unnamed scalar type.  Why does it even exist ?
*/
static struct ffi_typeinfo *
ffi_maybe_synthesize_integer_type (tree type, struct ffi_typeinfo *info)
{
  if (TREE_CODE (type) == INTEGER_TYPE)
    {
      
      int is_unsigned = TYPE_UNSIGNED (type);
      HOST_WIDE_INT nbytes =  int_size_in_bytes (type);

      switch (nbytes) 
        {
        case 1:
          info->name = is_unsigned ? "unsigned-char" : "signed-char";
          info->status = FFI_TYPE_PRIMITIVE;
          break;
        case 2:
          info->name = is_unsigned ? "unsigned-short" : "short";
          info->status = FFI_TYPE_PRIMITIVE;
          break;
        case 4:
          info->name = is_unsigned ? "unsigned" : "int";
          info->status = FFI_TYPE_PRIMITIVE;
          break;
        case 8:
          info->name = is_unsigned ? "unsigned-long-long" : "long-long";
          info->status = FFI_TYPE_PRIMITIVE;
          break;
        case 16:
          info->name = is_unsigned ? "unsigned-long-long-long" : "long-long-long";
          info->status = FFI_TYPE_PRIMITIVE;
          break;
        }
    }
  return info;
}

static struct ffi_typeinfo *
ffi_maybe_synthesize_vector_type (tree type, struct ffi_typeinfo *info)
{
  if (TREE_CODE (type) == VECTOR_TYPE)
    {
      
      HOST_WIDE_INT nbytes =  int_size_in_bytes (type);

      switch (nbytes) 
        {
        case 32:
          info->name = "vec256";
          info->status = FFI_TYPE_PRIMITIVE;
        case 16:
          info->name = "vec128";
          info->status = FFI_TYPE_PRIMITIVE;
          break;
        case 8:
          info->name = "vec64";
          info->status = FFI_TYPE_PRIMITIVE;
          break;
	default:
	  break;
        }
    }
  return info;
}
        
        

static struct ffi_typeinfo *
ffi_create_type_info (tree type)
{
  struct ffi_typeinfo *info;
  tree lookup_type = type;

  if (TYPE_READONLY (type) || TYPE_VOLATILE (type))
    lookup_type = TYPE_MAIN_VARIANT (type);
  
  info = find_ffi_type_info (lookup_type);

  if (info->status != FFI_TYPE_UNDEFINED)
    return info;

 
  switch (TREE_CODE (type))
    {

    case POINTER_TYPE:
    case REFERENCE_TYPE:
      {
	tree pointer_target_type = TREE_TYPE (type);
	struct ffi_typeinfo *pointer_type_info;

	info->status = FFI_TYPE_POINTER;
	pointer_type_info = ffi_create_type_info (pointer_target_type);

	/* 'info' might have moved */
	info = find_ffi_type_info (type);
	info->ref_type_number = pointer_type_info->type_number;
      }
      break;

    case FUNCTION_TYPE:
      {
	tree arg_type;
	info->status = FFI_TYPE_FUNCTION;
	ffi_create_type_info (TREE_TYPE (type));
	for (arg_type = TYPE_ARG_TYPES (type); arg_type; arg_type = TREE_CHAIN (arg_type))
	  {
	    ffi_create_type_info (TREE_VALUE (arg_type));
	  }
	info = find_ffi_type_info (type);
      }
      break;

    case ARRAY_TYPE:
      {
	tree array_element_type = TREE_TYPE (type);
	struct ffi_typeinfo *element_type_info;
	int array_size_in_bits = 0, element_size_in_bits, array_size_in_elements;
	
	info->status = FFI_TYPE_ARRAY;
	element_type_info = ffi_create_type_info (array_element_type);
	info = find_ffi_type_info (type);
	info->ref_type_number = element_type_info->type_number;
	element_size_in_bits = TREE_INT_CST_LOW ( TYPE_SIZE (array_element_type));
	if (TYPE_SIZE (type))
	  array_size_in_bits = TREE_INT_CST_LOW ( TYPE_SIZE (type));
	array_size_in_elements = array_size_in_bits / element_size_in_bits;
	if (array_size_in_bits % element_size_in_bits)
	  {
	    fprintf (stderr, "screw : array size not integral\n");
	    abort ();
	  }
	info->array_size = array_size_in_elements;
      }
      break;
      
    case RECORD_TYPE:
    case UNION_TYPE:
    case ENUMERAL_TYPE:
      {
	tree field;
	tree name = TYPE_NAME (type);
	if (name)
	  {
	    if (TREE_CODE (name) == IDENTIFIER_NODE)
	      info->name = (char *) xstrdup (IDENTIFIER_POINTER (name));
	    else if (TREE_CODE (name) == TYPE_DECL
		     && DECL_NAME (name))
	      info->name = (char *) xstrdup (IDENTIFIER_POINTER (DECL_NAME (name)));
	  }
	if (! info->name)
	  {
	    char buf[2048];
	    sprintf(buf, "%d_%s", info->type_number, ffi_primary_source_file);
	    info->name = (char *) xstrdup (buf);
	  }
	if (TREE_CODE (type) == ENUMERAL_TYPE)
	  {
	    info->status = FFI_TYPE_ENUM;
	    break;
	  }

        if (TREE_CODE (type) == RECORD_TYPE) 
          info->status = FFI_TYPE_STRUCT;
        else if (TYPE_TRANSPARENT_UNION (type))
          info->status = FFI_TYPE_TRANSPARENT_UNION;
        else info->status = FFI_TYPE_UNION;

	for (field = TYPE_FIELDS (type); field; field = TREE_CHAIN (field))
	  {
	    ffi_create_type_info (TREE_TYPE (field));
	  }
	info = find_ffi_type_info (type);
      }
      break;
    case VOID_TYPE:
      ffi_define_type (TYPE_NAME (type), FFI_TYPE_PRIMITIVE);
      info = ffi_create_type_info (type);
      break;
    case INTEGER_TYPE:
    case VECTOR_TYPE:
      if (TREE_CODE (type) == INTEGER_TYPE) 
	info = ffi_maybe_synthesize_integer_type (type, info);
      else
	info = ffi_maybe_synthesize_vector_type (type, info);
      if (info->status != FFI_TYPE_UNDEFINED)
        return info;
      /* else fall through */
    default:
      fprintf (stderr, "Need to create info for type:\n");
      debug_tree (type);
      abort ();
    }
  return info;
}

static void
emit_ffi_type_reference (tree target_type)
{
  struct ffi_typeinfo *info;
  tree lookup_type = target_type;

  if (TYPE_READONLY (target_type) || TYPE_VOLATILE (target_type))
    lookup_type = TYPE_MAIN_VARIANT (target_type);

  info = ffi_create_type_info (lookup_type);

  switch (info->status) 
    {
    case FFI_TYPE_PRIMITIVE:
      fprintf (ffifile, "(%s ())", info->name);	/* No quotes around primitive name. */
      return;

    case FFI_TYPE_TYPEDEF:
      fprintf (ffifile, "(typedef \"%s\")", info->name);
      return;

    case FFI_TYPE_FUNCTION:
      {
	tree arg, arg_type;
	/* struct ffi_typeinfo *arg_type_info; */
	/*
	  It seems like functions that take a fixed number of arguments
	  have a "void" argument after the fixed arguments, while those
	  that take an indefinite number don't.
	  That's perfectly sane, but the opposite of what LCC does.
	  So, if there's a "void" argument at the end of the arglist,
	  don't emit it; if there wasn't, emit one.
	  Sheesh.
	*/
	int had_void_arg = 0, args_emitted = 0;
	fprintf (ffifile, "(function");
	ffi_indent ();
	fprintf (ffifile, "(");
	for (arg = TYPE_ARG_TYPES (target_type); arg ; arg = TREE_CHAIN (arg))
	  {
	    if (args_emitted)
	      fprintf (ffifile, " ");
	    arg_type = TREE_VALUE (arg);
	    if (TREE_CODE (arg_type) != VOID_TYPE) {
	      emit_ffi_type_reference (arg_type);
	      args_emitted = 1;
	    }
	    else 
	      {
		had_void_arg = 1;
		break;
	      }
	  }
	if (! had_void_arg) {
	  if (args_emitted)
	    fprintf (ffifile, " ");
	  fprintf (ffifile, "(void ())");
	}
	fprintf (ffifile, ")");
	ffi_unindent (1);
	ffi_indent ();
	arg_type = TREE_TYPE (target_type);
	emit_ffi_type_reference (arg_type);
	ffi_unindent (1);
	fprintf (ffifile, ")");
      }
      break;
	
    case FFI_TYPE_POINTER:
      {
	tree pointer_target = TREE_TYPE (target_type);

	fprintf (ffifile, "(pointer ");
	emit_ffi_type_reference (pointer_target);
	fprintf (ffifile, ")");
	return;
      }
      
    case FFI_TYPE_STRUCT:
      fprintf (ffifile, "(struct-ref \"%s\")", info->name);
      break;

    case FFI_TYPE_UNION:
      fprintf (ffifile, "(union-ref \"%s\")", info->name);
      break;

    case FFI_TYPE_TRANSPARENT_UNION:
      fprintf (ffifile, "(transparent-union-ref \"%s\")", info->name);
      break;

    case FFI_TYPE_ENUM:
      fprintf (ffifile, "(enum-ref \"%s\")", info->name);
      break;

    case FFI_TYPE_ARRAY:
      {
	tree element_type = TREE_TYPE (target_type);

	fprintf (ffifile, "(array %d ", info->array_size);
	emit_ffi_type_reference (element_type);
	fprintf (ffifile, ")");
      }
      break;

    default:
      fprintf (stderr, "need to emit reference to this type\n");
      debug_tree (target_type);
      abort ();
    }
}

static void
ffi_emit_type_decl (struct ffi_typeinfo *info, tree decl)
{
  /* struct ffi_typeinfo *target_info; */
  tree target;
  struct ffi_typeinfo *target_info;

  if (TREE_ASM_WRITTEN (decl)) 
    return;


  target = DECL_ORIGINAL_TYPE (decl);
  if (target == NULL)
    target = TREE_TYPE (decl);

  TREE_ASM_WRITTEN (decl) = 1;
  
  if (! TYPE_SYMTAB_ADDRESS (target))
    switch (TREE_CODE (target)) 
      {
      case RECORD_TYPE:
      case UNION_TYPE:
        target_info = ffi_create_type_info (target);
        ffi_emit_type (target_info, target);
        break;
      default:
        break;
      }

  fprintf (ffifile, "(type (\"%s\" %d)", info->source_file, info->source_line);
  ffi_indent ();
  fprintf (ffifile, "\"%s\"", info->name);
  ffi_unindent (1);
  ffi_indent ();
  emit_ffi_type_reference (target);
  ffi_unindent (1);
  fprintf (ffifile, ")\n");
}

static char *
ffi_decl_name (tree decl)
{
  return (char*) (DECL_NAME (decl) 
		  ?  IDENTIFIER_POINTER (DECL_NAME (decl))
		  : "");
}

static void
ffi_emit_integer_constant (tree val)
{
  if (TREE_CODE (val) != INTEGER_CST) 
    {
      fprintf (stderr, "Expected integer_cst, got:\n");
      debug_tree (val);
      abort ();
    }
  if (TREE_INT_CST_HIGH (val) == 0)
    fprintf (ffifile, HOST_WIDE_INT_PRINT_UNSIGNED, TREE_INT_CST_LOW (val));
  else if (TREE_INT_CST_HIGH (val) == -1
	   && TREE_INT_CST_LOW (val) != 0)
    {
      fprintf (ffifile, "-");
      fprintf (ffifile, HOST_WIDE_INT_PRINT_UNSIGNED,
	       -TREE_INT_CST_LOW (val));
    }
  else
    fprintf (ffifile, HOST_WIDE_INT_PRINT_DOUBLE_HEX,
	     TREE_INT_CST_HIGH (val), TREE_INT_CST_LOW (val));
}

static void
ffi_emit_enum_values (tree val)
{
  fprintf (ffifile, "(");
  while (val)
    {
      fprintf (ffifile, "(\"%s\" ", IDENTIFIER_POINTER (TREE_PURPOSE (val)));
      ffi_emit_integer_constant (TREE_VALUE (val));
      fprintf (ffifile, ")");
      val = TREE_CHAIN (val);
    }
  fprintf (ffifile, ")");
}

static void
ffi_herald (tree decl, const char *prefix)
{
  const char *source_file = "";
  int line = 0;
  
  if (DECL_P (decl))
    {
      source_file = DECL_SOURCE_FILE (decl);
      line = DECL_SOURCE_LINE (decl);
    }

  if (source_file == NULL) 
    source_file = "";
  
  fprintf (ffifile, "(%s (\"%s\" %d)", prefix, source_file, line);
}

static void
ffi_emit_enum_idents (tree val)
{
  while (val)
    {
      fprintf (ffifile, "(enum-ident (\"\" 0)");
      ffi_indent ();
      fprintf (ffifile, "\"%s\" ", IDENTIFIER_POINTER (TREE_PURPOSE (val)));
      ffi_emit_integer_constant (TREE_VALUE (val));
      ffi_unindent (1);
      fprintf(ffifile, ")\n");
      val = TREE_CHAIN (val);
    }
}

/*
  It's hard to know exactly how to translate bit-fields.
  LCC only allows bit-fields to be of type "int" or "unsigned" (and
  doesn't even allow aliases to these types to be used.)
  LCC-FFIGEN therefore generates something like:

  ("x" (bit-field ("<type>" ()) <start-byte> <size-in-bytes> <a> <b> <c> <d>))

  where :
  <type> is one of int, unsigned
  <start-byte, size-in-bytes> refer to a properly-aligned fullword that
    "contains" the bit-field
  <a> is what lcc calls the "fieldleft" of the bit-field
  <b> is what lcc calls the "fieldright" of the bit-field
  <c> is the width of the bit-field
  <d> is a mask: (1- (ash 1 <c>))

  Hmm.  That doesn't have too much to do with the information that GCC
  provides, but we could probably fake it.  It seems silly to discard
  the information that GCC -does- provide, so let's instead generate:

  ("x" (bitfield ("<type>" ()) <start-bit> <size-in-bits>))

  where <start-bit> is the field position starting in bits, of 
  the bit closest to the beginning of the structure.

  Note that a GCC FIELD_DECL node X denotes a bit-field iff
   DECL_BIT_FIELD (X) is true and DECL_SIZE (X) is non-null.
*/

static void
ffi_emit_field_list (tree field, int public_only)
{
  int indentp = 0;
  ffi_indent ();
  fprintf (ffifile, "(");
  for (; field; field = TREE_CHAIN (field), indentp = 1)
    {
      if (!public_only || (!TREE_PRIVATE (field)))
        {
          /*
            fprintf (stderr, "\nEmit field:\n");
            debug_tree (field);
          */
          if (indentp)
            ffi_indent ();
          if (DECL_BIT_FIELD (field))
            {
              tree type = DECL_BIT_FIELD_TYPE (field);
              int bit_position = int_bit_position (field); 
              int size_in_bits = tree_low_cst (DECL_SIZE (field), 1);
              fprintf (ffifile, "(\"%s\" (bitfield ", ffi_decl_name (field));
              emit_ffi_type_reference (type);
              fprintf (ffifile, " %d %d))", bit_position, size_in_bits);
            }
          else
            {
              tree type = TREE_TYPE (field);
              int bit_position = int_bit_position (field); 
              tree size = TYPE_SIZE (type);
              int type_size = (size 
                               ? tree_low_cst (size, 1) 
                               : 0); /* flex array */
              fprintf (ffifile, "(\"%s\" (field ", ffi_decl_name (field));
              emit_ffi_type_reference (type);
              fprintf (ffifile, " %d %d))", bit_position >> 3, type_size >> 3);
            }
          if (indentp)
            ffi_unindent (1);
        }
    }
  fprintf (ffifile, ")");
  ffi_unindent (1);
}
  
static void
ffi_emit_type (struct ffi_typeinfo *info, tree type)
{
  char *source_file = (char*) (info->source_file ? info->source_file : "");
  int line_number = info->source_line;
  /* tree field; */


  switch (info->status)
    {
    case FFI_TYPE_STRUCT:
      fprintf (ffifile, "(struct (\"%s\" %d)", source_file, line_number);
      ffi_indent ();
      fprintf (ffifile, "\"%s\"", info->name);
      ffi_unindent (1);
      ffi_emit_field_list (TYPE_FIELDS (type), 0);
      fprintf (ffifile, ")\n");
      break;

    case FFI_TYPE_UNION:
    case FFI_TYPE_TRANSPARENT_UNION:
      fprintf (ffifile, "(%s (\"%s\" %d)", 
               info->status == FFI_TYPE_UNION ? "union" : "transparent-union",
               source_file,
               line_number);
      ffi_indent ();
      fprintf (ffifile, "\"%s\"", info->name);
      ffi_unindent (1);
      ffi_emit_field_list (TYPE_FIELDS (type), 0);
      fprintf (ffifile, ")\n");
      break;
     
    case FFI_TYPE_ENUM:
      fprintf (ffifile, "(enum (\"%s\" %d)", source_file, line_number);
      ffi_indent ();
      fprintf (ffifile, "\"%s\"", info->name);
      ffi_unindent (1);
      ffi_emit_enum_values (TYPE_VALUES (type));
      fprintf (ffifile, ")\n");
      ffi_emit_enum_idents (TYPE_VALUES (type));
      break;

    default:
      fprintf (stderr, "Unknown type!\n");
      debug_tree (type);
      abort ();
    }
}

static void
ffi_define_type (tree decl, enum ffi_typestatus status)
{
  tree type = TREE_TYPE(decl);
  struct ffi_typeinfo *info = find_ffi_type_info (type);

  /* fprintf (stderr, "Defining this type\n");
     debug_tree (type);
  */
  switch (status)
    {
    case FFI_TYPE_PRIMITIVE:
      info->name = ffi_primitive_type_name (decl);
      info->source_file = NULL;
      info->source_line = 0;
      break;
    case FFI_TYPE_TYPEDEF:
      info->name = (char *) xstrdup(IDENTIFIER_POINTER (DECL_NAME (decl)));
      info->source_file = (char *) xstrdup (DECL_SOURCE_FILE (decl));
      info->source_line = DECL_SOURCE_LINE (decl);
      ffi_emit_type_decl (info, decl);
       break;
    default:
      fprintf (stderr, "Ignoring this type\n");
      debug_tree (type);
      abort ();
    }
  info = find_ffi_type_info (type); /* Might have moved. */
  info->status = status;
}

static void
ffi_define_builtin_type (tree type)
{
  tree name;

  if (type && TYPE_P (type))
    switch (TREE_CODE (type)) 
      {
      case INTEGER_TYPE:
      case REAL_TYPE:
      case COMPLEX_TYPE:
      case VOID_TYPE:
      case CHAR_TYPE:
      case BOOLEAN_TYPE:
      case VECTOR_TYPE:
        name = TYPE_NAME (type);
        if (name)
          ffi_define_type (name, FFI_TYPE_PRIMITIVE);
	else {
#if 0
	  fprintf(stderr, "Unnamed builtin vector type:\n");
	  debug_tree(type);
#endif
	}
        break;
      case POINTER_TYPE:
      case ARRAY_TYPE:
        ffi_create_type_info (type);
        break;
      default:
        fprintf (stderr, "\nSkippped this type.\n");
        debug_tree(type);
        break;
      }
}
        
        


static void
ffi_define_variable (tree decl)
{
  tree type = TREE_TYPE (decl);

  ffi_herald (decl, "var");
  ffi_indent ();
  fprintf (ffifile, "\"%s\"", IDENTIFIER_POINTER (DECL_NAME (decl)));
  ffi_unindent (1);
  ffi_indent ();
  emit_ffi_type_reference (type);
  if (DECL_EXTERNAL (decl))
    fprintf (ffifile, " (extern))\n");
  else
    fprintf (ffifile, " (static))\n");
  ffi_unindent (1);
}

static void
ffi_define_function (tree decl)
{
  tree type = TREE_TYPE (decl);

  ffi_herald (decl, "function");
  ffi_indent ();
  fprintf (ffifile, "\"%s\"", IDENTIFIER_POINTER (DECL_NAME (decl)));
  ffi_unindent (1);
  ffi_indent ();
  emit_ffi_type_reference (TYPE_MAIN_VARIANT(type));
  if (DECL_EXTERNAL (decl))
    fprintf (ffifile, " (extern))\n");
  else
    fprintf (ffifile, " (static))\n");
  ffi_unindent (1);
}



/*
  Stolen/copied from 'objc_method_parm_type' in objc-act.c 
*/
static tree
ffi_objc_method_parm_type (tree type)
{
  type = TREE_VALUE (TREE_TYPE (type));
  if (TREE_CODE (type) == TYPE_DECL)
    type = TREE_TYPE (type);
#if 0
  return TYPE_MAIN_VARIANT (type);
#else
  return type;
#endif
}

    
static void
ffi_define_objc_class (tree node)
{
  const char *super_name = NULL;
  tree 
    pl = CLASS_PROTOCOL_LIST (node),
    p;

  if (CLASS_SUPER_NAME (node))		
    super_name = IDENTIFIER_POINTER (CLASS_SUPER_NAME (node));
  
  
  ffi_herald (node, "objc-class");
  ffi_indent ();
  fprintf (ffifile, "\"%s\"", IDENTIFIER_POINTER (CLASS_NAME (node)));
  ffi_unindent (1); 
  ffi_indent ();
  fprintf (ffifile, "(");
  if (super_name)
    fprintf (ffifile, "\"%s\")", super_name);
  else
    fprintf (ffifile, "void)");
  ffi_unindent (1);
  ffi_indent ();
  fprintf (ffifile, "(");
  for (pl = CLASS_PROTOCOL_LIST (node); pl; pl = TREE_CHAIN (pl))
    {
      p = TREE_VALUE (pl);
      fprintf(ffifile, "\"%s\"", IDENTIFIER_POINTER (PROTOCOL_NAME (p)));
      if (TREE_CHAIN (pl))
        fprintf (ffifile, " ");
    }
  fprintf (ffifile, ")");
  ffi_unindent (1);
  ffi_emit_field_list (CLASS_IVARS (node), 1);
  fprintf (ffifile, ")\n");
}

static void
ffi_define_objc_method (const char *method_kind, tree m, tree c, const char *category_name)
{
  tree param;
  int i;

  ffi_herald(m, method_kind);
  ffi_indent ();
  fprintf (ffifile, "\"%s\"", IDENTIFIER_POINTER (CLASS_NAME (c)));
  ffi_unindent(1);
  ffi_indent ();
  if (category_name) 
    fprintf (ffifile, "(\"%s\")", category_name);
  else
    fprintf (ffifile, "()");  
  ffi_unindent (1);
  ffi_indent ();
  fprintf (ffifile, "\"%s\"", IDENTIFIER_POINTER (METHOD_SEL_NAME (m)));
  ffi_unindent (1);
  ffi_indent ();
  fprintf (ffifile, "(");
  for (i = 0, param = METHOD_SEL_ARGS (m); 
       param; 
       i++, param = TREE_CHAIN (param)) {
    if (i > 0) {
      fprintf (ffifile, " ");
    }
    emit_ffi_type_reference (ffi_objc_method_parm_type (param));
  }
  if (METHOD_ADD_ARGS (m))
    if (TREE_PUBLIC (METHOD_ADD_ARGS (m)))
      {
        if (i) 
          fprintf (ffifile, " ");
        fprintf (ffifile, "(void ())");
      }
  fprintf (ffifile, ")");
  ffi_unindent (1);
  ffi_indent ();
  emit_ffi_type_reference (ffi_objc_method_parm_type (m));
  ffi_unindent (1);
  fprintf (ffifile, ")\n");
}
  

static void
ffi_define_objc_class_method (tree m, tree c, const char *category_name)
{
  ffi_define_objc_method ("objc-class-method", m, c, category_name);
}

static void
ffi_define_objc_instance_method (tree m, tree c, const char *category_name)
{
  ffi_define_objc_method ("objc-instance-method", m, c, category_name);
}

void
ffi_rest_of_objc_class_compilation (tree node)
{
  tree n;
  char *category_name = NULL;

  ffi_define_objc_class (node);

  for (n = CLASS_CLS_METHODS (node); n; n = TREE_CHAIN (n))
    ffi_define_objc_class_method (n, node, category_name);
  for (n = CLASS_NST_METHODS (node); n; n = TREE_CHAIN (n))
    ffi_define_objc_instance_method (n, node, category_name);
}

void
ffi_rest_of_objc_category_compilation (tree category)
{
  tree n;
  const char *category_name = IDENTIFIER_POINTER (CLASS_SUPER_NAME (category));

  for (n = CLASS_CLS_METHODS (category); n; n = TREE_CHAIN (n))
    ffi_define_objc_class_method (n, category, category_name);
  for (n = CLASS_NST_METHODS (category); n; n = TREE_CHAIN (n))
    ffi_define_objc_instance_method (n, category, category_name);
}

void
ffi_rest_of_objc_protocol_compilation (tree p)
{
  tree n;
  char *category_name;
  
  for (n = PROTOCOL_CLS_METHODS (p); n; n = TREE_CHAIN (n))
    ffi_define_objc_method ("objc-protocol-class-method", 
                            n, 
                            p, 
                            category_name);
  for (n = PROTOCOL_NST_METHODS (p); n; n = TREE_CHAIN (n))
    ffi_define_objc_method ("objc-protocol-instance-method", 
                            n, 
                            p, 
                            category_name);
}

void
ffi_rest_of_decl_compilation (tree decl, 
			      int top_level ATTRIBUTE_UNUSED, 
			      int at_end ATTRIBUTE_UNUSED)
{
  /* tree type = TREE_TYPE (decl); */

  if ((! ffi_initialized) || errorcount)
    return;


  if (TREE_CODE (decl) == ERROR_MARK) 
    return;

#if 0
  fprintf(stderr, "ffi_rest_of_decl_compilation:\n");
  debug_tree(decl); 
  fprintf(stderr, "\n");
#endif


  switch (TREE_CODE (decl))
    {
    case TYPE_DECL:
      break;

    case FUNCTION_DECL:
      ffi_define_function (decl);
      break;

    case VAR_DECL:
      if (DECL_SOURCE_LINE (decl))
	ffi_define_variable (decl);
      break;

    case PARM_DECL:
    case RESULT_DECL:
      fprintf(stderr, " ??? Parm or Result \n");
      debug_tree (decl);
      abort ();
      break;

    case CONST_DECL:
      fprintf(stderr, " --- Maybe an enum ?\n");
      debug_tree (decl);
      abort ();
      break;

    default:
      fprintf(stderr, " ??? Unknown \n");
      debug_tree (decl);
      abort ();
      break;
    }
}

void
ffi_rest_of_type_compilation (tree type, int top_level ATTRIBUTE_UNUSED)
{
  if ((! ffi_initialized) || errorcount)
    return;

  if (TREE_CODE (type) == ERROR_MARK)
    return;
  
  ffi_emit_type (ffi_create_type_info (type), type);
}

static int
is_whitespace (char c)
{
  switch (c)
    {
    case ' ': case '\n': case '\t': case '\f': case '\r': 
      return 1;
    default:
      return 0;
    }
}

/*
  Emit the macro name (possibly including a parenthesized arglist).
  Surround whatever's emitted here with double quotes.
  Return a pointer to the first character of the (possibly empty) definition.
*/
static const char *
ffi_emit_macro_name (const char *p)
{
  int in_arglist = 0;
  char c;

  putc ('\"', ffifile);
  while ((c = *p) != 0)
    {
      switch (c)
	{
	case '(':
	  in_arglist = 1;
	  /* Fall down */
	case ',':
	  putc (' ', ffifile);
	  putc (c, ffifile);
	  putc (' ', ffifile);
	  break;

	case ')':
	  putc (' ', ffifile);
	  putc (c, ffifile);
	  putc ('\"', ffifile);
	  
	  while (is_whitespace (*++p));
	  return p;
	
	default:
	  if (is_whitespace (c) && !in_arglist)
	    {
	      putc ('\"', ffifile);
	      while (is_whitespace (*++p));
	      return p;
	    }
	  putc (c, ffifile);
	}
      ++p;
    }
  putc ('\"', ffifile);
  return p;
}

static void
ffi_emit_macro_expansion (char *p)
{
  char c;
  char *q = p + strlen (p);
  
  while ((q > p) && is_whitespace (q[-1])) q--;
  *q = '\0';

  fprintf (ffifile, " \"");

  while ((c = *p++) != '\0')
    {
      if (c == '\"')
	fputc ('\\', ffifile);
      fputc (c, ffifile);
    }
  fputc ('\"', ffifile);
}
  
static void
ffi_debug_define (unsigned lineno, const char *macro)
{
  if (ffifile) {
    fprintf (ffifile, "(macro (\"%s\" %d) ", ffi_current_source_file, lineno);
    ffi_emit_macro_expansion ((char*)ffi_emit_macro_name (macro));
    fprintf (ffifile, ")\n");
  }
}

static void
ffi_debug_undef (unsigned lineno ATTRIBUTE_UNUSED, const char *buffer)
{
  fprintf (ffifile, "(undef-macro \"%s\")\n", buffer);
}

static void
ffi_debug_type_decl (tree decl, int local)
{
  tree type;

  if (errorcount)
    return;

  if ((! local) && ffi_initialized) {
    switch (TREE_CODE (decl))
      {
      case TYPE_DECL:
        type = TREE_TYPE (decl);
        if (DECL_NAME (decl))
          ffi_define_type (decl, FFI_TYPE_TYPEDEF);
        else
          {
            if (TREE_CODE (type) != ERROR_MARK)
              ffi_emit_type (ffi_create_type_info (type), type);
          }
        break;
        
      default:
        fprintf (stderr, "Huh?");
        debug_tree (decl);
        abort ();
      }
  }
}


static struct gcc_debug_hooks ffi_debug_hooks;

void
ffi_early_init (void)
{
  ffi_typevec_len = 100;
  ffi_typevec = xmalloc (ffi_typevec_len * sizeof ffi_typevec[0]);
  memset ((char *) ffi_typevec, 0, ffi_typevec_len * sizeof ffi_typevec[0]);
  /* Override debug hooks for macros. We don't emit assembly, so this
     shouldn't cause problems. */
  memcpy (&ffi_debug_hooks, &do_nothing_debug_hooks, sizeof ffi_debug_hooks);
  ffi_debug_hooks.start_source_file = ffi_debug_start_source_file;
  ffi_debug_hooks.end_source_file = ffi_debug_end_source_file;
  ffi_debug_hooks.define = ffi_debug_define;
  ffi_debug_hooks.undef = ffi_debug_undef;
  ffi_debug_hooks.type_decl = ffi_debug_type_decl;
  debug_hooks = &ffi_debug_hooks;
}

void
ffi_init (FILE *ffi_file, char *input_file_name)
{
  int i;

  ffifile = ffi_file ? ffi_file : stderr;
  ffi_current_source_file = (char *) xstrdup (input_file_name);
  ffi_primary_source_file = ffi_current_source_file;

  for (i = 0; i < itk_none; i++) 
    {
      ffi_define_builtin_type (integer_types[i]);
    }
  for (i = 0; i < TI_MAX; i++) 
    {
      ffi_define_builtin_type (global_trees[i]);
    }


  ffi_initialized = 1;
}

