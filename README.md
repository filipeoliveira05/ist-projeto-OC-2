# Organização de Computadores - Laboratório 2: Simulador de Cache TLB (Translation Lookaside Buffer)

Este repositório contém o código e os recursos desenvolvidos para o **2º Trabalho de Laboratório** da unidade curricular de **Organização de Computadores (LEIC)** no Instituto Superior Técnico (IST).

## 📋 Descrição do Projeto

A TLB é um componente de hardware fundamental em arquiteturas modernas que utilizam memória virtual. Ela atua como uma cache rápida para as traduções de endereços virtuais para físicos, evitando o acesso frequente e custoso à *Page Table* na memória principal.

O simulador desenvolvido implementa uma hierarquia de TLB com as seguintes características:

* **Dois Níveis (L1 e L2)**: Uma L1 pequena e muito rápida, e uma L2 maior.
* **Associatividade Total (Fully Associative)**: Qualquer página virtual pode ser mapeada em qualquer entrada da TLB.
* **Política de Substituição LRU (Least Recently Used)**: Quando a cache está cheia, a entrada menos recentemente utilizada é substituída.
* **Política de Escrita Write-Back**: As modificações (*dirty bit*) são propagadas ou escritas em memória apenas quando necessário (na evicção).

---

## 🚀 Funcionalidades Implementadas

A implementação principal encontra-se no ficheiro `src/tlb.c`. As principais funcionalidades desenvolvidas incluem:

1. **Inicialização (`tlb_init`)**:
   * Configuração das estruturas de dados para a TLB L1 e L2.
   * Reset de contadores de performance (hits, misses, invalidations).

2. **Tradução de Endereços (`tlb_translate`)**:
   * **Lookup L1**: Verifica se o endereço está na L1. Em caso de *hit*, atualiza a LRU e o estado *dirty*.
   * **Lookup L2**: Se falhar na L1, verifica a L2. Em caso de *hit* na L2, a entrada é promovida para a L1.
   * **Page Table Walk**: Se falhar em ambas (L1 e L2 Miss), o endereço é traduzido pela *Page Table* e inserido nas caches TLB, possivelmente causando evicções.

3. **Invalidação (`tlb_invalidate`)**:
   * Mecanismo para invalidar entradas específicas na TLB (por exemplo, devido a mudanças de contexto ou *page faults*), garantindo a coerência entre L1 e L2.

4. **Gestão de LRU e Evicção**:
   * Implementação da lógica para encontrar a entrada mais antiga (*Least Recently Used*) para substituição.
   * Gestão correta da promoção de entradas entre L2 e L1.
