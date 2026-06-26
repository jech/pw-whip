const int opusPayload = 111;

class WhipClient {
private:
    std::string token;
    bool insecure;
    std::mutex mu;
    bool closed;
    rtc::PeerConnection pc;
    std::shared_ptr<rtc::Track> audioTrack;
    std::string location;
    uint32_t ssrc;
public:
    WhipClient(std::string token, bool insecure) {
        this->token = token;
        this->insecure = insecure;
        this->closed = true;
    }

    ~WhipClient() {
        if(!this->closed)
            this->close();
    }

    void connect(std::string endpoint);
    void close();
    void sendAudio(const unsigned char *data, size_t count, uint32_t timestamp);
};
