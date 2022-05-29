
#include "aggregate/reducer.h"

///////////////////////////////////////////////////////////////////////////////////////////////

typedef struct {
  TrieMap *values;
  const RLookupKey *srckey;
} tolistCtx;

//---------------------------------------------------------------------------------------------

static void *tolistNewInstance(Reducer *rbase) {
  tolistCtx *ctx = Reducer_BlkAlloc(rbase, sizeof(*ctx), 100 * sizeof(*ctx));
  ctx->values = new TrieMap();
  ctx->srckey = rbase->srckey;
  return ctx;
}

//---------------------------------------------------------------------------------------------

static int tolistAdd(Reducer *rbase, void *ctx, const RLookupRow *srcrow) {
  tolistCtx *tlc = ctx;
  RSValue *v = RLookup_GetItem(tlc->srckey, srcrow);
  if (!v) {
    return 1;
  }

  // for non array values we simply add the value to the list */
  if (v->t != RSValue_Array) {
    uint64_t hval = v->Hash(0);
    if (TrieMap_Find(tlc->values, (char *)&hval, sizeof(hval)) == TRIEMAP_NOTFOUND) {

      tlc->values->Add((char *)&hval, sizeof(hval), v->MakePersistent()->IncrRef(), NULL);
    }
  } else {  // For array values we add each distinct element to the list
    uint32_t len = RSValue_ArrayLen(v);
    for (uint32_t i = 0; i < len; i++) {
      RSValue *av = v->ArrayItem(i);
      uint64_t hval = av->Hash(0);
      if (TrieMap_Find(tlc->values, (char *)&hval, sizeof(hval)) == TRIEMAP_NOTFOUND) {

        tlc->values->Add((char *)&hval, sizeof(hval), av->MakePersistent()->IncrRef(), NULL);
      }
    }
  }
  return 1;
}

//---------------------------------------------------------------------------------------------

static RSValue *tolistFinalize(Reducer *rbase, void *ctx) {
  tolistCtx *tlc = ctx;
  TrieMapIterator *it = tlc->values->Iterate("", 0);
  char *c;
  tm_len_t l;
  void *ptr;
  RSValue **arr = rm_calloc(tlc->values->cardinality, sizeof(RSValue));
  size_t i = 0;
  while (it->Next(&c, &l, &ptr)) {
    if (ptr) {
      arr[i++] = ptr;
    }
  }

  RSValue *ret = RSValue::NewArray(arr, i, RSVAL_ARRAY_ALLOC);
  return ret;
}

//---------------------------------------------------------------------------------------------

static void freeValues(void *ptr) {
  ((RSValue *)ptr)->Decref();
}

//---------------------------------------------------------------------------------------------

static void tolistFreeInstance(Reducer *parent, void *p) {
  tolistCtx *tlc = p;
  TrieMap_Free(tlc->values, freeValues);
}

//---------------------------------------------------------------------------------------------

Reducer *RDCRToList_New(const ReducerOptions *opts) {
  Reducer *r = rm_calloc(1, sizeof(*r));
  if (!ReducerOptions_GetKey(opts, &r->srckey)) {
    rm_free(r);
    return NULL;
  }
  r->Add = tolistAdd;
  r->Finalize = tolistFinalize;
  r->Free = Reducer_GenericFree;
  r->FreeInstance = tolistFreeInstance;
  r->NewInstance = tolistNewInstance;
  return r;
}

///////////////////////////////////////////////////////////////////////////////////////////////
