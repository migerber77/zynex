// Machbarkeitstest: laeuft llama.cpp auf Genode (libc, Threads, AVX2)?
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <locale>
#include <cstddef>
#include "llama.h"
extern "C" {
#include "zynex_llm.h"
}

// Genode-libc kennt mlock nicht; fuer den Test genuegt ein Stub.
extern "C" int mlock(const void *, size_t) { return 0; }
extern "C" int munlock(const void *, size_t) { return 0; }

// Fehlendes Symbol in Genodes stdcxx (wchar_t-Regex in unicode.cpp)
// std::__cxx11::collate<wchar_t>::id (locale::id ist ein size_t)
asm(".globl _ZNSt7__cxx117collateIwE2idE\n"
    ".bss\n.align 8\n"
    "_ZNSt7__cxx117collateIwE2idE: .zero 16\n"
    ".text\n");

int main(int argc, char **argv)
{
    std::printf("zynex_llmtest: start\n");
    std::atomic<int> summe{0};
    std::thread t1([&] { for (int i = 0; i < 1000; i++) summe++; });
    std::thread t2([&] { for (int i = 0; i < 1000; i++) summe++; });
    t1.join();
    t2.join();
    std::printf("zynex_llmtest: threads ok (%d)\n", summe.load());

    llama_backend_init();
    std::printf("zynex_llmtest: system: %s\n", llama_print_system_info());

    const char *pfad = argc > 1 ? argv[1] : "/modell.gguf";
    if (FILE *f = std::fopen(pfad, "rb")) {
        std::fclose(f);
        char err[200] = "";
        std::printf("zynex_llmtest: lade %s\n", pfad);
        zx_llm *m = zx_llm_open(pfad, 512, 2, err, sizeof(err));
        std::printf("zynex_llmtest: modell %s %s\n", m ? "geladen" : "NICHT geladen", err);
        if (m) {
            double tps = 0;
            int n = zx_llm_antwort(m, "Du bist ein hilfreicher Assistent. Antworte kurz auf Deutsch.",
                                   "Hallo, wer bist du?", 24,
                                   [](const char *st, void *) { std::printf("%s", st); std::fflush(stdout); return 0; },
                                   nullptr, &tps);
            std::printf("\nzynex_llmtest: %d Tokens, %.2f tok/s\n", n, tps);
            zx_llm_close(m);
        }
    } else {
        std::printf("zynex_llmtest: keine Modelldatei %s\n", pfad);
    }
    std::printf("zynex_llmtest: fertig\n");
    return 0;
}
