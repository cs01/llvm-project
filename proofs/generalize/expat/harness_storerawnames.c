// storeRawNames' realloc fixup reads pointer values that realloc may already
// have deallocated. See the comment in the FINDING doc.
#include "lib/xmlparse.c"

unsigned nondet_uint(void);
int nondet_int(void);

#define NAME_MAX_ 4
#define RAW_MAX_  8

void harness(void) {
  // Deliberately NOT XML_ParserCreate: that drags in hash tables, the random
  // seed and the whole init path, and symex never gets out of it. storeRawNames
  // reads exactly two fields, so supply exactly two.
  // m_mem is const, so it has to be set in the initializer, not assigned.
  struct XML_ParserStruct ps = {.m_mem = {.malloc_fcn = malloc,
                                          .realloc_fcn = realloc,
                                          .free_fcn = free}};
  XML_Parser parser = &ps;

  TAG *tag = (TAG *)malloc(sizeof(TAG));
  __CPROVER_assume(tag != NULL);
  tag->parent = NULL;
  tag->bindings = NULL;

  // tag->buf holds the converted name; nameLen bytes of it are in use.
  unsigned nameChars = nondet_uint();
  __CPROVER_assume(nameChars >= 1 && nameChars <= NAME_MAX_);
  size_t bufBytes = sizeof(XML_Char) * (nameChars + 1);
  char *buf = (char *)malloc(bufBytes);
  __CPROVER_assume(buf != NULL);
  tag->buf.raw = buf;
  tag->bufEnd = buf + bufBytes;
  tag->name.strLen = (int)nameChars;
  tag->name.str = tag->buf.str;

  // Namespace processing on: localPart points into tag->buf.
  unsigned lpOff = nondet_uint();
  __CPROVER_assume(lpOff <= nameChars);
  tag->name.localPart = tag->buf.str + lpOff;
  tag->name.prefix = NULL;
  tag->name.uriLen = 0;
  tag->name.prefixLen = 0;

  // rawName lives in the input buffer, not in tag->buf: this is the
  // "not yet stored" state that makes storeRawNames do its work.
  int rawLen = nondet_int();
  __CPROVER_assume(rawLen >= 1 && rawLen <= RAW_MAX_);
  char *raw = (char *)malloc((size_t)rawLen);
  __CPROVER_assume(raw != NULL);
  tag->rawName = raw;
  tag->rawNameLength = rawLen;

  parser->m_tagStack = tag;
  (void)storeRawNames(parser);
}
