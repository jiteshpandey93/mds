/**
 * mds — A micro-display server
 * Copyright © 2014  Mattias Andrée (maandree@member.fsf.org)
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "make-tree.h"

#include "raw-data.h"

#include <libmdsserver/macros.h>

#include <limits.h>
#include <stdlib.h>
#include <libgen.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>



/**
 * Wrapper around `asprintf` that makes sure that first
 * argument gets set to `NULL` on error and that zero is
 * returned on success rather than the number of printed
 * characters
 * 
 * @param   VAR:char**            The output parameter for the string
 * @param   ...:const char*, ...  The format string and arguments
 * @return  :int                  Zero on success, -1 on error
 */
#define xasprintf(VAR, ...)					\
  (asprintf(&(VAR), __VA_ARGS__) < 0 ? (VAR = NULL, -1) : 0)


/**
 * Check whether a value is inside a closed range
 * 
 * @param   LOWER:¿T?  The lower bound, inclusive
 * @param   VALUE:¿T?  The value to test
 * @param   UPPER:¿T?  The upper bound, inclusive
 * @return  :int       1 if `LOWER` ≤ `VALUE` ≤ `UPPER`, otherwise 0
 */
#define in_range(LOWER, VALUE, UPPER)			\
  (((LOWER) <= (VALUE)) && ((VALUE) <= (UPPER)))


/**
 * Check whether a character is a valid callable name character, forward slash is accepted
 * 
 * @param   C:char  The character
 * @return  :int    Zero if `C` is a valid callable name character or a forward slash, otherwise 0
 */
#define is_name_char(C)									\
  (in_range('a', C, 'z') || in_range('A', C, 'Z') || strchr("0123456789_/", C))


/**
 * Add an error the to error list
 * 
 * @param  ERROR_IS_IN_FILE:int  Whether the error is in the layout code
 * @param  SEVERITY:identifier   * in `MDS_KBDC_PARSE_ERROR_*` to indicate severity
 * @param  ...:const char*, ...  Error description format string and arguments
 */
#define NEW_ERROR(ERROR_IS_IN_FILE, SEVERITY, ...)					\
  if (errors_ptr + 1 >= errors_size)							\
    {											\
      errors_size = errors_size ? (errors_size << 1) : 2;				\
      fail_if (xxrealloc(old_errors, *errors, errors_size, mds_kbdc_parse_error_t*));	\
    }											\
  fail_if (xcalloc(error, 1, mds_kbdc_parse_error_t));					\
  (*errors)[errors_ptr + 0] = error;							\
  (*errors)[errors_ptr + 1] = NULL;							\
  errors_ptr++;										\
  error->line  = line_i;								\
  error->severity = MDS_KBDC_PARSE_ERROR_##SEVERITY;					\
  error->error_is_in_file = ERROR_IS_IN_FILE;						\
  error->start = (size_t)(line - source_code.lines[line_i]);				\
  error->end   = (size_t)(end  - source_code.lines[line_i]);				\
  fail_if ((error->pathname = strdup(pathname)) == NULL);				\
  if (ERROR_IS_IN_FILE)									\
    fail_if ((error->code = strdup(source_code.real_lines[line_i])) == NULL);		\
  fail_if (xasprintf(error->description, __VA_ARGS__))


/**
 * Create a new node
 * 
 * @param  LOWERCASE:identifier  The keyword, for the node type, in lower case
 * @param  UPPERCASE:identifier  The keyword, for the node type, in upper case
 */
