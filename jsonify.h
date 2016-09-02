#include "jsmn.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOKENS 100
#define MAXSTACK 100

int pop();
int push(int val);

int from_loose_to_strict(char *output, size_t outputsize, char *input, ssize_t inputlen);
ssize_t from_loose(jsmn_parser *p, char *line, ssize_t linelen, jsmntok_t *tokens, ssize_t nrtokens);
int to_strict(char *output, size_t outputsize, const char *input, jsmntok_t *tokens, int nrtokens);
