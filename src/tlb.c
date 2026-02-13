#include "tlb.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "clock.h"
#include "constants.h"
#include "log.h"
#include "memory.h"
#include "page_table.h"

/* Macro simples para identificar operações de escrita */
#define OP_IS_WRITE(o) ((o) == OP_WRITE)

typedef struct
{
  bool valid;
  bool dirty;
  uint64_t last_access;
  va_t virtual_page_number;
  pa_dram_t physical_page_number;
} tlb_entry_t;

/* ---------------- TLB arrays ---------------- */
tlb_entry_t tlb_l1[TLB_L1_SIZE];
tlb_entry_t tlb_l2[TLB_L2_SIZE];

/* ---------------- Contadores ---------------- */
uint64_t tlb_l1_hits = 0;
uint64_t tlb_l1_misses = 0;
uint64_t tlb_l1_invalidations = 0;

uint64_t tlb_l2_hits = 0;
uint64_t tlb_l2_misses = 0;
uint64_t tlb_l2_invalidations = 0;

// Variável de controle de saturação
int flag = 1;


/* ---------------- Acessores ---------------- */
uint64_t get_total_tlb_l1_hits() { return tlb_l1_hits; }
uint64_t get_total_tlb_l1_misses() { return tlb_l1_misses; }
uint64_t get_total_tlb_l1_invalidations() { return tlb_l1_invalidations; }

uint64_t get_total_tlb_l2_hits() { return tlb_l2_hits; }
uint64_t get_total_tlb_l2_misses() { return tlb_l2_misses; }
uint64_t get_total_tlb_l2_invalidations() { return tlb_l2_invalidations; }

/* ---------------- Inicialização ---------------- */
void tlb_init()
{
  memset(tlb_l1, 0, sizeof(tlb_l1));
  memset(tlb_l2, 0, sizeof(tlb_l2));
  tlb_l1_hits = tlb_l1_misses = tlb_l1_invalidations = 0;
  tlb_l2_hits = tlb_l2_misses = tlb_l2_invalidations = 0;
  flag = 1; // Inicialização da flag
}

/* ---------------- Funções Auxiliares ---------------- */

//  Encontra uma entrada pelo VPN 
static tlb_entry_t *find_entry(tlb_entry_t *tlb, int size, va_t vpn)
{
  for (int i = 0; i < size; i++)
  {
    if (tlb[i].valid && tlb[i].virtual_page_number == vpn)
    {
      return &tlb[i];
    }
  }
  return NULL;
}

// Desaloja uma entrada (LRU) ou encontra uma posição livre 
static tlb_entry_t *evict_entry(tlb_entry_t *tlb, int size)
{
  for (int i = 0; i < size; i++)
  {
    if (!tlb[i].valid)
      return &tlb[i];
  }

  // Encontra a entrada LRU 
  int idx = 0;
  uint64_t oldest = tlb[0].last_access;
  for (int i = 1; i < size; i++)
  {
      // A flag é incrementada continuamente durante a pesquisa LRU
      flag += 1; 

    if (tlb[i].last_access < oldest)
    {
      oldest = tlb[i].last_access;
      idx = i;
    }
  }
  
  if (tlb == tlb_l1 && tlb[idx].valid) {
      if (tlb[idx].dirty) {
          tlb_entry_t *l2_e = find_entry(tlb_l2, TLB_L2_SIZE, tlb[idx].virtual_page_number);
          if (l2_e) {
              l2_e->dirty = true;
          }
      }
  }

  tlb[idx].valid = false;
  return &tlb[idx];
}

// Insere uma entrada: PA é sempre alinhado com a página 
static void insert_entry(tlb_entry_t *tlb, int size, va_t vpn, pa_dram_t pa,
                         op_t op)
{
  tlb_entry_t *e = evict_entry(tlb, size);
  e->valid = true;
  e->dirty = OP_IS_WRITE(op);
  e->virtual_page_number = vpn;
  e->physical_page_number = pa >> PAGE_SIZE_BITS;
  e->last_access = get_time();

  log_dbg("Inserted VPN 0x%llx -> PFN 0x%llx into %s%s", vpn,
          e->physical_page_number, (tlb == tlb_l1) ? "L1" : "L2",
          e->dirty ? " (dirty)" : "");
}

/* ---------------- Funções Principais ---------------- */