#define NEW_NODE(LOWERCASE, UPPERCASE)				\
  mds_kbdc_tree_##LOWERCASE##_t* node;				\
  fail_if (xcalloc(node, 1, mds_kbdc_tree_##LOWERCASE##_t));	\
  node->type = MDS_KBDC_TREE_TYPE_##UPPERCASE


/**
 * Update the tip of the tree stack with the current node
 * and change the pointer at the tip to the pointer to the
 * current node's down pointer
 * 
 * This is what should be done when a branch node has
 * been created and should be added to the result tree
 * 
 * @param  KEYWORD:const char*  The keyword for the current node's type
 */
#define BRANCH(KEYWORD)					\
  *(tree_stack[stack_ptr]) = (mds_kbdc_tree_t*)node;	\
  tree_stack[stack_ptr + 1] = &(node->inner);		\
  keyword_stack[stack_ptr++] = KEYWORD


/**
 * Update the tip of the tree stack with the current node
 * and change the pointer at the tip to the pointer to the
 * current node's next pointer
 * 
 * This is what should be done when a leaf node has been
 * created and should be added to the result tree
 */
#define LEAF							\
  *(tree_stack[stack_ptr]) = (mds_kbdc_tree_t*)node;		\
  tree_stack[stack_ptr] = &(tree_stack[stack_ptr][0]->next)


/**
 * Check that there are no tokens after a keyword
 * 
 * @param  KEYWORD:const char*  The keyword,
 */
#define NO_PARAMETERS(KEYWORD)						\
  line += strlen(line);							\
  *end = prev_end_char, prev_end_char = '\0';				\
  while (*line && (*line == ' '))					\
    line++;								\
  do									\
    if (*line)								\
      {									\
	end = line + strlen(line);					\
	NEW_ERROR(1, ERROR, "extra token after ‘%s’", KEYWORD);		\
      }									\
  while (0)


/**
 * Take next parameter, which should be a name of a callable,
 * and store it in the current node
 * 
 * @param  var:identifier  The name of the member variable, for the current
 *                         node, where the parameter should be stored
 */
#define NAMES_1(var)								\
  line += strlen(line);								\
  *end = prev_end_char, prev_end_char = '\0';					\
  while (*line && (*line == ' '))						\
    line++;									\
  do										\
    if (*line == '\0')								\
      {										\
	line = original, end = line + strlen(line);				\
	NEW_ERROR(1, ERROR, "a name is expected");				\
      }										\
    else									\
      {										\
	char* name_end = line;							\
	char* test;								\
	int stray_char = 0;							\
	while (*name_end && is_name_char(*name_end))				\
	  name_end++;								\
	if (*name_end && (*name_end != ' '))					\
	  {									\
	    char* end_end = name_end + 1;					\
	    while ((*end_end & 0xC0) == 0x80)					\
	      end_end++;							\
	    prev_end_char = *end_end, *end_end = '\0';				\
	    NEW_ERROR(1, ERROR, "stray ‘%s’ character", name_end);		\
	    error->start = (size_t)(name_end - source_code.lines[line_i]);	\
	    error->end   = (size_t)(end_end  - source_code.lines[line_i]);	\
	    *end_end = prev_end_char;						\
	    stray_char = 1;							\
	  }									\
	test = name_end;							\
	while (*test && (*test == ' '))						\
	  test++;								\
	if (*test && !stray_char)						\
	  {									\
	    NEW_ERROR(1, ERROR, "too many parameters");				\
	    error->start = (size_t)(test - source_code.lines[line_i]);		\
	    error->end   = strlen(source_code.lines[line_i]);			\
	  }									\
	end = name_end;								\
	prev_end_char = *end;							\
	*end = '\0';								\
	fail_if ((node->var = strdup(line)) == NULL);				\
      }										\
  while (0)


/**
 * Suppress the next `line += strlen(line)`
 */
#define NO_JUMP			\
  *end = prev_end_char;		\
  end = line;			\
  prev_end_char = *end;		\
  *end = '\0'


/**
 * Take next parameter, which should be a string or numeral,
 * and store it in the current node
 * 
 * @param  var:identifier  The name of the member variable, for the current
 *                         node, where the parameter should be stored
 */
#define CHARS(var)									\
  do											\
    {											\
      if (too_few)									\
	break;										\
      line += strlen(line);								\
      *end = prev_end_char, prev_end_char = '\0';					\
      while (*line && (*line == ' '))							\
	line++;										\
      if (*line == '\0')								\
	{										\
	  line = original, end = line + strlen(line);					\
	  NEW_ERROR(1, ERROR, "too few parameters");					\
	  line = end, too_few = 1;							\
	}										\
      else										\
	{										\
	  char* arg_end = line;								\
	  char* call_end = arg_end;							\
	  int escape = 0, quote = 0;							\
	  while (*arg_end)								\
	    {										\
	      char c = *arg_end++;							\
	      if      (escape)               escape = 0;				\
	      else if (arg_end <= call_end)  ;						\
	      else if (c == '\\')							\
		{									\
		  escape = 0;								\
		  call_end = arg_end + get_end_of_call(arg_end, 0, strlen(arg_end)); 	\
		}									\
	      else if (quote)                quote = (c != '"');			\
	      else if (c == ' ')             break;					\
	      else                           quote = (c == '"');			\
	    }										\
	  prev_end_char = *arg_end, *arg_end = '\0';					\
	  fail_if ((node->var = strdup(line)) == NULL);					\
	  end = line = arg_end;								\
	}										\
    }											\
  while (0)


/**
 * Test that there are no more parameters
 */
#define END							\
  while (*line && (*line == ' '))				\
    line++;							\
  do								\
    if (*line)							\
      {								\
	NEW_ERROR(1, ERROR, "too many parameters");		\
	error->end = strlen(source_code.lines[line_i]);		\
      }								\
  while (0)


/**
 * Test that the next parameter is in quotes
 */
#define QUOTES								\
  do									\
    {									\
      char* line_ = line;						\
      line += strlen(line);						\
      *end = prev_end_char;						\
      while (*line && (*line == ' '))					\
	line++;								\
      if (*line && (*line != '"'))					\
	{								\
	  char* arg_end = line;						\
	  while (*arg_end && (*arg_end != ' '))				\
	    arg_end++;							\
	  NEW_ERROR(1, ERROR, "parameter must be in quotes");		\
	  error->end = (size_t)(arg_end - source_code.lines[line_i]);	\
	}								\
      *end = '\0';							\
      line = line_;							\
    }									\
  while (0)


/**
 * Check that there is exactly one parameter, that it is in
 * quotes, and add it to the current node
 * 
 * @param  var:identifier  The name of the member variable, for the current
 *                         node, where the parameter should be stored
 */
#define QUOTES_1(var)	\
  QUOTES;		\
  CHARS(var);		\
  END


/**
 * Check that the next word is a specific keyword
 * 
 * @parma  KEYWORD:const char*  The keyword
 */
#define TEST_FOR_KEYWORD(KEYWORD)							\
  do											\
    {											\
      if (too_few)									\
	break;										\
      line += strlen(line);								\
      *end = prev_end_char, prev_end_char = '\0';					\
      while (*line && (*line == ' '))							\
	line++;										\
      if (*line == '\0')								\
	{										\
	  line = original, end = line + strlen(line);					\
	  NEW_ERROR(1, ERROR, "too few parameters");					\
	  line = end, too_few = 1;							\
	}										\
      else										\
	{										\
	  int ok = (strstr(line, KEYWORD) == line);					\
	  line += strlen(KEYWORD);							\
	  ok = ok && ((*line == '\0') || (*line == ' '));				\
	  if (ok)									\
	    {										\
	      end = line;								\
	      prev_end_char = *end, *end = '\0';					\
	      break;									\
	    }										\
	  line -= strlen(KEYWORD);							\
	  end = line;									\
	  while (*end && (*end != ' '))							\
	    end++;									\
	  prev_end_char = *end, *end = '\0';						\
	  NEW_ERROR(1, ERROR, "expecting keyword ‘%s’", KEYWORD);			\
	}										\
    }											\
  while (0)


/**
 * Take next parameter, which should be a key combination or strings,
 * and store it in the current node
 * 
 * @param  var:identifier  The name of the member variable, for the current
 *                         node, where the parameter should be stored
 */
#define KEYS(var)									\
  do											\
    {											\
      if (too_few)									\
	break;										\
      line += strlen(line);								\
      *end = prev_end_char, prev_end_char = '\0';					\
      while (*line && (*line == ' '))							\
	line++;										\
      if (*line == '\0')								\
	{										\
	  line = original, end = line + strlen(line);					\
	  NEW_ERROR(1, ERROR, "too few parameters");					\
	  line = end, too_few = 1;							\
	}										\
      else										\
	{										\
	  char* arg_end = line;								\
	  char* call_end = arg_end;							\
	  int escape = 0, quote = 0, triangle = (*arg_end == '<');			\
	  while (*arg_end)								\
	    {										\
	      char c = *arg_end++                ;					\
	      if      (escape)                   escape = 0;				\
	      else if (arg_end <= call_end)      ;					\
	      else if (c == '\\')							\
		{									\
		  escape = 0;								\
		  call_end = arg_end + get_end_of_call(arg_end, 0, strlen(arg_end));	\
		}									\
	      else if (quote)                    quote = (c != '"');			\
	      else if (c == '\"')                quote = 1;				\
	      else if (c == '>')                 triangle = 0;				\
	      else if ((c == ' ') && !triangle)  break;					\
	    }										\
	  prev_end_char = *arg_end, *arg_end = '\0';					\
	  if (*line == '<')								\
	    {										\
	      mds_kbdc_tree_keys_t* subnode;						\
	      fail_if (xcalloc(subnode, 1, mds_kbdc_tree_keys_t));			\
	      subnode->type = MDS_KBDC_TREE_TYPE_KEYS;					\
	      node->var = (mds_kbdc_tree_t*)subnode;					\
	      fail_if ((subnode->keys = strdup(line)) == NULL);				\
	    }										\
	  else										\
	    {										\
	      mds_kbdc_tree_string_t* subnode;						\
	      fail_if (xcalloc(subnode, 1, mds_kbdc_tree_string_t));			\
	      subnode->type = MDS_KBDC_TREE_TYPE_STRING;				\
	      node->var = (mds_kbdc_tree_t*)subnode;					\
	      fail_if ((subnode->string = strdup(line)) == NULL);			\
	    }										\
	  end = line = arg_end;								\
	}										\
    }											\
  while (0)



/**
 * Parse a file into a syntex tree
 * 
 * @param   filename  The filename of the file to parse
 * @param   result    Output parameter for the root of the tree, `NULL` if -1 is returned
 * @param   errors    `NULL`-terminated list of found error, `NULL` if no errors were found or if -1 is returned
 * @return            -1 if an error occursed that cannot be stored in `*errors`, zero otherwise
 */
int parse_to_tree(const char* restrict filename, mds_kbdc_tree_t** restrict result,
		  mds_kbdc_parse_error_t*** restrict errors)
{
  mds_kbdc_parse_error_t* error;
  mds_kbdc_parse_error_t** old_errors = NULL;
  char* pathname;
  source_code_t source_code;
  size_t errors_size = 0;
  size_t errors_ptr = 0;
  size_t line_i, line_n;
  const char** keyword_stack = NULL;
  mds_kbdc_tree_t*** tree_stack = NULL;
  size_t stack_ptr = 0;
  int saved_errno, in_array = 0;
  
  *result = NULL;
  *errors = NULL;
  source_code_initialise(&source_code);
  
  /* Get a non-relative pathname for the file, relative filenames
   * can be misleading as the program can have changed working
   * directroy to be able to resolve filenames. */
  pathname = realpath(filename, NULL);
  fail_if (pathname == NULL);
  
  /* Check that the file exists and can be read. */
  if (access(pathname, R_OK) < 0)
    {
      saved_errno = errno;
      fail_if (xmalloc(*errors, 2, mds_kbdc_parse_error_t*));
      fail_if (xmalloc(**errors, 1, mds_kbdc_parse_error_t));
      (*errors)[1] = NULL;
      
      (**errors)->severity = MDS_KBDC_PARSE_ERROR_ERROR;
      (**errors)->error_is_in_file = 0;
      (**errors)->pathname = pathname, pathname = NULL;
      (**errors)->line = 0;
      (**errors)->start = 0;
      (**errors)->end = 0;
      (**errors)->code = NULL;
      (**errors)->description = strdup(strerror(saved_errno));
      fail_if ((**errors)->description == NULL);
      return 0;
    }
  
  /* Read the file and simplify it a bit. */
  fail_if (read_source_lines(pathname, &source_code) < 0);
  /* TODO '\t':s should be expanded into ' ':s. */
  
  /* Allocate stacks needed to parse the tree. */
  fail_if (xmalloc(keyword_stack, source_code.line_count, const char*));
  fail_if (xmalloc(tree_stack, source_code.line_count + 1, mds_kbdc_tree_t**));
  /* Create a node-slot for the tree root. */
  *tree_stack = result;
  
  for (line_i = 0, line_n = source_code.line_count; line_i < line_n; line_i++)
    {
      char* line = source_code.lines[line_i];
      char* end;
      char prev_end_char;
      char* original;
      int too_few = 0;
      
      while (*line && (*line == ' '))
	line++;
      end = strchrnul(line, ' ');
      if (end == line)
	continue;
      prev_end_char = *end;
      *end = '\0';
      original = line;
      
    redo:
      if (in_array)
	{
	  for (;;)
	    {
	      while (*line && (*line == ' '))
		line++;
	      if (*line == '\0')
		break;
	      if (*line == '}')
		{
		  line++;
		  end = line + strlen(line);
		  END;
		  line = end, prev_end_char = '\0';
		  stack_ptr--;
		  in_array = 0;
		  break;
		}
	      {
#define node subnode
		NEW_NODE(string, STRING);
		NO_JUMP;
		CHARS(string);
		LEAF;
		*end = prev_end_char;
		line = end;
#undef node
	      }
	    }
	  if (*line == '\0')
	    continue;
	}
      else if (!strcmp(line, "information"))
	{
	  NEW_NODE(information, INFORMATION);
	  NO_PARAMETERS("information");
	  BRANCH("information");
	}
      else if (!strcmp(line, "assumption"))
	{
	  NEW_NODE(assumption, ASSUMPTION);
	  NO_PARAMETERS("assumption");
	  BRANCH("assumption");
	}
      else if (!strcmp(line, "return"))
	{
	  NEW_NODE(return, RETURN);
	  NO_PARAMETERS("return");
	  LEAF;
	}
      else if (!strcmp(line, "continue"))
	{
	  NEW_NODE(continue, CONTINUE);
	  NO_PARAMETERS("continue");
	  LEAF;
	}
      else if (!strcmp(line, "break"))
	{
	  NEW_NODE(break, BREAK);
	  NO_PARAMETERS("break");
	  LEAF;
	}
      else if (!strcmp(line, "language"))
	{
	  NEW_NODE(information_language, INFORMATION_LANGUAGE);
	  QUOTES_1(data);
	  LEAF;
	}
      else if (!strcmp(line, "country"))
	{
	  NEW_NODE(information_country, INFORMATION_COUNTRY);
	  QUOTES_1(data);
	  LEAF;
	}
      else if (!strcmp(line, "variant"))
	{
	  NEW_NODE(information_variant, INFORMATION_VARIANT);
	  QUOTES_1(data);
	  LEAF;
	}
      else if (!strcmp(line, "include"))
	{
	  NEW_NODE(include, INCLUDE);
	  QUOTES_1(filename);
	  LEAF;
	}
      else if (!strcmp(line, "function"))
	{
	  NEW_NODE(function, FUNCTION);
	  NAMES_1(name);
	  BRANCH("function");
	}
      else if (!strcmp(line, "macro"))
	{
	  NEW_NODE(macro, MACRO);
	  NAMES_1(name);
	  BRANCH("macro");
	}
      else if (!strcmp(line, "if"))
	{
	  NEW_NODE(if, IF);
	  CHARS(condition);
	  END;
	  BRANCH("if");
	}
      else if (!strcmp(line, "else"))
	{
	  if (stack_ptr == 0)
	    {
	      NEW_ERROR(1, ERROR, "runaway ‘else’ statement");
	      goto next;
	    }
	  line += strlen(line);
	  *end = prev_end_char, prev_end_char = '\0';
	  end = line + strlen(line);
	  stack_ptr--;
	  while (*line && (*line == ' '))
	    line++;
	  if (strcmp(keyword_stack[stack_ptr], "if"))
	    {
	      line = original, end = line + strlen(line);
	      NEW_ERROR(1, ERROR, "runaway ‘else’ statement");
	    }
	  else if (*line == '\0')
	    {
	      /* else */
	      mds_kbdc_tree_if_t* node = &(tree_stack[stack_ptr][0]->if_);
	      tree_stack[stack_ptr + 1] = &(node->otherwise);
	      keyword_stack[stack_ptr++] = "if";
	    }
	  else if ((strstr(line, "if") == line) && ((line[3] == ' ') || (line[3] == '\0')))
	    {
	      /* else if */
	      NEW_NODE(if, IF);
	      tree_stack[stack_ptr][0]->if_.otherwise = (mds_kbdc_tree_t*)node;
	      line += 2;
	      CHARS(condition);
	      END;
	      BRANCH("if");
	    }
	  else
	    {
	      NEW_ERROR(1, ERROR, "expecting nothing or ‘if’");
	    }
	}
      else if (!strcmp(line, "for"))
	{
	  NEW_NODE(for, FOR);
	  CHARS(first);
	  TEST_FOR_KEYWORD("to");
	  CHARS(last);
	  TEST_FOR_KEYWORD("as");
	  CHARS(variable);
	  END;
	  BRANCH("for");
	}
      else if (!strcmp(line, "let"))
	{
	  NEW_NODE(let, LET);
	  CHARS(variable);
	  TEST_FOR_KEYWORD(":");
	  *end = prev_end_char;
	  while (*line && (*line == ' '))
	    line++;
	  if (*line == '\0')
	    {
	      line = original, end = line + strlen(line), prev_end_char = '\0';
	      NEW_ERROR(1, ERROR, "too few parameters");
	      LEAF;
	    }
	  else if (*line != '{')
	    {
#define node subnode
	      NEW_NODE(string, STRING);
	      NO_JUMP;
	      CHARS(string);
#undef node
	      node->value = (mds_kbdc_tree_t*)subnode;
	      END;
	      LEAF;
	    }
	  else
	    {
#define node subnode
	      NEW_NODE(array, ARRAY);
#define inner elements
	      BRANCH("}");
#undef inner
#undef node
	      in_array = 1;
	      line++;
	      goto redo;
	    }
	}
      else if (!strcmp(line, "have"))
	{
	  NEW_NODE(assumption_have, ASSUMPTION_HAVE);
	  KEYS(data);
	  END;
	  LEAF;
	}
      else if (!strcmp(line, "have_chars"))
	{
	  NEW_NODE(assumption_have_chars, ASSUMPTION_HAVE_CHARS);
	  QUOTES_1(chars);
	  LEAF;
	}
      else if (!strcmp(line, "have_range"))
	{
	  NEW_NODE(assumption_have_range, ASSUMPTION_HAVE_RANGE);
	  CHARS(first);
	  CHARS(last);
	  END;
	  LEAF;
	}
      else if (!strcmp(line, "end"))
	{
	  if (stack_ptr == 0)
	    {
	      NEW_ERROR(1, ERROR, "runaway ‘end’ statement");
	      goto next;
	    }
	  line += strlen(line);
	  *end = prev_end_char, prev_end_char = '\0';
	  stack_ptr--;
	  while (*line && (*line == ' '))
	    line++;
	  if (*line == '\0')
	    {
	      line = original, end = line + strlen(line);
	      NEW_ERROR(1, ERROR, "expecting a keyword after ‘end’");
	    }
	  else if (strcmp(line, keyword_stack[stack_ptr]))
	    {
	      NEW_ERROR(1, ERROR, "expected ‘%s’ but got ‘%s’", keyword_stack[stack_ptr], line);
	    }
	  tree_stack[stack_ptr] = &(tree_stack[stack_ptr][0]->next);
	}
      else if (strchr("\"<([", *line) || strchr(line, '('))
	; /* TODO */
      else
	{
	  NEW_ERROR(1, ERROR, "invalid syntax ‘%s’", line);
	}
      
    next:
      *end = prev_end_char;
    }
  
  /* Check that all scopes have been popped. */
  if (stack_ptr)
    {
      char* line = NULL;
      char* end = NULL;
      NEW_ERROR(0, ERROR, "premature end of file");
      while (stack_ptr--)
	/* TODO from where? */
	if (!strcmp(keyword_stack[stack_ptr], "}"))
	  {
	    NEW_ERROR(0, NOTE, "missing ‘%s’", keyword_stack[stack_ptr]);
	  }
	else
	  {
	    NEW_ERROR(0, NOTE, "missing ‘end %s’", keyword_stack[stack_ptr]);
	  }
    }
  
  free(pathname);
  free(keyword_stack);
  free(tree_stack);
  source_code_destroy(&source_code);
  return 0;
  
 pfail:
  saved_errno = errno;
  free(pathname);
  free(keyword_stack);
  free(tree_stack);
  source_code_destroy(&source_code);
  mds_kbdc_parse_error_free_all(old_errors);
  mds_kbdc_parse_error_free_all(*errors), *errors = NULL;
  mds_kbdc_tree_free(*result), *result = NULL;
  return errno = saved_errno, -1;
}



#undef KEYS
#undef TEST_FOR_KEYWORD
#undef QUOTES_1
#undef QUOTES
#undef END
#undef CHARS
#undef NAMES_1
#undef NO_PARAMETERS
#undef LEAF
#undef BRANCH
#undef NEW_NODE
#undef NEW_ERROR
#undef is_name_char
#undef in_range
#undef xasprintf

