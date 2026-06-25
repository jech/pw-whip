#include <string>
#include <iostream>
#include <thread>
#include <chrono>

#include <rtc/rtc.hpp>
#include <curl/curl.h>
#include <pipewire/pipewire.h>

#include "whip.hpp"
#include "pipewire.h"

static bool debug = false;

extern "C" void
sendit(const unsigned char* data, size_t count, uint32_t timestamp, void *closure)
{
    auto client = (WhipClient *)closure;
    try {
        client->sendAudio(data, count, timestamp);
    } catch(std::runtime_error &e) {
        if(debug)
            std::cerr << "Send RTP: " << e.what() << "\n";
        return;
    }
}

void usage(std::string progname) {
    std::cerr <<
        "Usage: " <<
        progname <<
        " [-k] [-t token] [-c] [-C target] [-b bitrate] [-d] endpoint\n";
}

int main(int argc, char **argv)
{

    bool insecure = false;
    std::string token;
    const char *target = NULL;
    int bitrate = 0;


    pw_init(&argc, &argv);

    int opt;
    while((opt = getopt(argc, argv, "kt:cC:b:d")) != -1) {
        switch(opt) {
        case 'k':
            insecure = true;
            break;
        case 't':
            token = optarg;
            break;
        case 'c':
            target = "";
            break;
        case 'C':
            target = optarg;
            break;
        case 'b':
            bitrate = atoi(optarg);
            break;
        case 'd':
            debug = true;
            break;
        default:
            usage(argv[0]);
            exit(1);
        }
    }

    if(argc != optind + 1) {
        usage(argv[0]);
        exit(1);
    }

    std::string endpoint(argv[optind]);

    if(debug) {
        pw_log_set_level(SPA_LOG_LEVEL_DEBUG);
        rtc::InitLogger(rtc::LogLevel::Debug);
    }

    void *pw_closure = pw_setup(target, bitrate);

    WhipClient client(token, insecure);
    client.connect(endpoint);

    pw_run(pw_closure, sendit, (void*)&client);

    client.close();

    pw_cleanup(pw_closure);

    return 0;
}
