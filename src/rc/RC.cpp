#include "RC.hpp"
#include "Common.hpp"
#include "Board.hpp"
#include "SRT/SRT.hpp"
#include "I-Bus.hpp"
#include "S-Bus.hpp"
#include <algorithm>
#include <cmath>

namespace RC
{
    float _channels[maxChannelCount];
    bool _channelInDZ[maxChannelCount];
    uint8_t _channelsCount = 0;

    State _state = State::signal_lose;
    uint8_t _rssi = UINT8_MAX;
    uint32_t _lastValidTimestamp;

    //=================================================

    uint32_t signalLoseTimeout = 1'000;

    int32_t _channelsAssign[static_cast<int>(ChannelFunction::__end)] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    float _minChannelValue[maxChannelCount] = {1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000},
          _maxChannelValue[maxChannelCount] = {2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000},
          _channelDeadZone[maxChannelCount] = {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5},
          _channelIsReverse[maxChannelCount] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    ProtocolDetector _selectedProtocol = ProtocolDetector::IBUS;

    //=================================================

    State state() { return _state; }
    uint8_t rssi() { return _rssi; }

    float channel(unsigned channel)
    {
        if (channel == 0 or
            channel > maxChannelCount or
            channel > _channelsCount)
            return NAN;
        else
            return _channels[channel - 1];
    }

    float channel(ChannelFunction ch)
    {
        if (static_cast<unsigned>(ch) >= static_cast<unsigned>(ChannelFunction::__end) or
            _channelsAssign[static_cast<unsigned>(ch)] == 0)
            return NAN;

        return _channels[_channelsAssign[static_cast<unsigned>(ch)]];
    }

    bool inDZ(unsigned channel)
    {
        if (channel == 0 or
            channel > maxChannelCount or
            channel > _channelsCount)
            return false;

        return _channelInDZ[channel - 1];
    }

    bool inDZ(ChannelFunction ch)
    {
        if (static_cast<unsigned>(ch) >= static_cast<unsigned>(ChannelFunction::__end) or
            _channelsAssign[static_cast<unsigned>(ch)] == 0)
            return false;

        return _channelInDZ[_channelsAssign[static_cast<unsigned>(ch)]];
    }

    uint8_t channelCount() { return _channelsCount; }

    void update(int16_t channels[], unsigned channelCount, uint8_t rssi, bool signalAvailable)
    {
        _lastValidTimestamp = millis();

        channelCount = std::min<unsigned>(maxChannelCount, channelCount);
        _channelsCount = channelCount;
        _rssi = rssi;

        for (unsigned i = 0; i < channelCount; i++)
        {
            float normalizedValue = channels[i];
            normalizedValue -= _minChannelValue[i];
            normalizedValue /= float(_maxChannelValue[i] - _minChannelValue[i]);
            normalizedValue = std::clamp<float>(normalizedValue, 0, 1);

            if (_channelIsReverse[i] < 0)
                normalizedValue = 1 - normalizedValue;

            bool inDZ = std::abs(normalizedValue) <= (_channelDeadZone[i] * 1e-3);
            normalizedValue = normalizedValue * 2 - 1;
            normalizedValue = std::clamp<float>(normalizedValue, -1, 1);

            if (inDZ)
                normalizedValue = 0;
            _channelInDZ[i] = inDZ;
            _channels[i] = normalizedValue;
        }
        for (unsigned i = channelCount; i < maxChannelCount; i++)
            _channels[i] = NAN;

        if (not signalAvailable)
            _state = State::signal_lose;
        else
            _state = State::ok;
    }

    uint32_t protocolProbeStart = 0;
    static constexpr uint32_t protocolProbeTime = 100;

    IBus ibusParser;
    SBus sBusParser;
    static constexpr RC_parser *parsers[] = {
        &ibusParser, // 20 40 DB 05 DC 05 54 05 DC 05 E8 03 D0 07 D2 05 E8 03 DC 05 DC 05 DC 05 DC 05 DC 05 DC 05 DA F3
        &sBusParser,
    };

    void incomingByteHandler()
    {
        if (_selectedProtocol == ProtocolDetector::not_connected or
            static_cast<int>(_selectedProtocol) >= static_cast<int>(ProtocolDetector::__end))
            return;

        int16_t ch[maxChannelCount];
        unsigned channelCount;
        uint8_t rssi;
        bool signalAvailable;

        if (parsers[static_cast<int>(_selectedProtocol) - 1]->parseData(RC_UART.read(), RC_UART.getParityErrorFlag(),
                                                                        ch, channelCount, rssi, signalAvailable) == false)
        {
            RC_UART.clearParityErrorFlag();
            return;
        }
        RC_UART.clearParityErrorFlag();
        update(ch, channelCount, rssi, signalAvailable);
    }

    // void protocolProbeHandler()
    // {
    //     if (_currentProtocol != ProtocolDetector::not_connected)
    //         return;

    //     if (millis() - protocolProbeStart < protocolProbeTime)
    //         return;
    //     protocolProbeStart = millis();

    //     RC_UART.end();
    //     while (RC_UART.available())
    //         RC_UART.read();
    //     RC_UART.clearParityErrorFlag(), RC_UART.clearWriteError();

    //     switch (protocolProbe)
    //     {
    //     case ProtocolDetector::not_connected:
    //         protocolProbe = ProtocolDetector::IBUS;
    //         RC_UART.begin(115'200);
    //         break;
    //     case ProtocolDetector::IBUS:
    //         protocolProbe = ProtocolDetector::SBUS;
    //         RC_UART.begin(100'000,
    //                       UART::WordLen::nine, UART::StopBit::two, UART::ParityControl::odd,
    //                       false, false, true);
    //         break;
    //     default:
    //         protocolProbe = ProtocolDetector::not_connected;
    //         break;
    //     }
    // }

    void checkValues()
    {
        for (float &max : _maxChannelValue)
            max = std::clamp<float>(max, 1'500, 2'200);
        for (float &min : _minChannelValue)
            min = std::clamp<float>(min, 800, 1'500);
        for (float &dz : _channelDeadZone)
            dz = std::clamp<float>(dz, 0, 100);
        for (float &rev : _channelIsReverse)
            rev = rev > 0 ? 1 : -1;
        for (int32_t &assign : _channelsAssign)
            if (assign < 0 or assign > maxChannelCount)
                assign = 0;
        if (static_cast<int32_t>(_selectedProtocol) >= static_cast<int32_t>(ProtocolDetector::__end))
            _selectedProtocol = ProtocolDetector::not_connected;
    }

    void init() {}

    void enable()
    {
        checkValues();
        RC_UART.end();
        RC_UART.attachOnReceiveIRQ(incomingByteHandler);

        switch (_selectedProtocol)
        {
        case ProtocolDetector::IBUS:
            RC_UART.begin(115'200);
            break;
        case ProtocolDetector::SBUS:
            RC_UART.begin(100'000,
                          UART::WordLen::nine, UART::StopBit::two, UART::ParityControl::odd,
                          false, false, true);
            break;
        default:
            break;
        }
    }

    void handler()
    {
        if (millis() - _lastValidTimestamp > signalLoseTimeout)
            _state = State::signal_lose;
    }

    REGISTER_SRT_MODULE(RC, init, enable, handler);
}