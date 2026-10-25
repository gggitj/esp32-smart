#include "protocol/pibridge_protocol.h"
#include "protocol/data_checksum.h"
#include <cstdlib>
#include <iostream>

void require(bool ok, const char *message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    require(dataChecksum("123456789").toHex() == "2639f4cb", "independent CRC32 vector");
    const auto golden = QByteArray::fromHex("5042523101010700000002007b7db634");
    require(PiBridge::encode(1, PiBridge::ControlEvent, 7, "{}") == golden, "independent CRC vector");
    quint8 direction = 0; PiBridge::Kind kind{}; quint32 seq = 0; QByteArray payload;
    for (int split = 0; split < golden.size(); ++split) {
        auto buffer = golden.left(split);
        require(!PiBridge::decode(buffer, direction, kind, seq, payload), "premature partial decode");
        buffer += golden.mid(split);
        require(PiBridge::decode(buffer, direction, kind, seq, payload), "fragment lost");
        require(direction == 1 && seq == 7 && payload == "{}" && buffer.isEmpty(), "decoded fields");
    }
    auto bad = golden; bad[bad.size() - 1] = char(0);
    auto buffer = QByteArray("noise") + bad + golden + golden;
    require(PiBridge::decode(buffer, direction, kind, seq, payload), "bad CRC recovery");
    require(PiBridge::decode(buffer, direction, kind, seq, payload), "coalesced frame");
    require(buffer.isEmpty(), "buffer not consumed");
    require(PiBridge::encode(1, PiBridge::ControlEvent, 0, QByteArray(2049, 'x')).isEmpty(), "oversize accepted");
    std::cout << "protocol tests passed\n";
}
