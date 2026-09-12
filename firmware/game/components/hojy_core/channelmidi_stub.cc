#include "audio/channelmidi.hh"

namespace hojy::audio {

ChannelMIDI::ChannelMIDI(Mixer *mixer, const std::string &filename) : Channel(mixer, filename)
{
    ok_ = false;
}

ChannelMIDI::~ChannelMIDI() = default;

void ChannelMIDI::load(const std::string &)
{
    ok_ = false;
}

void ChannelMIDI::reset() {}

void ChannelMIDI::setRepeat(bool r)
{
    Channel::setRepeat(r);
}

size_t ChannelMIDI::readPCMData(const void **, size_t, bool)
{
    return 0;
}

}
