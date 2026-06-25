#include <iostream>
#include <cstring>
#include <random>

#include <rtc/rtc.hpp>
#include <curl/curl.h>

#include "whip.hpp"

static size_t write_cb(char *data, size_t size, size_t nmemb, void *closure)
{
    auto buf = static_cast<std::string *>(closure);
    size_t real_size = size * nmemb;
    buf->append(data, real_size);
    return real_size;
}

void WhipClient::connect(std::string endpoint) {
    std::mt19937 rng(std::random_device{}());
    auto ssrc = std::uniform_int_distribution<std::uint32_t>{}(rng);

    std::unique_lock lk(this->mu);
    this->ssrc = ssrc;
    rtc::Description::Audio
        media("audio", rtc::Description::Direction::SendOnly);
    media.addOpusCodec(opusPayload);
    std::string cname("audio");
    media.addSSRC(this->ssrc, cname);
    this->audioTrack = this->pc.addTrack(media);
    auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, cname, opusPayload, rtc::OpusRtpPacketizer::DefaultClockRate);
    auto packetizer = std::make_shared<rtc::OpusRtpPacketizer>(rtpConfig);
    auto srReporter = std::make_shared<rtc::RtcpSrReporter>(rtpConfig);
    packetizer->addToChain(srReporter);
    this->audioTrack->setMediaHandler(packetizer);

    this->pc.setLocalDescription();

    auto offer_sdp = std::string(this->pc.localDescription().value());

    struct curl_slist *headers = NULL;
    if(this->token.length() > 0) {
        auto h = std::string("Authorization: bearer ") + token;
        headers = curl_slist_append(headers, h.c_str());
    }
    headers = curl_slist_append(headers, "Content-Type: application/sdp");
    char error_buffer[CURL_ERROR_SIZE] = {};

    std::string answer_sdp;

    CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, offer_sdp.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &answer_sdp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_UNRESTRICTED_AUTH, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
    if(this->insecure) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    auto res = curl_easy_perform(curl);

    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        this->pc.close();
        curl_easy_cleanup(curl);
        throw std::runtime_error(error_buffer[0] ? error_buffer : curl_easy_strerror(res));
    }

    long rc = 666;
    res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &rc);
    if(res != CURLE_OK || rc / 100 != 2) {
        this->pc.close();
        curl_easy_cleanup(curl);
        throw std::runtime_error("unexpected result code");
    }

    const char *ct;
    res = curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct);
    if(res != CURLE_OK || strcasecmp(ct, "application/sdp") != 0) {
        this->pc.close();
        curl_easy_cleanup(curl);
        throw std::runtime_error("unexpected content type");
    }

    const char *location;
    res = curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &location);
    if(res != CURLE_OK) {
        this->pc.close();
        curl_easy_cleanup(curl);
        throw std::runtime_error("couldn't get location header");
    }
    this->location = std::string(location);

    curl_easy_cleanup(curl);

    rtc::Description answer(answer_sdp, "answer");
    this->pc.setRemoteDescription(answer);
    this->closed = false;
}

void WhipClient::close() {
    std::string location;

    std::unique_lock lk(this->mu);
    if(this->closed)
        return;
    location = this->location;
    this->pc.close();
    this->closed = true;
    lk.unlock();

    struct curl_slist *headers = NULL;
    if(this->token.length() > 0) {
        auto h = std::string("Authorization: bearer ") + token;
        headers = curl_slist_append(headers, h.c_str());
    }
    char error_buffer[CURL_ERROR_SIZE] = {};
    CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, location.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_UNRESTRICTED_AUTH, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
    if(this->insecure) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    auto res = curl_easy_perform(curl);

    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        curl_easy_cleanup(curl);
        throw std::runtime_error(error_buffer[0] ? error_buffer : curl_easy_strerror(res));
    }
    curl_easy_cleanup(curl);
}

void WhipClient::sendAudio(const unsigned char *data, size_t count, uint32_t timestamp) {
    std::unique_lock lk(this->mu);
    if(this->closed)
        return;
    auto track = this->audioTrack;
    if(!track->isOpen())
        return;

    lk.unlock();

    track->sendFrame(reinterpret_cast<const std::byte *>(data), count,
                     rtc::FrameInfo(timestamp));
}
