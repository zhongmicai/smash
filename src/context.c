#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include "defs.h"
#include "mem.h"
#include "common.h"
#include "context.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static HCCounters zeroCounters = {0x00, 0x00, 0x00, 0x00};
static HCCounters auxCounters;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void InitHashTable(CModel *M)
  { 
  M->hTable.entries  = (Entry **) Calloc(HASH_SIZE, sizeof(Entry *));
  M->hTable.counters = (HCCounters **) Calloc(HASH_SIZE, sizeof(HCCounters*));
  M->hTable.size     = (ENTMAX *) Calloc(HASH_SIZE, sizeof(ENTMAX));
  }

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void FreeCModel(CModel *M)
  {
  uint32_t k;
  if(M->mode == HASH_TABLE_MODE)
    {
    for(k = 0 ; k < HASH_SIZE ; ++k)
      {
      if(M->hTable.size[k] != 0)
        Free(M->hTable.entries[k]);
      if(M->hTable.counters[k] != NULL)
        Free(M->hTable.counters[k]);
      }
    Free(M->hTable.entries);
    Free(M->hTable.counters);
    Free(M->hTable.size);
    }
  else // TABLE_MODE
    Free(M->array.counters);
  Free(M);
  }

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void InitArray(CModel *M)
  {
  M->array.counters = (ACCounter *) Calloc(M->nPModels<<2, sizeof(ACCounter));
  }

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void InsertKey(HashTable *H, uint32_t i, uint64_t key)
  {
  H->entries[i] = (Entry *) Realloc(H->entries[i], (H->size[i]+1) * 
  sizeof(Entry), sizeof(Entry));
  H->entries[i][H->size[i]++].key = (uint32_t) (key&0xffffffff);
  }

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void InsertCounters(HashTable *H, uint32_t i, uint32_t nHCC, 
uint32_t k, uint32_t small)
  {
  H->counters[i] = (HCCounters *) Realloc(H->counters[i], (nHCC + 1) * 
  sizeof(HCCounters), sizeof(HCCounters));

  if(k < nHCC)
    memmove(H->counters[i][k+1], H->counters[i][k], (nHCC-k) * 
    sizeof(HCCounters));

  H->counters[i][k][0] =  small& 0x03;
  H->counters[i][k][1] = (small&(0x03<<2))>>2;
  H->counters[i][k][2] = (small&(0x03<<4))>>4;
  H->counters[i][k][3] = (small&(0x03<<6))>>6;
  }

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static HCCounter *GetHCCounters(HashTable *H, uint64_t key)
  {
  uint32_t k = 0, n, i = key % HASH_SIZE;
  uint64_t b = key & 0xffffffff00000000;

  for(n = 0 ; n < H->size[i] ; n++)
    {
    if(((uint64_t) H->entries[i][n].key | b) == key)
      switch(H->entries[i][n].counters)
        {
        case 0: return H->counters[i][k];
        default:
        auxCounters[0] =  H->entries[i][n].counters& 0x03;
        auxCounters[1] = (H->entries[i][n].counters&(0x03<<2))>>2;
        auxCounters[2] = (H->entries[i][n].counters&(0x03<<4))>>4;
        auxCounters[3] = (H->entries[i][n].counters&(0x03<<6))>>6;
        return auxCounters;
        }
    if(H->entries[i][n].counters == 0)
      k++;
    }
  return NULL;
  }

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

