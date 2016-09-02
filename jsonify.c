#include "jsonify.h"

static int sp = 0;
static int stack[MAXSTACK];

int
from_loose_to_strict(char *output, size_t outputsize, char *input, ssize_t inputlen)
{
  ssize_t nrtokens;
  jsmntype_t jt;
  jsmn_parser parser;
  jsmntok_t tokens[TOKENS];

  jsmn_init(&parser);

  nrtokens = from_loose(&parser, input, inputlen, tokens, TOKENS);
  return to_strict(output, outputsize, input, tokens, nrtokens);
}

ssize_t
from_loose(jsmn_parser *p, char *line, ssize_t linelen, jsmntok_t *tokens, ssize_t nrtokens)
{
  return jsmn_parse(p, line, linelen, tokens, nrtokens);
}

int
to_strict(char *output, size_t outputsize, const char *input, jsmntok_t *tokens, int nrtokens)
{
  char *key, c;
  jsmntok_t *tok;
  int len, i, j;

  if (outputsize < 1)
    return -1;

  // ensure termination
  *output = '\0';

  for (i = 0; i < nrtokens; i++) {
    tok = &tokens[i];
    key = strndup(input + tok->start, tok->end - tok->start);

    // 1.
    switch (tok->type) {
    case JSMN_OBJECT:
      strlcat(output, "{", outputsize);
      push('}');
      break;
    case JSMN_ARRAY:
      strlcat(output, "[", outputsize);
      push(']');
      break;
    case JSMN_UNDEFINED:
      if (tok->size) { // quote keys
        strlcat(output, "\"undefined\":", outputsize);
      } else { // don't quote values
        strlcat(output, key, outputsize);
      }
      break;
    case JSMN_STRING:
      strlcat(output, "\"", outputsize);
      strlcat(output, key, outputsize);
      strlcat(output, "\"", outputsize);
      if (tok->size) // this is a key
        strlcat(output, ":", outputsize);
      break;
    case JSMN_PRIMITIVE:
      // convert single quotes at beginning and end of string
      if (key[0] == '\'')
        key[0] = '"';
      if (key[strlen(key) - 1] == '\'')
        key[strlen(key) - 1] = '"';

      if (tok->size) { // quote keys
        strlcat(output, "\"", outputsize);
        strlcat(output, key, outputsize);
        strlcat(output, "\":", outputsize);
      } else // don't quote values
        strlcat(output, key, outputsize);
      break;
    default:
      fatal("unknown json token type");
    }

    // 2.
    for (j = 0; j < tok->size - 1; j++)
      switch (tok->type) {
      case JSMN_OBJECT:
        if (push(',') == -1)
          fatal("stack push error");
        break;
      case JSMN_ARRAY:
        if (push(',') == -1)
          fatal("stack push error");
        break;
      default:
        fatal("unknown json token type");
      }

    // 3.
    if (!tok->size) {
      if ((c = pop()) != -1) {
        if ((len = strlen(output)) >= (outputsize - 1)) {
          fprintf(stderr, "len: %d %s\n", len, output);
          fatal("output full");
        } else {
          output[len] = c;
          output[len + 1] = '\0';
        }
      }
      while (c == ']' || c == '}') {
        if ((c = pop()) != -1) {
          if ((len = strlen(output)) >= (outputsize - 1)) {
            fprintf(stderr, "len: %d %s\n", len, output);
            fatal("output full");
          } else {
            output[len] = c;
            output[len + 1] = '\0';
          }
        }
      }
    }
  }
 
  return 0;
}

// pop item from the stack
// return item on the stack on success, -1 on error
int pop()
{
  if (sp == 0)
    return -1;
  return stack[--sp];
}

// push new item on the stack
// return 0 on success, -1 on error
int push(int val)
{
  if (val == -1) // don't support -1 values, reserved for errors
    return -1;
  if (sp == MAXSTACK)
    return -1;
  stack[sp++] = val;
  return 0;
}
