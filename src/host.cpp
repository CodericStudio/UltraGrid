/*
 * This file contains common external definitions
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#include "config_unix.h"
#include "config_win32.h"
#endif

#include "host.h"

#include "audio/audio_capture.h"
#include "audio/audio_playback.h"
#include "lib_common.h"
#include "messaging.h"
#include "video_capture.h"
#include "video_compress.h"
#include "video_display.h"
#include "video.h"
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>

using namespace std;

unsigned int cuda_device = 0;
unsigned int audio_capture_channels = DEFAULT_AUDIO_CAPTURE_CHANNELS;
unsigned int audio_capture_bps = 0;
unsigned int audio_capture_sample_rate = 0;

unsigned int cuda_devices[MAX_CUDA_DEVICES] = { 0 };
unsigned int cuda_devices_count = 1;

int audio_init_state_ok;

uint32_t RTT = 0;               /*  this is computed by handle_rr in rtp_callback */

int uv_argc;
char **uv_argv;

char *export_dir = NULL;
volatile bool should_exit_receiver = false;

volatile int log_level = LOG_LEVEL_INFO;
bool color_term = (getenv("TERM") && set<string>{"linux", "screen", "xterm", "xterm-256color"}.count(getenv("TERM")) > 0) && isatty(1) && isatty(2);

bool ldgm_device_gpu = false;

const char *window_title = NULL;
#include <sstream>

void print_capabilities(struct module *root, bool use_vidcap)
{
        // try to figure out actual input video format
        struct video_desc desc{};
        if (use_vidcap && root) {
                for (int attempt = 0; attempt < 2; ++attempt) {
                        struct msg_sender *m = (struct msg_sender *) new_message(sizeof(struct msg_sender));
                        m->type = SENDER_MSG_QUERY_VIDEO_MODE;
                        struct response *r = send_message_sync(root, "sender", (struct message *) m, 1000);
                        if (response_get_status(r) == RESPONSE_OK) {
                                const char *text = response_get_text(r);
                                istringstream iss(text);
                                iss >> desc;
                                free_response(r);
                                break;
                        }
                        free_response(r);
                        sleep(1);
                }
        }

        // compressions
        cout << "[cap] Compressions:" << endl;
        auto compressions = get_libraries_for_class(LIBRARY_CLASS_VIDEO_COMPRESS, VIDEO_COMPRESS_ABI_VERSION);

        for (auto it : compressions) {
                auto vci = static_cast<const struct video_compress_info *>(it.second);
                auto presets = vci->get_presets();
                for (auto const & it : presets) {
                        cout << "[cap] (" << vci->name << (it.name.empty() ? "" : ":") <<
                                it.name << ";" << it.quality << ";" << setiosflags(ios_base::fixed) << setprecision(2) << it.compute_bitrate(&desc) << ";" <<
                                it.enc_prop.latency << ";" << it.enc_prop.cpu_cores << ";" << it.enc_prop.gpu_gflops << ";" <<
                                it.dec_prop.latency << ";" << it.dec_prop.cpu_cores << ";" << it.dec_prop.gpu_gflops <<
                                ")\n";
                }
        }

        // capturers
        cout << "[cap] Capturers:" << endl;
        print_available_capturers();

        // displays
        cout << "[cap] Displays:" << endl;
        auto const & display_capabilities =
                get_libraries_for_class(LIBRARY_CLASS_VIDEO_DISPLAY, VIDEO_DISPLAY_ABI_VERSION);
        for (auto const & it : display_capabilities) {
                auto vdi = static_cast<const struct video_display_info *>(it.second);
                int count;
                struct display_card *devices;
                vdi->probe(&devices, &count);
                for (int i = 0; i < count; ++i) {
                        cout << "[cap] (" << devices[i].id << ";" << devices[i].name << ";" <<
                                devices[i].repeatable << ")\n";
                }
                free(devices);
        }

        cout << "[cap] Audio capturers:" << endl;
        auto const & audio_cap_capabilities =
                get_libraries_for_class(LIBRARY_CLASS_AUDIO_CAPTURE, AUDIO_CAPTURE_ABI_VERSION);
        for (auto const & it : audio_cap_capabilities) {
                auto aci = static_cast<const struct audio_capture_info *>(it.second);
                int count;
                struct device_info *devices;
                aci->probe(&devices, &count);
                for (int i = 0; i < count; ++i) {
                        cout << "[cap] (" << devices[i].id << ";" << devices[i].name << ")\n";
                }
                free(devices);
        }

        cout << "[cap] Audio playback:" << endl;
        auto const & audio_play_capabilities =
                get_libraries_for_class(LIBRARY_CLASS_AUDIO_PLAYBACK, AUDIO_PLAYBACK_ABI_VERSION);
        for (auto const & it : audio_play_capabilities) {
                auto api = static_cast<const struct audio_playback_info *>(it.second);
                int count;
                struct device_info *devices;
                api->probe(&devices, &count);
                for (int i = 0; i < count; ++i) {
                        cout << "[cap] (" << devices[i].id << ";" << devices[i].name << ")\n";
                }
                free(devices);
        }
}

