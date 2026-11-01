// Mutate the blob, and if Load() accepts it, USE it. Under ASan+UBSan, with a hang watchdog.
#include "common.h"
#include "zindex.h"
#include <signal.h>
#include <sys/mman.h>
#include <fcntl.h>
static void OnAlarm(int){ fputs("HANG: a corrupt blob that Load() accepted made matching spin\n", stderr); _exit(3); }
int main(int argc,char**argv){
  std::vector<std::string> files(argv+1,argv+argc); std::string text=Combined(files); auto corpus=MakeCorpus(text,400);
  std::vector<uint8_t> blob; { std::ifstream f(getenv("ZIDX_BLOB")?getenv("ZIDX_BLOB"):"zidx.bin",std::ios::binary); blob.assign(std::istreambuf_iterator<char>(f),{}); }
  signal(SIGALRM,OnAlarm); std::mt19937_64 rng(2026); size_t accepted=0, rejected=0, used=0; const int N=getenv("ZIDX_N")?atoi(getenv("ZIDX_N")):3000;
  for(int it=0; it<N; ++it){
    auto c=blob; int kind=rng()%4;
    if(kind==0){ for(int k=0;k<1+(int)(rng()%16);++k) c[rng()%c.size()]^=1<<(rng()%8); }                       // random bit flips anywhere
    else if(kind==1){ size_t hdr=sizeof(zidx::Header); for(int k=0;k<4;++k) c[rng()%hdr]^=1<<(rng()%8); }       // flips in the header (offsets/sizes)
    else if(kind==2){ size_t off=rng()%(c.size()-64); for(int k=0;k<64;++k) c[off+k]=(uint8_t)rng(); }          // a 64-byte run of garbage
    else { c.resize(c.size()-(1+rng()%4096)); }                                                                // truncation
    // Load() must not read beyond `c`: copy into an exactly-sized heap block so ASan can see an overrun
    uint8_t* exact=(uint8_t*)aligned_alloc(16, (c.size()+15)&~15ull); memcpy(exact,c.data(),c.size());
    zidx::Index idx; alarm(5); bool ok=idx.Load(exact,c.size()); 
    if(ok){ ++accepted; for(auto&r:corpus){ GURL u(r.url),i(r.initiator); (void)idx.ShouldBlock(u,i,(zephyrus_adblock::ResourceType)r.type); } (void)idx.DocumentExceptions(GURL("https://example.com/")); ++used; } else ++rejected;
    alarm(0); free(exact);
  }
  printf("%d mutated blobs: %zu rejected by Load(), %zu accepted and then used for %zu matches each: no crash, no UB, no hang\n", N, rejected, accepted, corpus.size());
}
