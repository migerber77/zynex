#!/usr/bin/env python3
"""Baut ein winziges Qwen2-aehnliches GGUF (Zufallsgewichte, Q8_0) und rechnet die Referenz mit numpy.
Vergleicht den C-Motor (tools/llm_host) mit der Referenz: Logits und BPE-Tokenizer."""
import struct, subprocess, sys, numpy as np
rng = np.random.default_rng(7)
DIM, FFN, NH, NKV, NL, CTX = 64, 128, 4, 2, 2, 128
HD = DIM // NH; BASE = 10000.0; EPS = 1e-6
# --- Vokabular (Byte-Level, GPT-2-Zuordnung) ---
bs = list(range(33, 127)) + list(range(161, 173)) + list(range(174, 256)); cs = bs[:]; n = 0
for b in range(256):
    if b not in bs: bs.append(b); cs.append(256 + n); n += 1
b2c = {b: chr(c) for b, c in zip(bs, cs)}
c2b = {v: k for k, v in b2c.items()}
tokens = [b2c[b] for b in range(256)]
merges = []
def mk(a, b):
    merges.append(a + " " + b); tokens.append(a + b)
h, e, l, o, sp, t = b2c[ord('h')], b2c[ord('e')], b2c[ord('l')], b2c[ord('o')], b2c[32], b2c[ord('t')]
mk(h, e); mk(l, l); mk(h + e, l + l); mk(h + e + l + l, o); mk(sp, t); mk(sp + t, e)
tokens += ["<|im_start|>", "<|im_end|>", "<|endoftext|>"]
V = len(tokens)
# --- Gewichte ---
def q8(w):                      # w: (out,in) -> Bytes und dequantisiert
    out, inn = w.shape; blocks = w.reshape(out, inn // 32, 32)
    amax = np.abs(blocks).max(-1); d = (amax / 127).astype(np.float16); df = d.astype(np.float32)
    q = np.where(df[..., None] != 0, np.round(blocks / np.where(df[..., None] == 0, 1, df[..., None])), 0).clip(-127, 127).astype(np.int8)
    raw = b"".join(d[i, j].tobytes() + q[i, j].tobytes() for i in range(out) for j in range(inn // 32))
    return raw, (q.astype(np.float32) * df[..., None]).reshape(out, inn)
W = {}; T = []   # (name, dims(ne), type, bytes)
def add_q(name, shape):
    w = rng.normal(0, 0.08, shape).astype(np.float32); raw, dq = q8(w); W[name] = dq; T.append((name, [shape[1], shape[0]], 8, raw))
def add_f(name, v):
    v = v.astype(np.float32); W[name] = v; T.append((name, [len(v)], 0, v.tobytes()))
add_q("token_embd.weight", (V, DIM))
for L in range(NL):
    add_f(f"blk.{L}.attn_norm.weight", 1 + rng.normal(0, .1, DIM)); add_q(f"blk.{L}.attn_q.weight", (DIM, DIM)); add_f(f"blk.{L}.attn_q.bias", rng.normal(0, .05, DIM))
    add_q(f"blk.{L}.attn_k.weight", (NKV * HD, DIM)); add_f(f"blk.{L}.attn_k.bias", rng.normal(0, .05, NKV * HD))
    add_q(f"blk.{L}.attn_v.weight", (NKV * HD, DIM)); add_f(f"blk.{L}.attn_v.bias", rng.normal(0, .05, NKV * HD))
    add_q(f"blk.{L}.attn_output.weight", (DIM, DIM)); add_f(f"blk.{L}.ffn_norm.weight", 1 + rng.normal(0, .1, DIM))
    add_q(f"blk.{L}.ffn_gate.weight", (FFN, DIM)); add_q(f"blk.{L}.ffn_up.weight", (FFN, DIM)); add_q(f"blk.{L}.ffn_down.weight", (DIM, FFN))
add_f("output_norm.weight", 1 + rng.normal(0, .1, DIM))
# --- GGUF schreiben ---
def s(x): b = x.encode(); return struct.pack("<Q", len(b)) + b
def kv(k, ty, v):
    r = s(k) + struct.pack("<I", ty)
    if ty == 4: r += struct.pack("<I", v)
    elif ty == 6: r += struct.pack("<f", v)
    elif ty == 8: r += s(v)
    elif ty == 9: r += struct.pack("<IQ", 8, len(v)) + b"".join(s(x) for x in v)
    return r
meta = [kv("general.architecture", 8, "qwen2"), kv("qwen2.block_count", 4, NL), kv("qwen2.embedding_length", 4, DIM), kv("qwen2.feed_forward_length", 4, FFN),
        kv("qwen2.attention.head_count", 4, NH), kv("qwen2.attention.head_count_kv", 4, NKV), kv("qwen2.attention.layer_norm_rms_epsilon", 6, EPS),
        kv("qwen2.rope.freq_base", 6, BASE), kv("qwen2.context_length", 4, CTX), kv("tokenizer.ggml.tokens", 9, tokens), kv("tokenizer.ggml.merges", 9, merges),
        kv("tokenizer.ggml.eos_token_id", 4, V - 1)]
info = b""; off = 0; datab = b""
for name, ne, ty, raw in T:
    info += s(name) + struct.pack("<I", len(ne)) + b"".join(struct.pack("<Q", x) for x in ne) + struct.pack("<II", ty, 0)[:4] + struct.pack("<Q", off)
    pad = (-len(raw)) % 32; datab += raw + b"\0" * pad; off += len(raw) + pad
head = b"GGUF" + struct.pack("<IQQ", 3, len(T), len(meta)) + b"".join(meta) + info
head += b"\0" * ((-len(head)) % 32)
open("/tmp/tiny.gguf", "wb").write(head + datab)
# --- Referenz ---
def rms(x, g): return x / np.sqrt((x * x).mean() + EPS) * g
def quant(x):
    b = x.reshape(-1, 32); amax = np.abs(b).max(-1); d = amax / 127; q = np.where(d[:, None] != 0, np.round(b / np.where(d[:, None] == 0, 1, d[:, None])), 0)
    q = np.sign(q) * np.floor(np.abs(np.where(d[:, None] != 0, b / np.where(d[:, None] == 0, 1, d[:, None]), 0)) + 0.5)
    return (q * d[:, None]).reshape(-1)
def mv(w, x): return w @ quant(x)
def rope(v, pos):
    out = v.copy().reshape(-1, HD); half = HD // 2
    for i in range(half):
        f = BASE ** (-2.0 * i / HD); a = pos * f; c, s_ = np.cos(a), np.sin(a)
        x0, x1 = out[:, i].copy(), out[:, i + half].copy(); out[:, i] = x0 * c - x1 * s_; out[:, i + half] = x0 * s_ + x1 * c
    return out.reshape(-1)
def forward(toks):
    kc = [[] for _ in range(NL)]; vc = [[] for _ in range(NL)]
    for pos, tk in enumerate(toks):
        x = W["token_embd.weight"][tk].copy()
        for L in range(NL):
            p = f"blk.{L}."; xb = rms(x, W[p + "attn_norm.weight"])
            q = mv(W[p + "attn_q.weight"], xb) + W[p + "attn_q.bias"]; k = mv(W[p + "attn_k.weight"], xb) + W[p + "attn_k.bias"]; v = mv(W[p + "attn_v.weight"], xb) + W[p + "attn_v.bias"]
            q = rope(q, pos); k = rope(k, pos); kc[L].append(k); vc[L].append(v); out = np.zeros(DIM)
            for hh in range(NH):
                kvh = hh // (NH // NKV); qh = q[hh * HD:(hh + 1) * HD]
                sc = np.array([qh @ kk[kvh * HD:(kvh + 1) * HD] for kk in kc[L]]) / np.sqrt(HD); sc = np.exp(sc - sc.max()); sc /= sc.sum()
                out[hh * HD:(hh + 1) * HD] = sum(w * vv[kvh * HD:(kvh + 1) * HD] for w, vv in zip(sc, vc[L]))
            x = x + mv(W[p + "attn_output.weight"], out)
            xb = rms(x, W[p + "ffn_norm.weight"]); g = mv(W[p + "ffn_gate.weight"], xb); u = mv(W[p + "ffn_up.weight"], xb)
            x = x + mv(W[p + "ffn_down.weight"], g / (1 + np.exp(-g)) * u)
        xb = rms(x, W["output_norm.weight"])
    return W["token_embd.weight"] @ quant(xb)
toks = [3, 70, 104, 200, 15, 259]
ref = forward(toks)
host = sys.argv[1]
r = subprocess.run([host, "/tmp/tiny.gguf", "logits"] + [str(t) for t in toks], capture_output=True, text=True)
vals = np.array([float(x) for x in r.stdout.strip().split("\n")[1:]])
diff = np.abs(vals - ref).max(); print("Logits: max. Abweichung %.2e (Wertebereich %.2f)" % (diff, np.abs(ref).max()))
ok = diff < 2e-3 and vals.argmax() == ref.argmax()
# Tokenizer
def bpe(word):
    syms = [b2c[b] for b in word.encode()]
    while True:
        best = None
        for i in range(len(syms) - 1):
            pr = syms[i] + " " + syms[i + 1]
            if pr in merges and (best is None or merges.index(pr) < best[0]): best = (merges.index(pr), i)
        if best is None: break
        i = best[1]; syms[i:i + 2] = [syms[i] + syms[i + 1]]
    return [tokens.index(x) for x in syms]
for text, pre in (("hello", ["hello"]), ("the hello", ["the", " hello"]), ("hello the", ["hello", " the"])):
    exp = [i for w in pre for i in bpe(w)]
    r = subprocess.run([host, "/tmp/tiny.gguf", "encode", text], capture_output=True, text=True)
    got = [int(x) for x in r.stdout.strip().split("\n")[1].split()]
    print("Tokenizer %-10r %s %s" % (text, got, "OK" if got == exp else "FEHLER erwartet %s" % exp)); ok = ok and got == exp
print("Ergebnis:", "OK" if ok else "FEHLER"); sys.exit(0 if ok else 1)
