CC ?= cc
CFLAGS ?= -O2
override CFLAGS += -std=c11 -Wall -Wextra -D_POSIX_C_SOURCE=200809L

CORE = zynex_rules.o zynex_visemes.o zynex_model.o zynex_mem.o
WEB = zynex_web.o zynex_web_html.o
GFX = zynex_gfx.o zynex_font.o zynex_face.o zynex_ui.o zynex_fb.o
OBJS = $(CORE)

# Mit echtem Sprachmodell: make LLAMA=1 LLAMA_CFLAGS="-I..." LLAMA_LIBS="-L... -lllama -lggml ..."
ifdef LLAMA
LLM = zynex_llm.o
LINK = $(CXX)
else
LLM = zynex_llm_stub.o
LINK = $(CC)
endif

all: zynexd

zynexd: main.o $(CORE) $(GFX) $(WEB) $(LLM)
	$(LINK) $(LDFLAGS) -o $@ $^ $(LLAMA_LIBS) -lm -lpthread

tests/test_core: tests/test_core.o $(CORE) $(GFX)
	$(CC) $(LDFLAGS) -o $@ $^ -lm

zynex_llm.o: zynex_llm.c
	$(CC) $(CFLAGS) $(LLAMA_CFLAGS) -c -o $@ $<

tests/test_core.o: tests/test_core.c
	$(CC) $(CFLAGS) -I. -c -o $@ $<

test: tests/test_core
	./tests/test_core

clean:
	rm -f *.o tests/*.o zynexd tests/test_core

.PHONY: all test clean