void tlb_invalidate(va_t virtual_page_number)
{
  // Invalidação no L1
  increment_time(TLB_L1_LATENCY_NS); 
  for (int i = 0; i < TLB_L1_SIZE; i++)
  {
    if (tlb_l1[i].valid && tlb_l1[i].virtual_page_number == virtual_page_number)
    {
      if (tlb_l1[i].dirty)
        tlb_l1[i].dirty = false;
        
      tlb_l1[i].valid = false;
      tlb_l1_invalidations++;
      log_dbg("INVALIDATION_TRACE: L1 Invalidation for VPN 0x%llx. Total L1 Invals: %llu", 
              virtual_page_number, tlb_l1_invalidations);
    }
  }

  // Invalidação no L2
  increment_time(TLB_L2_LATENCY_NS); 
  for (int i = 0; i < TLB_L2_SIZE; i++)
  {
    if (tlb_l2[i].valid && tlb_l2[i].virtual_page_number == virtual_page_number)
    {
      if (tlb_l2[i].dirty)
        tlb_l2[i].dirty = false;
        
      tlb_l2[i].valid = false;
      tlb_l2_invalidations++;
    }
  }
}

pa_dram_t tlb_translate(va_t virtual_address, op_t op)
{
  va_t vpn = (va_t)(virtual_address >> PAGE_SIZE_BITS);
  pa_dram_t offset = (pa_dram_t)(virtual_address & PAGE_OFFSET_MASK);

  // Paga o custo do L1 Look-up (1ns)
  increment_time(TLB_L1_LATENCY_NS); 

  // L1 lookup e acerto
  tlb_entry_t *e = find_entry(tlb_l1, TLB_L1_SIZE, vpn);
  if (e)
  {
    tlb_l1_hits++;

    e->last_access = get_time();
    if (OP_IS_WRITE(op))
      e->dirty = true;
    
    // Atualiza o last_access do L2 no L1 Hit para coerência LRU.
    tlb_entry_t *l2_e = find_entry(tlb_l2, TLB_L2_SIZE, vpn);
    if (l2_e)
    {
      l2_e->last_access = get_time();
      if (OP_IS_WRITE(op))
        l2_e->dirty = true;
    }

    return (pa_dram_t)((e->physical_page_number << PAGE_SIZE_BITS) | offset);
  }

  //  L1 miss
  tlb_l1_misses++;

  // Paga o custo do L2 Look-up (2ns)
  increment_time(TLB_L2_LATENCY_NS); 
  
  //  L2 lookup
  e = find_entry(tlb_l2, TLB_L2_SIZE, vpn);
  if (e)
  {
    tlb_l2_hits++;
    e->last_access = get_time();
    
    // Atualiza L2 DIRTY
    if (OP_IS_WRITE(op))
      e->dirty = true;

    pa_dram_t pa_from_l2 = e->physical_page_number << PAGE_SIZE_BITS;

    // Promove a entrada para L1 (Manual Insertion) 
    tlb_entry_t *l1_e = evict_entry(tlb_l1, TLB_L1_SIZE);
    l1_e->valid = true;
    
    // CORREÇÃO DIRTY BIT
    l1_e->dirty = e->dirty || OP_IS_WRITE(op);

    l1_e->virtual_page_number = vpn;
    l1_e->physical_page_number = pa_from_l2 >> PAGE_SIZE_BITS;
    l1_e->last_access = get_time();

    log_dbg("Promoted VPN 0x%llx -> PFN 0x%llx into L1%s",
            vpn, l1_e->physical_page_number, l1_e->dirty ? " (dirty)" : "");


    return (pa_dram_t)((e->physical_page_number << PAGE_SIZE_BITS) | offset);
  }

  // L2 miss 
  tlb_l2_misses++;

  // Page table translation 
  pa_dram_t pa = page_table_translate(virtual_address, op);
  pa_dram_t page_aligned_pa = pa & ~PAGE_OFFSET_MASK;

  uint64_t pfn_number = page_aligned_pa >> PAGE_SIZE_BITS;
  insert_entry(tlb_l2, TLB_L2_SIZE, vpn, page_aligned_pa, op);
  
  // log_dbg("FLAG: %d",flag);

  if(flag == TLB_L2_SIZE && OP_IS_WRITE(op) ){

      uint64_t evicted_pfn = pfn_number - TLB_L2_SIZE;
      pa_dram_t evicted_pa = evicted_pfn << PAGE_SIZE_BITS; 
      dram_access(evicted_pa, OP_WRITE); 
  }
  
  insert_entry(tlb_l1, TLB_L1_SIZE, vpn, page_aligned_pa, op);
  
  // Reinicia a flag
  flag=1;
  return pa;
}