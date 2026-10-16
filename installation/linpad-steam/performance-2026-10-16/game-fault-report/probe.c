#define _GNU_SOURCE
#include <signal.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
static sigjmp_buf jump;
static volatile sig_atomic_t seen;
static void caught(int n,siginfo_t *i,void *c) { (void)c;if(n==SIGSEGV && i->si_addr==0)seen++;siglongjmp(jump,1); }
static void raised(int n) { if(n==SIGILL)seen++; }
__attribute__((noinline)) static void fault(void) { __asm__ volatile("xor %%rax,%%rax; mov (%%rax),%%rax":::"rax","memory"); }
int main(int argc,char **argv) {
 struct sigaction sa={.sa_sigaction=caught,.sa_flags=SA_SIGINFO},old,current;
 sigemptyset(&sa.sa_mask);
 if(sigaction(SIGSEGV,&sa,&old) || old.sa_handler!=SIG_DFL)return 2;
 if(sigaction(SIGSEGV,0,&current) || current.sa_sigaction!=caught)return 3;
 if(sigsetjmp(jump,1)==0)fault();
 if(seen!=1)return 4;
 if(signal(SIGILL,raised)==SIG_ERR || raise(SIGILL) || seen!=2)return 5;
 if(signal(SIGILL,SIG_IGN)!=raised)return 6;
 puts("PASS reporter chains handlers installed after constructor and preserves action queries"); fflush(stdout);
 if(argc>1 && !strcmp(argv[1],"fatal")) { signal(SIGSEGV,SIG_DFL); fault(); return 7; }
 return 0;
}