PModel *CreatePModel(uint32_t n)
  {
  PModel *P = (PModel *) Malloc(sizeof(PModel));
  P->freqs  = (uint32_t *) Malloc(n * sizeof(uint32_t));
  return P;
  }

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void UpdateCModelCounter(CModel *M, uint32_t s)
  {
  uint32_t  n;
  ACCounter *aCounters;
  uint64_t  idx = M->pModelIdx;

  if(M->mode == HASH_TABLE_MODE)
    {
    uint8_t small;
    uint32_t i, k = 0, nHCC, h = idx % HASH_SIZE;
    uint64_t b = idx & 0xffffffff00000000;

    for(n = 0 ; n < M->hTable.size[h] ; n++)
      {
      if(((uint64_t) M->hTable.entries[h][n].key | b) == idx)
        { 
        if(M->hTable.entries[h][n].counters == 0) // Update "large" counters
          {
          if(++M->hTable.counters[h][k][s] == 255)
            {
            M->hTable.counters[h][k][0] >>= 1;
            M->hTable.counters[h][k][1] >>= 1;
            M->hTable.counters[h][k][2] >>= 1;
            M->hTable.counters[h][k][3] >>= 1;
            }
          return;
          }
        
        small = (M->hTable.entries[h][n].counters>>(s<<1))&0x03;
        if(small == 3)
          {
          nHCC = k;
          for(i = n + 1 ; i < M->hTable.size[h] ; ++i)
            if(M->hTable.entries[h][i].counters == 0)
              nHCC++;

          InsertCounters(&M->hTable, h, nHCC, k, M->hTable.entries[h][n].
          counters);
          M->hTable.entries[h][n].counters = 0;
          M->hTable.counters[h][k][s]++;
          return;
          }
        else
          {
          small++;
          M->hTable.entries[h][n].counters &= ~(0x03<<(s<<1));
          M->hTable.entries[h][n].counters |= (small<<(s<<1));
          return;
          }
        }

      if(!M->hTable.entries[h][n].counters)
        k++;
      }

    // LIMITING ENTRYSIZE
    if(M->hTable.size[h] == 65535)
      return;

    InsertKey(&M->hTable, h, idx);
    M->hTable.entries[h][M->hTable.size[h]-1].counters = (0x01<<(s<<1));
    }
  else
    {
    aCounters = &M->array.counters[idx << 2];
    aCounters[s]++;
    if(aCounters[s] == M->maxCount && M->maxCount != 0)
      {    
      aCounters[0] >>= 1;
      aCounters[1] >>= 1;
      aCounters[2] >>= 1;
      aCounters[3] >>= 1;
      }
    }
  }

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

CModel *CreateCModel(uint32_t ctx, uint32_t a, uint32_t mc) 
  {
  CModel    *M = (CModel *) Calloc(1, sizeof(CModel));
  uint64_t       prod = 1, *multipliers;
  uint32_t       n;

  if(ctx > MAX_HASH_CTX)
    {
    fprintf(stderr, "Error: context size is greater than %u\n", MAX_HASH_CTX);
    exit(1);
    }

  multipliers    = (uint64_t *) Calloc(ctx, sizeof(uint64_t));
  M->nPModels    = (uint64_t  ) pow(4, ctx);
  M->ctx         = ctx;
  M->alphaDen    = a;
  M->pModelIdx   = 0;

  if(ctx >= HASH_TABLE_BEGIN_CTX)
    {
    M->mode     = HASH_TABLE_MODE;
    M->maxCount = mc >> 8;
    InitHashTable(M);
    }
  else
    {
    M->mode     = ARRAY_MODE;
    M->maxCount = mc;
    InitArray(M);
    }

  for(n = 0 ; n < ctx ; ++n)
    {
    multipliers[n] = prod;
    prod <<= 2;
    }

  M->multiplier = multipliers[M->ctx-1];

  return M;
  }

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ResetCModel(CModel *M)
  {
  M->pModelIdx = 0;
  }

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void GetPModelIdx(uint8_t *p, CModel *M)
  {
  M->pModelIdx = ((M->pModelIdx-*(p-M->ctx)*M->multiplier)<<2)+*p;
  }

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

void ComputePModel(CModel *M, PModel *P)
  {
  if(M->mode == HASH_TABLE_MODE)
    {
    HCCounter *hCounters;
    if(!(hCounters = GetHCCounters(&M->hTable, M->pModelIdx)))
      hCounters = zeroCounters;
    P->freqs[0] = 1 + M->alphaDen * hCounters[0];
    P->freqs[1] = 1 + M->alphaDen * hCounters[1];
    P->freqs[2] = 1 + M->alphaDen * hCounters[2];
    P->freqs[3] = 1 + M->alphaDen * hCounters[3];
    }
  else
    {
    ACCounter *aCounters = &M->array.counters[M->pModelIdx<<2];
    P->freqs[0] = 1 + M->alphaDen * aCounters[0];
    P->freqs[1] = 1 + M->alphaDen * aCounters[1];
    P->freqs[2] = 1 + M->alphaDen * aCounters[2];
    P->freqs[3] = 1 + M->alphaDen * aCounters[3];
    }
  P->sum = P->freqs[0]+P->freqs[1]+P->freqs[2]+P->freqs[3];
  }

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

double PModelSymbolNats(PModel *P, uint32_t s)
  {
  return log((double) P->sum / P->freqs[s]);
  }

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
