/*
 * dis-cil.c: Disassembles CIL byte codes
 *
 * Author:
 *   Miguel de Icaza (miguel@ximian.com)
 *
 * (C) 2001 Ximian, Inc.
 */
#include <config.h>
#include <glib.h>
#include <stdio.h>
#include <wchar.h>
#include "meta.h"
#include "get.h"
#include "dump.h"
#include "dis-cil.h"
#include "mono/metadata/opcodes.h"

/*
 * Strings on the US heap are encoded using UTF-16.  Poor man's
 * UTF-16 to UTF-8.  I know its broken, use libunicode later.
 */
static char *
get_encoded_user_string (const char *ptr)
{
	char *res;
	int len, i, j;

	len = mono_metadata_decode_blob_size (ptr, &ptr);
	res = g_malloc (len + 1);

	/*
	 * I should really use some kind of libunicode here
	 */
	for (i = 0, j = 0; i < len; j++, i += 2)
		res [j] = ptr [i];

	res [j] = 0;
		
	return res;
}

#define CODE_INDENT g_assert (indent_level < 512); \
	indent[indent_level*2] = ' ';	\
	indent[indent_level*2+1] = ' ';	\
	++indent_level;	\
	indent[indent_level*2] = 0;
#define CODE_UNINDENT g_assert (indent_level);	\
	--indent_level;	\
	indent[indent_level*2] = 0;

void
dissasemble_cil (MonoImage *m, MonoMethodHeader *mh) 
{
	const unsigned char *start = mh->code;
	int size = mh->code_size;
	const unsigned char *end = start + size;
	const unsigned char *ptr = start;
	const MonoOpcode *entry;
	char indent[1024];
	int i, indent_level = 0;
	char *clause_names[] = {"catch", "filter", "finally", "fault"};

	indent [0] = 0;

#ifdef DEBUG
	for (i = 0; i < mh->num_clauses; ++i) {
#define clause mh->clauses [i]
		g_print ("out clause %d: from %d len=%d, handler at %d, %d\n", 
			clause.flags, clause.try_offset, clause.try_len, clause.handler_offset, clause.handler_len);
#undef clause
	}
#endif
	while (ptr < end){
		for (i = mh->num_clauses - 1; i >= 0 ; --i) {
			if ((mh->clauses[i].flags == 0 || mh->clauses[i].flags == 2) && ptr == start + mh->clauses[i].try_offset) {
				fprintf (output, "\t%s.try { // %d\n", indent, i);
				CODE_INDENT;
			}
			if (ptr == start + mh->clauses[i].handler_offset) {
				char * klass = mh->clauses[i].flags ? g_strdup ("") : dis_stringify_token (m, mh->clauses[i].token_or_filter);
				fprintf (output, "\t%s%s %s { // %d\n", indent, clause_names [mh->clauses[i].flags], klass, i);
				CODE_INDENT;
				g_free (klass);
			}
		}
		fprintf (output, "\t%sIL_%04x: ", indent, (int) (ptr - start));
		i = *ptr;
		if (*ptr == 0xfe){
			ptr++;
			i = *ptr + 256;
		} 
		entry = &mono_opcodes [i];

		fprintf (output, "%s ", mono_opcode_names [i]);
		ptr++;
		switch (entry->argument){
		case MonoInlineBrTarget: {
			gint target = read32 (ptr);
			fprintf (output, "IL_%04x\n", ((int) (ptr - start)) + 4 + target);
			ptr += 4;
			break;
		}
			
		case MonoInlineField: {
			guint32 token = read32 (ptr);
			char *s;
			
			s = get_field (m, token);
			fprintf (output, "%s", s);
			g_free (s);
			ptr += 4;
			break;
		}
		
		case MonoInlineI: {
			int value = read32 (ptr);

			fprintf (output, "%d", value);
			ptr += 4;
			break;
		}
		
		case MonoInlineI8: {
			gint64 top = read64 (ptr);

			fprintf (output, "0x%llx", (long long) top);
			ptr += 8;
			break;
		}
		
		case MonoInlineMethod: {
			guint32 token = read32 (ptr);
			char *s;

			s = get_method (m, token);
			fprintf (output, "%s", s);
			g_free (s);
			ptr += 4;
			break;
		}
		
		case MonoInlineNone:
			break;
			
		case MonoInlineR: {
			double r = *(double *) ptr;
			fprintf (output, "%g", r);
			ptr += 8;
			break;
		}
		
		case MonoInlineSig: {
			guint32 token = read32 (ptr);
			fprintf (output, "signature-0x%08x", token);
			ptr += 4;
			break;
		}
		
		case MonoInlineString: {
			guint32 token = read32 (ptr);
			
			char *s = get_encoded_user_string (
				mono_metadata_user_string (m, token & 0xffffff));
			
			/*
			 * See section 23.1.4 on the encoding of the #US heap
			 */
			fprintf (output, "\"%s\"", s);
			g_free (s);
			ptr += 4;
			break;
		}

		case MonoInlineSwitch: {
			guint32 count = read32 (ptr);
			const unsigned char *endswitch;
			guint32 i;
			
			ptr += 4;
			endswitch = ptr + sizeof (guint32) * count;
			fprintf (output, "(\n");
			CODE_INDENT;
			for (i = 0; i < count; i++){
				fprintf (output, "\t%sIL_%04x%s", indent, 
					endswitch-start+read32 (ptr), 
					i == count - 1 ? ")" : ",\n");
				ptr += 4;
			}
			CODE_UNINDENT;
			break;
		}

		case MonoInlineTok: {
			guint32 token = read32 (ptr);
			char *s;
			
			s = get_token (m, token);
			fprintf (output, "%s", s);
			g_free (s);
			
			ptr += 4;
			break;
		}
		
		case MonoInlineType: {
			guint32 token = read32 (ptr);
			char *s = get_token_type (m, token);
			fprintf (output, "%s", s);
			g_free (s);
			ptr += 4;
			break;
		}

		case MonoInlineVar: {
			gint16 var_idx = read16 (ptr);

			fprintf (output, "variable-%d\n", var_idx);
			ptr += 2;
			break;
		}

		case MonoShortInlineBrTarget: {
			signed char x = *ptr;
			
			fprintf (output, "IL_%04x\n", ptr - start + 1 + x);
			ptr++;
			break;
		}

		case MonoShortInlineI: {
			char x = *ptr;

			fprintf (output, "0x%02x", x);
			ptr++;
			break;
		}

		case MonoShortInlineR: {
			float f = *(float *) ptr;

			fprintf (output, "%g", (double) f);
			ptr += 4;
			break;
		}

		case MonoShortInlineVar: {
			signed char x = *ptr;

			fprintf (output, "%d", (int) x);
			ptr++;
			break;
		}
		default:
			break;
		}

		fprintf (output, "\n");
		for (i = 0; i < mh->num_clauses; ++i) {
			if ((mh->clauses[i].flags == 0 || mh->clauses[i].flags == 2)  && ptr == start + mh->clauses[i].try_offset + mh->clauses[i].try_len) {
				CODE_UNINDENT;
				fprintf (output, "\t%s} // end .try %d\n", indent, i);
			}
			if (ptr == start + mh->clauses[i].handler_offset + mh->clauses[i].handler_len) {
				CODE_UNINDENT;
				fprintf (output, "\t%s} // end handler %d\n", indent, i);
			}
		}
	}
}
