#!/usr/bin/env python3
# Passt llama.cpp fuer Genode an: dynamisches Laden von Backends (std::filesystem) entfaellt.
import sys, re
p = sys.argv[1] + "/ggml/src/ggml-backend-reg.cpp"
s = open(p).read()
k = s.index("static ggml_backend_reg_t ggml_backend_load_best(")
b = s.index("{", s.index(")", k))
d = 0
i = b
while True:
    if s[i] == "{": d += 1
    elif s[i] == "}":
        d -= 1
        if d == 0: break
    i += 1
s = s[:b] + "{ (void)name; (void)silent; (void)user_search_path; return nullptr; }" + s[i+1:]
open(p, "w").write(s)
print("gepatcht:", p)
