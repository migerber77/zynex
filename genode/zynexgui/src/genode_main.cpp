// Zynex auf Genode (Spike Schritt 3): Gesicht + Eingabe ueber die Gui-Session.
#include <base/component.h>
#include <base/attached_dataspace.h>
#include <base/log.h>
#include <libc/component.h>
#include <gui_session/connection.h>
#include <input/event.h>
#include <input/keycodes.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <atomic>
#include <string>
#include <vector>
#include <mutex>
#include <thread>

extern "C" {
#include "zynex_ui.h"
#include "zynex_model.h"
#include "zynex_llm.h"
}

// Genode-libc kennt mlock nicht
extern "C" int mlock(const void *, size_t) { return 0; }
extern "C" int munlock(const void *, size_t) { return 0; }
// std::__cxx11::collate<wchar_t>::id fehlt in Genodes stdcxx
asm(".globl _ZNSt7__cxx117collateIwE2idE\n.bss\n.align 8\n_ZNSt7__cxx117collateIwE2idE: .zero 16\n.text\n");

using Genode::Attached_dataspace;
using Genode::Constructible;
using Genode::Codepoint;
namespace Framebuffer = ::Framebuffer;

static uint32_t jetzt_ms()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

struct Zynex_gui
{
	Libc::Env &env;
	Gui::Connection gui { env, "zynex" };

	static constexpr unsigned W = 1024, H = 768;

	Framebuffer::Mode const mode { .area = { W, H }, .alpha = false };

	Constructible<Attached_dataspace> fb_ds { };
	zx_canvas cv { };
	zx_ui ui { };
	Gui::View_id const view_id { 1 };
	int frames = 0;

	/* Sprachmodell (im Hintergrund) */
	zx_llm *llm = nullptr;
	std::atomic<int> llm_status { 0 };   /* 0 laedt, 1 bereit, -1 fehlt */
	std::atomic<bool> rechnet { false };
	std::mutex mx;
	std::string antwort_text;
	bool antwort_da = false;
	bool selftest = true;

	void modell_laden()
	{
		FILE *f = fopen("/modell.gguf", "rb");
		if (!f) { llm_status = -1; Genode::log("zynex: keine Modelldatei"); return; }
		fclose(f);
		char err[200] = "";
		llm = zx_llm_open("/modell.gguf", 512, 3, err, sizeof(err));
		if (!llm) { llm_status = -1; Genode::log("zynex: modell nicht geladen ", (char const *)err); return; }
		llm_status = 1;
		Genode::log("zynex: modell geladen");
	}

	void frage_stellen(std::string frage)
	{
		rechnet = true;
		std::thread([this, frage] {
			std::string out;
			auto cb = [] (const char *st, void *p) { static_cast<std::string *>(p)->append(st); return 0; };
			double tps = 0;
			int n = zx_llm_antwort(llm, zx_model_system(), frage.c_str(), 96, cb, &out, &tps);
			char aktion[64] = "";
			std::vector<char> buf(out.begin(), out.end()); buf.push_back(0);
			zx_model_split(buf.data(), aktion, sizeof(aktion));
			{
				std::lock_guard<std::mutex> l(mx);
				antwort_text = buf.data();
				if (antwort_text.empty()) antwort_text = "Dazu fehlen mir die Worte.";
				antwort_da = true;
			}
			char zeile[160];
			snprintf(zeile, sizeof(zeile), "zynex: antwort %d tokens", n);
			Genode::log((char const *)zeile);
			rechnet = false;
		}).detach();
	}

	Zynex_gui(Libc::Env &env) : env(env)
	{
		Genode::log("zynex: gui-session offen");
		gui.buffer(mode);
		Genode::log("zynex: buffer ok");
		fb_ds.construct(env.rm(), gui.framebuffer.dataspace());
		gui.view(view_id, { });
		using Command = Gui::Session::Command;
		gui.enqueue<Command::Geometry>(view_id, Gui::Rect(Gui::Point(0, 0), mode.area));
		gui.enqueue<Command::Front>(view_id);
		gui.execute();

		cv.w = (int)W;
		cv.h = (int)H;
		cv.px = (uint32_t *)calloc((size_t)W * H, 4);
		zx_ui_init(&ui, jetzt_ms());
		zx_ui_say(&ui, 1, "Hallo, ich bin Zynex auf Genode. Ich wache gerade auf.");
		std::thread([this] { modell_laden(); }).detach();
	}

	void antworte(const char *zeile)
	{
		char aktion[64];
		zx_ui_say(&ui, 0, zeile);
		if (llm_status == 1) {
			if (rechnet) { zx_ui_say(&ui, 1, "Einen Moment, ich denke noch."); return; }
			zx_face_set_mood(&ui.face, ZX_DENKT);
			frage_stellen(zeile);
			return;
		}
		if (zx_model_intent(zeile, aktion, sizeof(aktion))) {
			char t[160];
			snprintf(t, sizeof(t), "Ich wuerde %s tun.", aktion);
			zx_ui_say(&ui, 1, t);
		} else {
			zx_ui_say(&ui, 1, "Das habe ich noch nicht verstanden.");
		}
	}

	void eingabe(Input::Event const &ev)
	{
		ev.handle_press([&] (Input::Keycode key, Codepoint cp) {
			size_t n = strlen(ui.eingabe);
			if (key == Input::KEY_ENTER) {
				char z[ZX_UI_LINE];
				snprintf(z, sizeof(z), "%s", ui.eingabe);
				ui.eingabe[0] = '\0';
				if (z[0]) antworte(z);
			} else if (key == Input::KEY_BACKSPACE) {
				if (n) ui.eingabe[n - 1] = '\0';
			} else if (cp.value >= 32 && cp.value < 127 && n + 1 < sizeof(ui.eingabe)) {
				ui.eingabe[n] = (char)cp.value;
				ui.eingabe[n + 1] = '\0';
			}
		});
	}

	void schleife()
	{
		for (;;) {
			gui.input.for_each_event([&] (Input::Event const &ev) { eingabe(ev); });

			if (llm_status == 1 && selftest) {
				selftest = false;
				zx_ui_say(&ui, 1, "Ich bin wach. Frag mich etwas.");
				zx_ui_say(&ui, 0, "Hallo, wer bist du?");
				frage_stellen("Hallo, wer bist du?");
			}
			{
				std::lock_guard<std::mutex> l(mx);
				if (antwort_da) {
					antwort_da = false;
					zx_ui_say(&ui, 1, antwort_text.c_str());
					zx_face_set_mood(&ui.face, ZX_FREUNDLICH);
				}
			}
			zx_face_update(&ui.face, jetzt_ms());
			zx_ui_draw(&ui, &cv);
			memcpy(fb_ds->local_addr<void>(), cv.px, (size_t)W * H * 4);
			gui.framebuffer.refresh(0, 0, W, H);

			if (++frames == 30)
				Genode::log("zynex: gui ok, frames=", frames);
			usleep(33000);
		}
	}
};

static void *thread_main(void *p)
{
	static_cast<Zynex_gui *>(p)->schleife();
	return nullptr;
}

void Libc::Component::construct(Libc::Env &env)
{
	Genode::log("zynex: start");
	static Zynex_gui gui { env };
	static pthread_t th;
	Libc::with_libc([&] () { pthread_create(&th, nullptr, thread_main, &gui); });
}
